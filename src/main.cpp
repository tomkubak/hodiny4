// =====================================================================
//  Digitronové hodiny – 4 digitrony, 1× HV5222, ESP32-C3
//  Firmware 2.2 (stejný základ jako 6místné hodiny)
//  Zapojení desky (piny CLK/DIN/OE a typ digitronů) se nastavuje ve webu,
//  průvodce "Hledat zapojení" umí vyzkoušet všechny kombinace pinů.
//
//   - přenos do HV5222 s nastavitelnou délkou pulzu a zhasnutím přes OE
//   - NTP na pozadí, časové pásmo s automatickým letním/zimním časem
//   - volitelně RTC DS3231 (HAS_RTC), jinak čas z NTP nebo ručně z webu
//   - neblokující start, WiFi s automatickým návratem, captive portál
//   - OTA přes web (ElegantOTA) i z PlatformIO (ArduinoOTA)
//   - noční režim, 10 efektů čištění katod, dvojtečka svítí/bliká,
//     testovací režim digitronů
// =====================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <ArduinoOTA.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>
#include <soc/gpio_reg.h>
#include <freertos/semphr.h>

// ============ VOLBY SESTAVENÍ ============
// 1 = na desce je RTC DS3231 na I2C (SDA_PIN/SCL_PIN), 0 = bez RTC
#ifndef HAS_RTC
#define HAS_RTC 0
#endif

#if HAS_RTC
#include <Wire.h>
#include <RTClib.h>
#endif

#include "web_ui.h"
#include "icons.h"

#if !defined(ELEGANTOTA_USE_ASYNC_WEBSERVER) || (ELEGANTOTA_USE_ASYNC_WEBSERVER != 1)
#error "V platformio.ini musi byt build_flags = -DELEGANTOTA_USE_ASYNC_WEBSERVER=1"
#endif

#define FW_VERSION "2.3.0-4"

// ============ HARDWARE ============
#define SDA_PIN 8      // jen pro HAS_RTC
#define SCL_PIN 9

// Výchozí zapojení (platí i po továrním nastavení). Piny i typ digitronů se mění ve webu.
// CLK, DIN i OE vedou přes invertující tranzistor (OE = PWM jasu).
static constexpr uint8_t DEF_PIN_CLK = 0;
static constexpr uint8_t DEF_PIN_DIN = 2;
static constexpr uint8_t DEF_PIN_OE  = 4;
static constexpr uint8_t PIN_NONE    = 0xFF;   // OE nezapojené (jas pak nejde řídit)
#define PIN_PWR  -1    // řízení VN zdroje (-1 = není)

// GPIO vyvedené na ESP32-C3 SuperMini, které smí displej použít.
// Ne: 11–17 (SPI flash, zápis by shodil čip), 18/19 (USB – přišel bys o sériovou linku).
static const uint8_t GPIO_OK[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21};
static constexpr int GPIO_OK_N = sizeof(GPIO_OK);

static bool gpioAllowed(long p) {
#if HAS_RTC
  if (p == SDA_PIN || p == SCL_PIN) return false;
#endif
  for (uint8_t g : GPIO_OK) if (g == p) return true;
  return false;
}

// oe = -1 nebo PIN_NONE: OE nezapojené
static bool pinsValid(long clk, long din, long oe) {
  if (oe == PIN_NONE) oe = -1;
  if (!gpioAllowed(clk) || !gpioAllowed(din) || clk == din) return false;
  return oe < 0 || (gpioAllowed(oe) && oe != clk && oe != din);
}

// Počet bitů jednoho snímku (HV5222 má 32 výstupů, mapování používá indexy 1–32)
static constexpr int FRAME_BITS = 33;

// Doutnavky dvojtečky – výstupy HV5222, které je rozsvěcují
static constexpr uint64_t COLON_BITS = (1ULL << 1) | (1ULL << 2);

// Pokud má driver latch (LE / STROBE) a připojíš ho na volný GPIO, zadej číslo pinu.
// -1 = bez latche (současné zapojení).
#define PIN_LE   -1
#define LE_ACTIVE_HIGH 1

// Časování posuvného registru [µs]. Bezpečně pomalé – i s tranzistorovými
// převodníky úrovní. Snímek trvá 32 × (pulz + 3 µs), při 20 µs cca 0,75 ms.
static constexpr uint32_t SR_SETUP_US = 1;    // data před stažením CLK (pak mají ještě celý pulz)
static constexpr uint32_t SR_PULSE_US = 10;   // CLK v LOW – tranzistorový převodník má pomalou náběžnou hranu
static constexpr uint32_t SR_HOLD_US  = 2;    // aktivní hrana (sestupná na HV5222) je rychlá

// Jak často se snímek pošle znovu, i když se nezměnil (samooprava glitchů).
static constexpr uint32_t DISPLAY_REFRESH_MS = 1000;

// PWM jasu na OE
static constexpr int   PWM_CHANNEL  = 0;
static constexpr int   PWM_FREQ     = 2000;
static constexpr int   PWM_RES      = 10;
static constexpr int   PWM_MAX      = (1 << PWM_RES) - 1;
static constexpr float BRIGHT_GAMMA = 1.0f;  // 1.0 = stejná lineární křivka jako fw 1.x (digitrony při krátkých pulzech nezapalují)

// Starý firmware 1.x ukládal do RTC místní čas s chybným TZ (americká pravidla DST)
#if HAS_RTC
static const char* LEGACY_TZ = "UTC-1DST";
#endif
static const char* DEFAULT_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";

// ============ MAPOVÁNÍ DIGITRONŮ ============
static constexpr int NT = 4;                      // počet digitronů
// Kolik katod (0..n-1) má každá pozice: desítky hodin 0–2, desítky minut 0–5
static const uint8_t TUBE_N[NT] = {3, 10, 6, 10};

struct MapEntry { uint8_t hv; uint8_t tube; int8_t cathode; };   // tube 0–3
enum TubeType : uint8_t { TUBE_IN12B = 0, TUBE_Z1082M = 1 };

static const MapEntry MAP_Z1082M[] = {
  {32,0,0}, {31,0,1}, {30,0,2},
  {29,1,7}, {28,1,8}, {27,1,6}, {26,1,9}, {25,1,0}, {24,1,1}, {23,1,2}, {22,1,3}, {21,1,4}, {20,1,5},
  {19,2,0}, {18,2,1}, {17,2,2}, {16,2,3}, {15,2,4}, {14,2,5},
  {13,3,6}, {12,3,5}, {11,3,4}, {10,3,3}, {9,3,2}, {8,3,1}, {7,3,0}, {6,3,9}, {4,3,8}, {3,3,7}
};
static const MapEntry MAP_IN12B[] = {
  {32,0,0}, {31,0,1}, {30,0,2},
  {29,1,0}, {28,1,9}, {27,1,8}, {26,1,7}, {25,1,6}, {24,1,5}, {23,1,4}, {22,1,3}, {21,1,2}, {20,1,1},
  {19,2,0}, {18,2,5}, {17,2,4}, {16,2,3}, {15,2,2}, {14,2,1},
  {13,3,1}, {12,3,2}, {11,3,3}, {10,3,4}, {9,3,5}, {8,3,6}, {7,3,7}, {6,3,8}, {5,3,9}, {4,3,0}
};
static uint8_t hvIndex[NT][10];                   // sestaví se podle typu digitronů, 0 = katoda není

static void buildHvIndex(uint8_t type) {
  const MapEntry* map = type == TUBE_Z1082M ? MAP_Z1082M : MAP_IN12B;
  const size_t n = type == TUBE_Z1082M ? sizeof(MAP_Z1082M) / sizeof(MapEntry) : sizeof(MAP_IN12B) / sizeof(MapEntry);
  memset(hvIndex, 0, sizeof(hvIndex));
  for (size_t i = 0; i < n; i++) {
    const MapEntry& m = map[i];
    if (m.tube < NT && m.cathode >= 0 && m.cathode <= 9) hvIndex[m.tube][m.cathode] = m.hv;
  }
}

// ============ NASTAVENÍ ============
struct Settings {
  // čas
  String ntp1 = "ntp.nic.cz";
  String ntp2 = "tik.cesnet.cz";
  String tz   = DEFAULT_TZ;
  uint16_t ntpIntervalMin = 360;
  // displej
  uint8_t  bright = 20;
  bool     hideZero = false;
  bool     showDate = false;
  uint8_t  colon = 0;              // 0 svítí, 1 bliká, 2 vypnuto
  // noc
  bool     nightOn = false;
  uint16_t nightFrom = 23 * 60;
  uint16_t nightTo   = 6 * 60;
  uint8_t  nightBright = 10;
  bool     nightHvOff = false;
  // čištění katod
  uint8_t  cleanMode = 1;          // 0 vyp, 1 každou minutu, 2 každých 10 min, 3 každou hodinu
  uint8_t  cleanFx = 0;            // efekt čištění, 10 = náhodný
  bool     deepOn = true;
  uint8_t  deepHour = 3;
  uint8_t  deepMinutes = 5;
  // síť a systém
  String   hostname = "hodiny";
  uint8_t  txPower = 60;           // 0,25 dBm jednotky (60 = 15 dBm)
  uint8_t  srPulse = 10;           // délka CLK pulzu v µs (s 1k pull-upy stačí 10)
  bool     srBlank = true;         // zhasnout přes OE během přenosu
  // zapojení desky
  uint8_t  pinClk = DEF_PIN_CLK;
  uint8_t  pinDin = DEF_PIN_DIN;
  uint8_t  pinOe  = DEF_PIN_OE;    // PIN_NONE = nezapojené
  uint8_t  tube   = TUBE_IN12B;
  String   otaUser = "";
  String   otaPass = "";
};

Settings cfg;                      // sdílená kopie (chráněná mutexem)
Settings cur;                      // pracovní kopie hlavní smyčky
SemaphoreHandle_t cfgMutex;
volatile uint32_t cfgVersion = 0;
uint32_t cfgSeenVersion = 0;
uint32_t cfgChangedMs = 0;
bool     cfgSavePending = false;

String wifiSsid, wifiPass;         // čte/zapisuje se jen pod mutexem

struct CfgLock {
  CfgLock()  { xSemaphoreTake(cfgMutex, portMAX_DELAY); }
  ~CfgLock() { xSemaphoreGive(cfgMutex); }
};

// ============ GLOBÁLNÍ OBJEKTY ============
#if HAS_RTC
RTC_DS3231 rtc;
#endif
Preferences prefs;
AsyncWebServer server(80);
DNSServer dns;

// ============ STAV ============
enum TimeSrc : uint8_t { SRC_NONE, SRC_RTC, SRC_NTP, SRC_MANUAL };
static const char* const TIME_SRC_NAMES[] = {"none", "rtc", "ntp", "manual"};

volatile uint8_t  timeSrc = SRC_NONE;
volatile uint32_t lastSyncEpoch = 0;      // poslední synchronizace (NTP nebo ručně)
volatile uint32_t lastNtpEpoch = 0;
volatile bool     ntpEvent = false;       // nastavuje SNTP callback
volatile bool     rtcOk = false;
volatile bool     rtcValid = false;
volatile float    rtcTemp = NAN;
bool              rtcWritePending = false;

// požadavky z webu -> zpracuje hlavní smyčka
volatile bool     reqNtpSync = false;
volatile bool     reqClean = false;
volatile bool     reqScan = false;
volatile bool     reqRestart = false;
volatile bool     reqFactory = false;
volatile bool     reqWifiSave = false;
volatile bool     reqSetTime = false;
volatile uint32_t reqSetEpoch = 0;
volatile uint32_t reqSetUsec = 0;
uint32_t          restartAtMs = 0;
String            pendingSsid, pendingPass;   // pod mutexem

// OTA
volatile bool     otaActive = false;
volatile uint8_t  otaPct = 0;
volatile uint32_t otaEndMs = 0;
volatile uint32_t otaLastMs = 0;          // poslední průběh webového OTA (hlídání přerušeného nahrávání)
static constexpr uint32_t OTA_STALL_MS = 60000;
volatile bool     otaOk = false;

// efekty a stavy displeje
volatile uint8_t  curBrightness = 0;
volatile bool     hvOn = true;
volatile bool     nightActive = false;
volatile bool     deepActive = false;

// ============ POMOCNÉ ============
static inline bool timeIsValid() { return time(nullptr) > 1700000000; }

static String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  return o;
}

static bool minuteInWindow(uint16_t m, uint16_t from, uint16_t to) {
  if (from == to) return false;
  if (from < to) return m >= from && m < to;
  return m >= from || m < to;              // přes půlnoc
}

// =====================================================================
//  DISPLEJ – posuvné registry
// =====================================================================
static int appliedDuty = -1;

// Aktuální piny displeje (mění je jen hlavní smyčka: nastavení nebo hledání zapojení)
static uint8_t gClk = PIN_NONE, gDin = PIN_NONE, gOe = PIN_NONE;
static bool    pwmOn = false;                // PWM jasu je připojené na gOe

static void pwmAttach() {
  if (gOe == PIN_NONE) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(gOe, PWM_FREQ, PWM_RES);
  if (appliedDuty >= 0) ledcWrite(gOe, appliedDuty);
#else
  ledcAttachPin(gOe, PWM_CHANNEL);
#endif
  pwmOn = true;
}

static void pwmDetach() {
  if (!pwmOn) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(gOe);
#else
  ledcDetachPin(gOe);
#endif
  pwmOn = false;
}

// Zhasnutí přes OE během přenosu: výstupy driveru pak při posouvání bitů
// nespínají VN zátěž a nerozkmitávají zem/napájení pod pomalou hranou CLK.
static void oeForceOff() {
  pwmDetach();
  pinMode(gOe, OUTPUT);
  digitalWrite(gOe, HIGH);                  // OE aktivní v LOW -> HIGH = zhasnuto
}

static void oeRestore() { pwmAttach(); }

namespace disp { extern bool hasFrame; }

// Přepne displej na jiné piny. Staré piny, které v novém zapojení nejsou, se uvolní
// (vstup = vysoká impedance), aby zkoušená zapojení nikdy nebudila vodič, který jim nepatří.
static void setDisplayPins(uint8_t clk, uint8_t din, uint8_t oe, bool pwm) {
  pwmDetach();
  const uint8_t old[3] = {gClk, gDin, gOe};
  for (uint8_t p : old)
    if (p != PIN_NONE && p != clk && p != din && p != oe) pinMode(p, INPUT);
  gClk = clk; gDin = din; gOe = oe;
  pinMode(din, OUTPUT); digitalWrite(din, HIGH);   // klidový stav (invertováno)
  pinMode(clk, OUTPUT); digitalWrite(clk, HIGH);
  if (oe != PIN_NONE) {
    pinMode(oe, OUTPUT); digitalWrite(oe, HIGH);   // zhasnuto, než naběhne PWM
    if (pwm) pwmAttach();
  }
  disp::hasFrame = false;                          // poslat snímek hned
}

namespace disp {
  uint64_t lastFrame = 0;
  uint32_t lastSendMs = 0;
  bool     hasFrame = false;

  static inline void pins(uint32_t setMask, uint32_t clrMask) {
    if (setMask) REG_WRITE(GPIO_OUT_W1TS_REG, setMask);
    if (clrMask) REG_WRITE(GPIO_OUT_W1TC_REG, clrMask);
  }

  volatile uint8_t pulseUs = SR_PULSE_US;  // kopie cur.srPulse
  volatile bool    blankOe = true;         // kopie cur.srBlank

  // DIN je invertovaný (bit 1 = pin LOW). HV5222 posouvá na sestupné hraně
  // svého CLK = přechod LOW -> HIGH na straně ESP (přes invertující tranzistor).
  // Bity jdou od indexu 0 nahoru, stejně jako v původním firmwaru.
  // Pošle snímek na datové piny md (může jich být víc – hledání zapojení), CLK = mc.
  void sendRaw(uint32_t mc, uint32_t md, uint64_t bits) {
    const uint32_t pulse = pulseUs;
    for (int i = 0; i < FRAME_BITS; i++) {
      if ((bits >> i) & 1ULL) pins(0, md); else pins(md, 0);
      delayMicroseconds(SR_SETUP_US);
      pins(0, mc);                       // CLK LOW (neaktivní hrana)
      delayMicroseconds(pulse);
      pins(mc, 0);                       // CLK HIGH (aktivní hrana)
      delayMicroseconds(SR_HOLD_US);     // hold – data se ještě nemění
    }
    pins(md, 0);                         // klidový stav datové linky

#if PIN_LE >= 0
    delayMicroseconds(SR_HOLD_US);
    digitalWrite(PIN_LE, LE_ACTIVE_HIGH ? HIGH : LOW);
    delayMicroseconds(SR_PULSE_US);
    digitalWrite(PIN_LE, LE_ACTIVE_HIGH ? LOW : HIGH);
#endif
  }

  void sendFrame(uint64_t bits) {
    if (gClk == PIN_NONE || gDin == PIN_NONE) return;
    const bool blank = blankOe && pwmOn && appliedDuty >= 0;
    if (blank) { oeForceOff(); delayMicroseconds(20); }
    sendRaw(1UL << gClk, 1UL << gDin, bits);       // GPIO_OK jsou všechny < 32
    if (blank) { delayMicroseconds(SR_HOLD_US); oeRestore(); }
  }

  uint64_t build(const int8_t d[NT], bool colon) {
    uint64_t bits = colon ? COLON_BITS : 0;
    for (int i = 0; i < NT; i++) {
      int v = d[i];
      if (v < 0 || v > 9) continue;
      uint8_t hv = hvIndex[i][v];
      if (hv) bits |= 1ULL << hv;
    }
    return bits;
  }

  // Pošle snímek, pokud se změnil, nebo pokud uplynul čas obnovení.
  void show(const int8_t d[NT], bool colon, bool force = false) {
    uint64_t f = build(d, colon);
    uint32_t now = millis();
    if (!force && hasFrame && f == lastFrame && now - lastSendMs < DISPLAY_REFRESH_MS) return;
    sendFrame(f);
    lastFrame = f; lastSendMs = now; hasFrame = true;
  }

  void blank() {
    const int8_t d[NT] = {-1, -1, -1, -1};
    show(d, false, true);
  }

  void initLatch() {
#if PIN_LE >= 0
    pinMode(PIN_LE, OUTPUT);
    digitalWrite(PIN_LE, LE_ACTIVE_HIGH ? LOW : HIGH);
#endif
  }
}

// =====================================================================
//  JAS / VN
// =====================================================================

void pwmInit() {
#if ESP_ARDUINO_VERSION_MAJOR < 3
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);   // pin se připojí v setDisplayPins
#endif
}

void applyBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  int on = 0;
  if (pct > 0) {
    float lin = powf(pct / 100.0f, BRIGHT_GAMMA);
    on = (int)lroundf(lin * PWM_MAX);
    if (on < 1) on = 1;
  }
  int duty = PWM_MAX - on;                 // OE aktivní v LOW (jako ve fw 1.x)
  curBrightness = pct;
  if (duty == appliedDuty) return;
  appliedDuty = duty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (pwmOn) ledcWrite(gOe, duty);         // jinak se zapíše při připojení PWM
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

static int hvApplied = -1;

void applyHv(bool on) {
  hvOn = on;
  if (hvApplied == (int)on) return;
  hvApplied = on;
#if PIN_PWR >= 0
  digitalWrite(PIN_PWR, on ? HIGH : LOW);
#endif
}

// =====================================================================
//  NASTAVENÍ – NVS
// =====================================================================
void loadSettings() {
  Settings s;
  prefs.begin("clock", true);
  s.ntp1           = prefs.getString("ntp1", prefs.getString("ntp", s.ntp1));
  s.ntp2           = prefs.getString("ntp2", s.ntp2);
  s.tz             = prefs.getString("tz", s.tz);
  s.ntpIntervalMin = prefs.getUShort("ntpint", s.ntpIntervalMin);
  s.bright         = (uint8_t)constrain(prefs.getInt("bright", s.bright), 0, 100);   // klíč z fw 1.x
  s.hideZero       = prefs.getBool("hidezero", s.hideZero);
  s.showDate       = prefs.getBool("showdate", s.showDate);
  s.colon          = prefs.getUChar("colon", s.colon);
  s.nightOn        = prefs.getBool("nighton", s.nightOn);
  s.nightFrom      = prefs.getUShort("nfrom", s.nightFrom);
  s.nightTo        = prefs.getUShort("nto", s.nightTo);
  s.nightBright    = prefs.getUChar("nbright", s.nightBright);
  s.nightHvOff     = prefs.getBool("nhvoff", s.nightHvOff);
  s.cleanMode      = prefs.getUChar("clean", s.cleanMode);
  s.cleanFx        = prefs.getUChar("cleanfx", s.cleanFx);
  s.deepOn         = prefs.getBool("deepon", s.deepOn);
  s.deepHour       = prefs.getUChar("deeph", s.deepHour);
  s.deepMinutes    = prefs.getUChar("deepm", s.deepMinutes);
  s.hostname       = prefs.getString("host", s.hostname);
  s.txPower        = prefs.getUChar("txpow", s.txPower);
  s.srPulse        = prefs.getUChar("srpulse", s.srPulse);
  s.srBlank        = prefs.getBool("srblank", s.srBlank);
  s.pinClk         = prefs.getUChar("pclk", s.pinClk);
  s.pinDin         = prefs.getUChar("pdin", s.pinDin);
  s.pinOe          = prefs.getUChar("poe", s.pinOe);
  s.tube           = prefs.getUChar("tube", s.tube);
  s.otaUser        = prefs.getString("otauser", s.otaUser);
  s.otaPass        = prefs.getString("otapass", s.otaPass);
  String ssid      = prefs.getString("ssid", "");
  String pass      = prefs.getString("pass", "");
  prefs.end();

  // hodnoty z NVS mohou pocházet ze starší verze -> stejné meze jako ve webu (handleConfigPost)
  if (s.tz.length() == 0) s.tz = DEFAULT_TZ;
  s.ntpIntervalMin = constrain(s.ntpIntervalMin, 15, 1440);
  s.bright      = min<uint8_t>(s.bright, 100);
  s.colon       = min<uint8_t>(s.colon, 2);
  s.nightFrom   = min<uint16_t>(s.nightFrom, 1439);
  s.nightTo     = min<uint16_t>(s.nightTo, 1439);
  s.nightBright = min<uint8_t>(s.nightBright, 100);
  s.cleanMode   = min<uint8_t>(s.cleanMode, 3);
  s.cleanFx     = min<uint8_t>(s.cleanFx, 10);
  s.deepHour    = min<uint8_t>(s.deepHour, 23);
  s.deepMinutes = constrain(s.deepMinutes, 1, 30);
  s.txPower     = constrain(s.txPower, 8, 78);
  s.srPulse     = constrain(s.srPulse, 6, 25);
  if (s.hostname.length() == 0) s.hostname = "hodiny";
  if (!pinsValid(s.pinClk, s.pinDin, s.pinOe)) { s.pinClk = DEF_PIN_CLK; s.pinDin = DEF_PIN_DIN; s.pinOe = DEF_PIN_OE; }
  if (s.tube > TUBE_Z1082M) s.tube = TUBE_IN12B;

  CfgLock l;
  cfg = s;
  wifiSsid = ssid;
  wifiPass = pass;
  cfgVersion++;
}

void saveSettings(const Settings& s) {
  prefs.begin("clock", false);
  prefs.putString("ntp1", s.ntp1);
  prefs.putString("ntp2", s.ntp2);
  prefs.putString("tz", s.tz);
  prefs.putUShort("ntpint", s.ntpIntervalMin);
  prefs.putInt("bright", s.bright);
  prefs.putBool("hidezero", s.hideZero);
  prefs.putBool("showdate", s.showDate);
  prefs.putUChar("colon", s.colon);
  prefs.putBool("nighton", s.nightOn);
  prefs.putUShort("nfrom", s.nightFrom);
  prefs.putUShort("nto", s.nightTo);
  prefs.putUChar("nbright", s.nightBright);
  prefs.putBool("nhvoff", s.nightHvOff);
  prefs.putUChar("clean", s.cleanMode);
  prefs.putUChar("cleanfx", s.cleanFx);
  prefs.putBool("deepon", s.deepOn);
  prefs.putUChar("deeph", s.deepHour);
  prefs.putUChar("deepm", s.deepMinutes);
  prefs.putString("host", s.hostname);
  prefs.putUChar("txpow", s.txPower);
  prefs.putUChar("srpulse", s.srPulse);
  prefs.putBool("srblank", s.srBlank);
  prefs.putUChar("pclk", s.pinClk);
  prefs.putUChar("pdin", s.pinDin);
  prefs.putUChar("poe", s.pinOe);
  prefs.putUChar("tube", s.tube);
  prefs.putString("otauser", s.otaUser);
  prefs.putString("otapass", s.otaPass);
  prefs.end();
}

// =====================================================================
//  ČAS – TZ, SNTP, RTC
// =====================================================================
static char ntpBuf1[64], ntpBuf2[64], tzBuf[64];   // SNTP si drží jen ukazatele!

void onSntpSync(struct timeval* tv) {
  (void)tv;
  ntpEvent = true;          // zbytek dodělá hlavní smyčka
}

void applyTimeZone() {
  setenv("TZ", tzBuf, 1);
  tzset();
}

void startSntp() {
  strlcpy(ntpBuf1, cur.ntp1.c_str(), sizeof(ntpBuf1));
  strlcpy(ntpBuf2, cur.ntp2.c_str(), sizeof(ntpBuf2));
  strlcpy(tzBuf,   cur.tz.c_str(),   sizeof(tzBuf));

  uint32_t intervalMs = (uint32_t)max<uint16_t>(cur.ntpIntervalMin, 1) * 60000UL;
  sntp_set_sync_interval(intervalMs);
  sntp_set_time_sync_notification_cb(onSntpSync);
  configTzTime(tzBuf, ntpBuf1, ntpBuf2[0] ? ntpBuf2 : nullptr);
}

#if HAS_RTC
static bool rtcSane(const DateTime& t) {
  return t.isValid() && t.year() >= 2024 && t.year() <= 2099;
}

// Zarovnání systémového času na RTC – čeká se na hranu sekundy RTC,
// takže přesnost je v řádu milisekund. Neblokující.
struct {
  bool     active = false;
  int      lastSec = -1;
  uint32_t startMs = 0;
  uint32_t lastPollMs = 0;
} rtcAlign;
uint32_t lastRtcAlignMs = 0;

void startRtcAlign() {
  if (!rtcOk || !rtcValid) return;
  rtcAlign.active = true;
  rtcAlign.lastSec = -1;
  rtcAlign.startMs = millis();
  rtcAlign.lastPollMs = 0;
  lastRtcAlignMs = millis();
}

void updateRtcAlign() {
  if (!rtcAlign.active) return;
  uint32_t now = millis();
  if (now - rtcAlign.lastPollMs < 4) return;
  rtcAlign.lastPollMs = now;
  if (now - rtcAlign.startMs > 1600) { rtcAlign.active = false; return; }

  DateTime t = rtc.now();
  if (!rtcSane(t)) { rtcAlign.active = false; return; }
  if (rtcAlign.lastSec < 0) { rtcAlign.lastSec = t.second(); return; }
  if (t.second() == rtcAlign.lastSec) return;

  // právě začala nová sekunda RTC
  rtcAlign.active = false;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t sysUs = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
  int64_t rtcUs = (int64_t)t.unixtime() * 1000000LL;
  int64_t diff  = sysUs - rtcUs;
  if (!timeIsValid() || diff > 30000 || diff < -30000) {
    struct timeval nt = { (time_t)t.unixtime(), 0 };
    settimeofday(&nt, nullptr);
  }
  if (timeSrc == SRC_NONE) timeSrc = SRC_RTC;
}

void writeRtcNow(uint32_t epoch) {
  if (!rtcOk) return;
  rtc.adjust(DateTime(epoch));
  rtcValid = true;
}

// Při startu: načti RTC (UTC). Jednorázově převeď čas uložený fw 1.x.
void initRtcAndClock() {
  rtcOk = rtc.begin(&Wire);
  if (!rtcOk) return;

  rtc.disable32K();
  bool lost = rtc.lostPower();
  DateTime t = rtc.now();
  rtcValid = !lost && rtcSane(t);

  prefs.begin("clock", false);
  bool migrated = prefs.getBool("rtcutc", false);
  if (!migrated) {
    if (rtcValid) {
      // fw 1.x ukládal místní čas podle "UTC-1DST" (= DST podle USA)
      setenv("TZ", LEGACY_TZ, 1); tzset();
      struct tm tmv = {};
      tmv.tm_year = t.year() - 1900; tmv.tm_mon = t.month() - 1; tmv.tm_mday = t.day();
      tmv.tm_hour = t.hour(); tmv.tm_min = t.minute(); tmv.tm_sec = t.second();
      tmv.tm_isdst = -1;
      time_t utc = mktime(&tmv);
      rtc.adjust(DateTime((uint32_t)utc));
      t = rtc.now();
    }
    prefs.putBool("rtcutc", true);
  }
  prefs.end();
  applyTimeZone();

  if (rtcValid) {
    struct timeval tv = { (time_t)t.unixtime(), 0 };
    settimeofday(&tv, nullptr);
    timeSrc = SRC_RTC;
    startRtcAlign();                       // doladí na milisekundy
  }
  rtcTemp = rtc.getTemperature();
}
#else
uint32_t lastRtcAlignMs = 0;
void startRtcAlign() {}
void updateRtcAlign() {}
void writeRtcNow(uint32_t) {}
void initRtcAndClock() { applyTimeZone(); }
#endif

// =====================================================================
//  WiFi
// =====================================================================
enum class NetState : uint8_t { CONNECTING, STA, AP };
NetState netState = NetState::CONNECTING;
uint32_t netStateMs = 0;
uint32_t lastStaRetryMs = 0;
uint32_t staLostMs = 0;
uint32_t staUpSinceMs = 0;
bool     apOn = false;
bool     servicesStarted = false;
bool     sntpStarted = false;
static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t STA_RETRY_MS          = 120000;
static constexpr uint32_t STA_LOST_TO_AP_MS     = 300000;
static constexpr uint32_t AP_GRACE_MS           = 120000;

String apSsid() { return String("hodiny"); }   // otevřený AP jako ve fw 1.x

void startAp() {
  if (apOn) return;
  WiFi.mode(wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(apSsid().c_str());
  WiFi.setTxPower((wifi_power_t)cur.txPower);
  delay(100);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
}

void stopAp() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower((wifi_power_t)cur.txPower);
  apOn = false;
}

void beginSta() {
  String s, p;
  { CfgLock l; s = wifiSsid; p = wifiPass; }
  if (!s.length()) return;
  WiFi.begin(s.c_str(), p.c_str());
  lastStaRetryMs = millis();
}

void startServices();
void startBootIpShow(IPAddress ip);

// AP režim: STA se nesmí neustále připojovat (přeskakuje kanály a telefon
// připojený k AP pak nenačte stránku). Zkoušíme jen občas a jen bez klientů.
void enterApState(uint32_t now) {
  startAp();
  WiFi.setAutoReconnect(false);
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false);
  netState = NetState::AP;
  netStateMs = now;
  lastStaRetryMs = now;
}

void enterStaState(uint32_t now) {
  WiFi.setAutoReconnect(true);
  netState = NetState::STA;
  netStateMs = now;
  staUpSinceMs = now;
  staLostMs = 0;
  reqNtpSync = true;                       // čas hned po připojení
}

void wifiInit() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(cur.hostname.c_str());
  bool haveCreds;
  { CfgLock l; haveCreds = wifiSsid.length() > 0; }
  uint32_t now = millis();
  if (haveCreds) {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower((wifi_power_t)cur.txPower);
    beginSta();
    netState = NetState::CONNECTING;
    netStateMs = now;
  } else {
    enterApState(now);
    startServices();
    startBootIpShow(WiFi.softAPIP());
  }
}

void wifiLoop() {
  uint32_t now = millis();
  bool staUp = WiFi.status() == WL_CONNECTED;
  bool haveCreds = wifiSsid.length() > 0;  // zapisuje jen hlavní smyčka

  switch (netState) {
    case NetState::CONNECTING:
      if (staUp) {
        enterStaState(now);
        startServices();
        startBootIpShow(WiFi.localIP());
      } else if (now - netStateMs > STA_CONNECT_TIMEOUT_MS) {
        enterApState(now);
        startServices();
        startBootIpShow(WiFi.softAPIP());
      }
      break;

    case NetState::STA:
      if (!staUp) {
        if (!staLostMs) staLostMs = now;
        if (now - staLostMs > STA_LOST_TO_AP_MS) enterApState(now);   // dlouho bez sítě -> AP
      } else {
        staLostMs = 0;
        if (apOn && now - staUpSinceMs > AP_GRACE_MS && WiFi.softAPgetStationNum() == 0) stopAp();
      }
      break;

    case NetState::AP:
      if (staUp) {
        enterStaState(now);                // AP zůstane ještě AP_GRACE_MS
      } else if (haveCreds && now - lastStaRetryMs > STA_RETRY_MS &&
                 WiFi.softAPgetStationNum() == 0) {
        beginSta();                        // router mohl naběhnout později než hodiny
      }
      break;
  }

  if (apOn) dns.processNextRequest();
}

// =====================================================================
//  OTA
// =====================================================================
// Procenta OTA: stovky na 1. digitronu (0–2), desítky na 2., jednotky na 4.
// (3. digitron umí jen 0–5)
void otaDigits(uint8_t p, int8_t d[NT]) {
  d[0] = p >= 100 ? 1 : -1;
  d[1] = p >= 10 ? (p / 10) % 10 : -1;
  d[2] = -1;
  d[3] = p % 10;
}

void setupArduinoOta() {
  ArduinoOTA.setHostname(cur.hostname.c_str());
  if (cur.otaPass.length()) ArduinoOTA.setPassword(cur.otaPass.c_str());
  ArduinoOTA.setRebootOnSuccess(true);
  ArduinoOTA.onStart([]() {
    disableLoopWDT();
    otaActive = true; otaPct = 0; otaLastMs = millis();
    applyHv(true);
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    uint8_t pct = t ? (uint8_t)((uint64_t)p * 100 / t) : 0;
    otaPct = pct;
    int8_t d[NT];
    otaDigits(pct, d);
    disp::show(d, false);                  // voláno z hlavní smyčky -> bezpečné
  });
  ArduinoOTA.onEnd([]() { otaOk = true; });
  ArduinoOTA.onError([](ota_error_t) {
    otaActive = false; otaEndMs = millis(); otaOk = false;
    enableLoopWDT();
  });
  ArduinoOTA.begin();
}

void setupElegantOta() {
  ElegantOTA.begin(&server);
  if (cur.otaUser.length() && cur.otaPass.length())
    ElegantOTA.setAuth(cur.otaUser.c_str(), cur.otaPass.c_str());
  ElegantOTA.setAutoReboot(true);
  // callbacky běží v úloze webserveru -> jen nastavujeme proměnné
  ElegantOTA.onStart([]() { otaActive = true; otaPct = 0; otaOk = false; otaLastMs = millis(); });
  ElegantOTA.onProgress([](size_t c, size_t f) {
    otaLastMs = millis();
    if (f) otaPct = (uint8_t)min<uint64_t>(100, (uint64_t)c * 100 / f);
  });
  ElegantOTA.onEnd([](bool ok) {
    otaOk = ok; otaEndMs = millis();
    if (!ok) otaActive = false;
  });
}

void startServices() {
  if (servicesStarted) return;
  servicesStarted = true;
  setupArduinoOta();                       // spouští i mDNS
  MDNS.addService("http", "tcp", 80);
}

// =====================================================================
//  ZOBRAZENÍ – režimy
// =====================================================================
// boot: Knight Rider při připojování, pak IP adresa
struct {
  bool     ipShow = false;
  uint8_t  ipPart = 0;
  uint32_t ipMs = 0;
  IPAddress ip;
} bootSeq;

void startBootIpShow(IPAddress ip) {
  bootSeq.ip = ip;
  bootSeq.ipShow = true;
  bootSeq.ipPart = 0;
  bootSeq.ipMs = millis();
}

// Oktet IP: stovky (0–2) na 1., desítky na 2., jednotky na 4. digitronu
static void showOctet(int v, int8_t d[NT]) {
  d[0] = v >= 100 ? v / 100 : -1;
  d[1] = v >= 10 ? (v / 10) % 10 : -1;
  d[2] = -1;
  d[3] = v % 10;
}

// testovací režim z webu
volatile bool   testMode = false;
volatile uint32_t testMs = 0;             // poslední požadavek /api/test
static constexpr uint32_t TEST_TIMEOUT_MS = 15UL * 60 * 1000;   // zapomenutý test se sám vypne
volatile int8_t testDigits[NT] = {0, 0, 0, 0};
volatile bool   testColon = true;

// =====================================================================
//  ČIŠTĚNÍ KATOD – efekty
//  Každý digitron má svou "stopu": od kdy je zhasnutý, kdy začne a jakou
//  posloupnost číslic projede. Mimo stopu ukazuje aktuální čas.
//  Všechny efekty projedou na každém digitronu všech 10 katod.
// =====================================================================
static constexpr uint8_t FX_COUNT  = 10;      // index FX_COUNT = náhodný efekt
static constexpr uint8_t FX_SEQMAX = 40;

struct FxTube {
  int32_t  blankFrom;                         // -1 = před startem ukazuje čas
  uint32_t start;
  uint8_t  len;
  uint8_t  seq[FX_SEQMAX];
};

struct {
  bool     active = false;
  uint32_t t0 = 0;
  uint32_t endMs = 0;
  uint16_t step = 30;
  FxTube   tube[NT];
} fx;

volatile int8_t reqCleanFx = -1;              // -1 = podle nastavení

static void shuffleN(uint8_t* a, int n = NT) {
  for (int i = n - 1; i > 0; i--) { int j = esp_random() % (i + 1); uint8_t t = a[i]; a[i] = a[j]; a[j] = t; }
}

// Projede katody digitronu i (jen ty, které má: 0..TUBE_N[i]-1).
// count = 0 -> právě jedno kolo všech katod.
static void fxRun(int i, uint32_t start, uint8_t from = 0, uint8_t count = 0, int8_t dir = 1) {
  FxTube& t = fx.tube[i];
  uint8_t n = TUBE_N[i];
  if (count == 0) count = n;
  t.start = start;
  t.len = min<uint8_t>(count, FX_SEQMAX);
  for (uint8_t k = 0; k < t.len; k++) t.seq[k] = (uint8_t)((from + 100 * n + dir * k) % n);
}

void timeDigits(const struct tm& t, int8_t d[NT]);

void startCleanAnim(int sel = -1) {
  uint8_t e = sel < 0 ? cur.cleanFx : (uint8_t)sel;
  if (e >= FX_COUNT) e = esp_random() % FX_COUNT;

  int8_t live[NT] = {0, 0, 0, 0};
  if (timeIsValid()) { time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt); timeDigits(lt, live); }

  for (auto& t : fx.tube) { t.blankFrom = -1; t.start = 0; t.len = 0; }
  fx.step = 40;                                     // 4 digitrony -> o něco pomaleji než u 6
  uint8_t order[NT] = {0, 1, 2, 3};

  switch (e) {
    case 0:   // Postupně zleva – klasika (jako původní firmware)
      { uint32_t t0 = 0; for (int i = 0; i < NT; i++) { fxRun(i, t0); t0 += TUBE_N[i] * fx.step; } }
      break;

    case 1:   // Náhodné rozsvícení – vše zhasne, digitrony naskakují v náhodném pořadí
      shuffleN(order);
      for (int k = 0; k < NT; k++) { int i = order[k]; fx.tube[i].blankFrom = 0; fxRun(i, 150 + k * 450); }
      break;

    case 2:   // Automat – všechny se točí a zastavují se postupně zleva
      fx.step = 50;
      for (int i = 0; i < NT; i++) fxRun(i, 0, esp_random() % TUBE_N[i], 20 + i * 5);
      break;

    case 3:   // Vlna – rozjíždí se zleva doprava s překryvem
      for (int i = 0; i < NT; i++) fxRun(i, i * 120);
      break;

    case 4:   // Sudé a liché – 1. a 3., pak 2. a 4.
      for (int i = 0; i < NT; i++) fxRun(i, (i % 2) * 420);
      break;

    case 5:   // Počítadlo – od vlastní číslice dokola, zprava doleva
      fx.step = 45;
      for (int i = 0; i < NT; i++) {
        uint8_t n = TUBE_N[i];
        uint8_t from = live[i] < 0 ? 0 : (live[i] + 1) % n;
        fxRun(i, (NT - 1 - i) * 180, from);
      }
      break;

    case 6:   // Šum – náhodné číslice, zamykají se v náhodném pořadí
      fx.step = 55;
      shuffleN(order);
      for (int k = 0; k < NT; k++) {
        int i = order[k];
        FxTube& t = fx.tube[i];
        uint8_t n = TUBE_N[i];
        uint8_t perm[10];
        for (uint8_t q = 0; q < n; q++) perm[q] = q;
        for (int q = n - 1; q > 0; q--) { int j = esp_random() % (q + 1); uint8_t x = perm[q]; perm[q] = perm[j]; perm[j] = x; }
        t.start = 0;
        t.len = n + 4 * k;                       // všechny katody + náhodný dojezd
        for (uint8_t q = 0; q < t.len; q++) t.seq[q] = q < n ? perm[q] : esp_random() % n;
      }
      break;

    case 7:   // Rozpad – postupně zhasnou a pak se náhodně znovu rozsvítí
      shuffleN(order);
      for (int k = 0; k < NT; k++) fx.tube[order[k]].blankFrom = k * 160;
      shuffleN(order);
      for (int k = 0; k < NT; k++) fxRun(order[k], 800 + k * 350);
      break;

    case 8: { // Ze středu ven
      static const uint8_t dist[NT] = {1, 0, 0, 1};
      for (int i = 0; i < NT; i++) fxRun(i, dist[i] * 400);
      break;
    }

    case 9: { // Do středu
      static const uint8_t dist[NT] = {0, 1, 1, 0};
      for (int i = 0; i < NT; i++) fxRun(i, dist[i] * 400);
      break;
    }
  }

  uint32_t end = 0;
  for (auto& t : fx.tube) end = max<uint32_t>(end, t.start + (uint32_t)t.len * fx.step);
  fx.endMs = end;
  fx.t0 = millis();
  fx.active = true;
}

// Přepíše číslice podle běžícího efektu.
void applyCleanFx(int8_t d[NT]) {
  if (!fx.active) return;
  uint32_t t = millis() - fx.t0;
  if (t >= fx.endMs) { fx.active = false; return; }
  for (int i = 0; i < NT; i++) {
    const FxTube& u = fx.tube[i];
    if (u.len && t >= u.start) {
      uint32_t idx = (t - u.start) / fx.step;
      if (idx < u.len) d[i] = u.seq[idx];
    } else if (u.blankFrom >= 0 && (int32_t)t >= u.blankFrom) {
      d[i] = -1;
    }
  }
}

void timeDigits(const struct tm& t, int8_t d[NT]) {
  d[0] = t.tm_hour / 10; d[1] = t.tm_hour % 10;
  d[2] = t.tm_min / 10;  d[3] = t.tm_min % 10;
  if (cur.hideZero && d[0] == 0) d[0] = -1;
}

// Vrací stav dvojtečky.
bool composeDisplay(int8_t d[NT]) {
  uint32_t now = millis();

  // 1) OTA
  if (otaActive) { otaDigits(otaPct, d); return false; }

  // 2) testovací režim z webu
  if (testMode) {
    for (int i = 0; i < NT; i++) d[i] = testDigits[i];
    return testColon;
  }

  // 3) připojování k WiFi – Knight Rider s nulou
  if (netState == NetState::CONNECTING) {
    static const uint8_t seq[] = {0, 1, 2, 3, 2, 1};
    uint8_t pos = seq[(now / 120) % sizeof(seq)];
    for (int i = 0; i < NT; i++) d[i] = (i == pos) ? 0 : -1;
    return false;
  }

  // 4) IP adresa po připojení – 4 oktety po 1,5 s s krátkou mezerou
  if (bootSeq.ipShow) {
    uint32_t el = now - bootSeq.ipMs;
    uint8_t part = el / 1900;
    if (part >= 4) bootSeq.ipShow = false;
    else {
      if (el % 1900 < 1500) showOctet(bootSeq.ip[part], d);
      else for (int i = 0; i < NT; i++) d[i] = -1;
      return false;
    }
  }

  // 5) čas není známý – blikající nuly
  if (!timeIsValid()) {
    bool on = (now / 500) % 2;
    for (int i = 0; i < NT; i++) d[i] = on ? 0 : -1;
    return on;
  }

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec;
  struct tm lt;
  localtime_r(&t, &lt);
  bool colon = cur.colon == 0 || (cur.colon == 1 && tv.tv_usec < 500000);

  // 6) hloubkové čištění (v noci)
  if (deepActive) {
    uint32_t s = (uint32_t)t;
    for (int i = 0; i < NT; i++) d[i] = (s + i) % TUBE_N[i];
    return false;
  }

  // 7) datum v sekundách 30–33: MM DD
  //    (1. digitron umí jen 0–2, takže den 30/31 by vlevo nešel)
  if (cur.showDate && lt.tm_sec >= 30 && lt.tm_sec < 34 && !fx.active) {
    int m = lt.tm_mon + 1;
    d[0] = m / 10; d[1] = m % 10;
    d[2] = lt.tm_mday / 10; d[3] = lt.tm_mday % 10;
    return false;
  }

  // 8) čas (+ případně animace čištění)
  timeDigits(lt, d);
  applyCleanFx(d);
  return colon;
}

// =====================================================================
//  HLEDÁNÍ ZAPOJENÍ – postupně najde CLK, DIN a OE
//  Během hledání jsou všechny vybrané piny výstupy. Po každém snímku jsou všechny kromě CLK
//  v LOW: ať je OE kterýkoli z nich, displej svítí naplno (OE přes invertující tranzistor).
//   1 CLK – zkoušený pin hodinuje, data jdou na všechny ostatní piny -> rozsvítí se 12:34
//   2 DIN – data jen na zkoušený pin, ostatní během snímku HIGH (= nuly)
//   4 OE  – zkoušený pin rozbliká displej
//  Na displeji je vzor 1 2 : 3 4 podle zvoleného typu digitronů (jiné číslice = jiný typ).
// =====================================================================
enum ScanCmd : uint8_t { SC_NONE = 0, SC_START, SC_NEXT, SC_PREV, SC_PAUSE, SC_RESUME, SC_OK, SC_NOOE, SC_STOP };
enum ScanPhase : uint8_t { SP_OFF = 0, SP_CLK, SP_DIN, SP_OE = 4 };   // čísla shodná s 6místnými hodinami
static constexpr uint32_t SCAN_IDLE_MS    = 30UL * 60 * 1000;   // bez obsluhy z webu se hledání ukončí
static constexpr uint32_t SCAN_REFRESH_MS = 700;
static const int8_t SCAN_PATTERN[NT] = {1, 2, 3, 4};

struct {
  uint8_t  phase = SP_OFF;
  bool     paused = false;
  uint8_t  cand[GPIO_OK_N + 2];      // všechny piny, se kterými hledání pracuje
  uint8_t  nc = 0;
  uint8_t  list[GPIO_OK_N + 2];      // piny zkoušené v aktuální fázi
  uint8_t  count = 0, idx = 0;
  uint16_t stepMs = 2500;
  uint32_t lastMs = 0, cmdMs = 0, frameMs = 0;
  uint8_t  clk = PIN_NONE, din = PIN_NONE, oe = PIN_NONE;
} scan;

// požadavek z webu -> zpracuje hlavní smyčka
volatile uint8_t  reqScanCmd = SC_NONE;
volatile uint32_t reqScanMask = 0;   // bit n = GPIO n
volatile uint16_t reqScanStep = 2500;
volatile uint8_t  reqScanPhase = SP_CLK;
volatile int16_t  reqScanIdx = -1;   // index, který viděl uživatel při "použít" (reakce je pomalejší než krok)

static bool scanHas(uint8_t p) {
  for (uint8_t i = 0; i < scan.nc; i++) if (scan.cand[i] == p) return true;
  return false;
}
static void scanAdd(uint8_t p) { if (p != PIN_NONE && !scanHas(p)) scan.cand[scan.nc++] = p; }

static void scanApply() {
  const uint8_t t = scan.list[scan.idx];
  switch (scan.phase) {
    case SP_CLK: scan.clk = t; break;
    case SP_DIN: scan.din = t; break;
    case SP_OE:  scan.oe = t; break;
  }
  for (uint8_t i = 0; i < scan.nc; i++) {
    const uint8_t p = scan.cand[i];
    pinMode(p, OUTPUT);
    digitalWrite(p, p == scan.clk ? HIGH : LOW);   // CLK v klidu, ostatní LOW
  }
  scan.lastMs = millis();
  scan.frameMs = 0;                                // snímek hned
}

static void scanFrame() {
  const uint64_t bits = disp::build(SCAN_PATTERN, true);
  const uint32_t mc = 1UL << scan.clk;
  uint32_t all = 0;
  for (uint8_t i = 0; i < scan.nc; i++) if (scan.cand[i] != scan.clk) all |= 1UL << scan.cand[i];
  if (scan.phase == SP_CLK) {
    disp::sendRaw(mc, all, bits);                  // DIN neznámý: stejná data na všechny ostatní piny
  } else {
    const uint32_t md = 1UL << scan.din;
    disp::pins(all & ~md, 0);                      // ostatní HIGH = nulové bity
    disp::sendRaw(mc, md, bits);
  }
  disp::pins(0, all);                              // po snímku vše LOW -> OE (kdekoli) = plný jas
}

// Ukončí hledání a přepne na zadané zapojení; piny použité při hledání uvolní.
static void scanEnd(uint8_t clk, uint8_t din, uint8_t oe) {
  for (uint8_t i = 0; i < scan.nc; i++) pinMode(scan.cand[i], INPUT);
  scan.phase = SP_OFF;
  gClk = gDin = gOe = PIN_NONE;                    // už uvolněné
  setDisplayPins(clk, din, oe, true);
}
static void scanEnd() { scanEnd(cur.pinClk, cur.pinDin, cur.pinOe); }

static void scanSave(uint8_t oe) {
  { CfgLock l; cfg.pinClk = scan.clk; cfg.pinDin = scan.din; cfg.pinOe = oe; cfgVersion++; }
  scanEnd(scan.clk, scan.din, oe);
}

// Přejde do fáze ph: zkouší se všechny piny, které ještě nemají roli. false = není co zkoušet.
static bool scanPhase(uint8_t ph) {
  scan.phase = ph;
  scan.idx = 0;
  scan.count = 0;
  scan.paused = false;
  for (uint8_t i = 0; i < scan.nc; i++) {
    const uint8_t p = scan.cand[i];
    if (p != scan.clk && p != scan.din) scan.list[scan.count++] = p;
  }
  if (!scan.count) return false;
  scanApply();
  return true;
}

void scanCommand(uint8_t cmd) {
  const uint32_t now = millis();
  scan.cmdMs = now;
  if (cmd == SC_START) {
    if (scan.phase) scanEnd();                     // nové hledání = čistý start
    testMode = false;
    fx.active = false;
    pwmDetach();
    scan.nc = 0;
    for (uint8_t g : GPIO_OK) if ((reqScanMask >> g) & 1UL && gpioAllowed(g)) scanAdd(g);
    scan.clk = scan.din = scan.oe = PIN_NONE;
    uint8_t ph = SP_CLK;
    if (reqScanPhase == SP_OE) {                   // CLK a DIN známé z nastavení
      ph = SP_OE;
      scan.clk = cur.pinClk; scan.din = cur.pinDin;
      scanAdd(scan.clk); scanAdd(scan.din);
    }
    // piny běžného zapojení, se kterými se nezkouší, uvolnit
    const uint8_t old[3] = {gClk, gDin, gOe};
    for (uint8_t p : old) if (p != PIN_NONE && !scanHas(p)) pinMode(p, INPUT);
    scan.stepMs = constrain(reqScanStep, 800, 10000);
    if (!scanPhase(ph)) scanEnd();                 // web to kontroluje předem
    return;
  }
  if (!scan.phase) return;
  const int16_t seen = reqScanIdx;
  if (seen >= 0 && seen < scan.count && seen != scan.idx && (cmd == SC_OK || cmd == SC_PAUSE)) {
    scan.idx = seen;                               // vrátit se na kombinaci, kterou uživatel viděl
    scanApply();
  }
  switch (cmd) {
    case SC_NEXT:   scan.idx = (scan.idx + 1) % scan.count; scanApply(); break;
    case SC_PREV:   scan.idx = (scan.idx + scan.count - 1) % scan.count; scanApply(); break;
    case SC_PAUSE:  scan.paused = true; break;
    case SC_RESUME: scan.paused = false; scan.lastMs = now; break;
    case SC_STOP:   scanEnd(); break;
    case SC_NOOE:   if (scan.phase == SP_OE) scanSave(PIN_NONE); break;
    case SC_OK:
      if (scan.phase == SP_OE) { scanSave(scan.oe); break; }
      if (!scanPhase(scan.phase == SP_CLK ? SP_DIN : SP_OE)) {
        if (scan.phase == SP_OE) scanSave(PIN_NONE);   // na OE nezbyl pin
        else scanEnd();                                // na DIN nezbyl pin
      }
      break;
  }
}

// Volá hlavní smyčka místo běžného zobrazení.
void scanLoop() {
  const uint32_t now = millis();
  if (now - scan.cmdMs > SCAN_IDLE_MS) { scanEnd(); return; }
  if (!scan.paused && now - scan.lastMs >= scan.stepMs) {
    scan.idx = (scan.idx + 1) % scan.count;
    scanApply();
  }
  if (!scan.frameMs || now - scan.frameMs >= SCAN_REFRESH_MS) { scanFrame(); scan.frameMs = now | 1; }
  if (scan.phase == SP_OE) digitalWrite(scan.oe, (now / 250) % 2 ? HIGH : LOW);
}

static int pinJ(uint8_t p) { return p == PIN_NONE ? -1 : p; }

// =====================================================================
//  WEB
// =====================================================================
static bool argStr(AsyncWebServerRequest* r, const char* n, String& out) {
  if (!r->hasParam(n, true)) return false;
  out = r->getParam(n, true)->value();
  out.trim();
  return true;
}
static bool argInt(AsyncWebServerRequest* r, const char* n, long& out, long lo, long hi) {
  if (!r->hasParam(n, true)) return false;
  long v = r->getParam(n, true)->value().toInt();
  out = constrain(v, lo, hi);
  return true;
}
static bool argBool(AsyncWebServerRequest* r, const char* n, bool& out) {
  if (!r->hasParam(n, true)) return false;
  String v = r->getParam(n, true)->value();
  out = (v == "1" || v == "true" || v == "on");
  return true;
}

static const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "zapnutí napájení";
    case ESP_RST_SW:       return "softwarový restart";
    case ESP_RST_PANIC:    return "pád (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "pokles napájení (brownout)";
    case ESP_RST_DEEPSLEEP:return "probuzení";
    default:               return "jiný";
  }
}

String statusJson() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec;
  struct tm lt;
  localtime_r(&t, &lt);
  char tbuf[16], dbuf[16], zbuf[16];
  strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &lt);
  strftime(dbuf, sizeof(dbuf), "%d.%m.%Y", &lt);
  strftime(zbuf, sizeof(zbuf), "%Z", &lt);
  long off = 0;
  { struct tm gt; gmtime_r(&t, &gt); gt.tm_isdst = lt.tm_isdst; off = (long)difftime(mktime(&lt), mktime(&gt)); }
  // mktime(&lt) == t, mktime(gt) = t - offset  -> rozdíl = offset
  bool staUp = WiFi.status() == WL_CONNECTED;

  String j;
  j.reserve(700);
  j += "{\"valid\":"; j += timeIsValid() ? "true" : "false";
  j += ",\"epoch\":"; j += (uint32_t)t;
  j += ",\"ms\":"; j += (uint32_t)(tv.tv_usec / 1000);
  j += ",\"time\":\""; j += tbuf;
  j += "\",\"date\":\""; j += dbuf;
  j += "\",\"wday\":"; j += lt.tm_wday;
  j += ",\"dst\":"; j += lt.tm_isdst > 0 ? "true" : "false";
  j += ",\"zone\":\""; j += jsonEscape(zbuf);
  j += "\",\"utcoff\":"; j += off;
  j += ",\"src\":\""; j += TIME_SRC_NAMES[timeSrc];
  j += "\",\"lastSync\":"; j += (uint32_t)lastSyncEpoch;
  j += ",\"lastNtp\":"; j += (uint32_t)lastNtpEpoch;
  j += ",\"rtc\":"; j += rtcOk ? "true" : "false";
  j += ",\"rtcValid\":"; j += rtcValid ? "true" : "false";
  j += ",\"temp\":"; j += isnan(rtcTemp) ? String("null") : String(rtcTemp, 2);
  j += ",\"sta\":"; j += staUp ? "true" : "false";
  j += ",\"ssid\":\""; j += jsonEscape(WiFi.SSID());
  j += "\",\"ip\":\""; j += staUp ? WiFi.localIP().toString() : String("");
  j += "\",\"rssi\":"; j += staUp ? WiFi.RSSI() : 0;
  j += ",\"ap\":"; j += apOn ? "true" : "false";
  j += ",\"apSsid\":\""; j += jsonEscape(apSsid());
  j += "\",\"apIp\":\""; j += apOn ? WiFi.softAPIP().toString() : String("");
  String host;
  { CfgLock l; host = cfg.hostname; }
  j += "\",\"host\":\""; j += jsonEscape(host);
  j += "\",\"bright\":"; j += curBrightness;
  j += ",\"night\":"; j += nightActive ? "true" : "false";
  j += ",\"hv\":"; j += hvOn ? "true" : "false";
  j += ",\"deep\":"; j += deepActive ? "true" : "false";
  j += ",\"uptime\":"; j += (uint32_t)(esp_timer_get_time() / 1000000ULL);
  j += ",\"heap\":"; j += ESP.getFreeHeap();
  j += ",\"heapMin\":"; j += ESP.getMinFreeHeap();
  j += ",\"ver\":\"" FW_VERSION "\"";
  j += ",\"build\":\"" __DATE__ " " __TIME__ "\"";
  j += ",\"reset\":\""; j += resetReasonStr();
  j += "\",\"ota\":"; j += otaActive ? "true" : "false";
  j += ",\"test\":"; j += testMode ? "true" : "false";
  j += ",\"tubes\":" + String(NT);
  j += ",\"hasRtc\":" + String(HAS_RTC ? "true" : "false");
  j += ",\"hasHv\":" + String(PIN_PWR >= 0 ? "true" : "false");
  j += ",\"scan\":{\"phase\":"; j += scan.phase;
  j += ",\"paused\":"; j += scan.paused ? "true" : "false";
  j += ",\"idx\":"; j += scan.idx;
  j += ",\"count\":"; j += scan.count;
  j += ",\"clk\":"; j += pinJ(scan.clk);
  j += ",\"din1\":"; j += pinJ(scan.din);
  j += ",\"din2\":-1";
  j += ",\"oe\":"; j += pinJ(scan.oe);
  j += "}}";
  return j;
}

String configJson() {
  Settings s;
  String ssid;
  bool hasPass;
  { CfgLock l; s = cfg; ssid = wifiSsid; hasPass = wifiPass.length() > 0; }
  String j;
  j.reserve(600);
  j += "{\"ntp1\":\"" + jsonEscape(s.ntp1) + "\"";
  j += ",\"ntp2\":\"" + jsonEscape(s.ntp2) + "\"";
  j += ",\"tz\":\"" + jsonEscape(s.tz) + "\"";
  j += ",\"ntpint\":" + String(s.ntpIntervalMin);
  j += ",\"bright\":" + String(s.bright);
  j += ",\"hidezero\":" + String(s.hideZero ? "true" : "false");
  j += ",\"showdate\":" + String(s.showDate ? "true" : "false");
  j += ",\"colon\":" + String(s.colon);
  j += ",\"nighton\":" + String(s.nightOn ? "true" : "false");
  j += ",\"nfrom\":" + String(s.nightFrom);
  j += ",\"nto\":" + String(s.nightTo);
  j += ",\"nbright\":" + String(s.nightBright);
  j += ",\"nhvoff\":" + String(s.nightHvOff ? "true" : "false");
  j += ",\"clean\":" + String(s.cleanMode);
  j += ",\"cleanfx\":" + String(s.cleanFx);
  j += ",\"deepon\":" + String(s.deepOn ? "true" : "false");
  j += ",\"deeph\":" + String(s.deepHour);
  j += ",\"deepm\":" + String(s.deepMinutes);
  j += ",\"host\":\"" + jsonEscape(s.hostname) + "\"";
  j += ",\"txpow\":" + String(s.txPower);
  j += ",\"srpulse\":" + String(s.srPulse);
  j += ",\"srblank\":" + String(s.srBlank ? "true" : "false");
  j += ",\"pclk\":" + String(s.pinClk);
  j += ",\"pdin\":" + String(s.pinDin);
  j += ",\"poe\":" + String(pinJ(s.pinOe));
  j += ",\"tube\":" + String(s.tube);
  j += ",\"gpios\":[";
  for (int i = 0; i < GPIO_OK_N; i++) {
    if (!gpioAllowed(GPIO_OK[i])) continue;
    if (j[j.length() - 1] != '[') j += ",";
    j += GPIO_OK[i];
  }
  j += "]";
  j += ",\"otauser\":\"" + jsonEscape(s.otaUser) + "\"";
  j += ",\"otapassset\":" + String(s.otaPass.length() ? "true" : "false");
  j += ",\"ssid\":\"" + jsonEscape(ssid) + "\"";
  j += ",\"passset\":" + String(hasPass ? "true" : "false");
  j += "}";
  return j;
}

static bool validHostname(const String& h) {
  if (h.length() < 1 || h.length() > 31) return false;
  for (char c : h) if (!(isalnum((unsigned char)c) || c == '-')) return false;
  return h[0] != '-' && h[h.length() - 1] != '-';
}

void handleConfigPost(AsyncWebServerRequest* r) {
  String sv; long iv; bool bv;
  String err;
  {
    CfgLock l;
    Settings s = cfg;
    if (argStr(r, "ntp1", sv)) { if (sv.length() && sv.length() < 63) s.ntp1 = sv; else err = "Neplatný NTP server"; }
    if (argStr(r, "ntp2", sv)) { if (sv.length() < 63) s.ntp2 = sv; }
    if (argStr(r, "tz", sv))   { if (sv.length() >= 3 && sv.length() < 63) s.tz = sv; else err = "Neplatné časové pásmo"; }
    if (argInt(r, "ntpint", iv, 15, 1440)) s.ntpIntervalMin = iv;
    if (argInt(r, "bright", iv, 0, 100)) s.bright = iv;
    if (argBool(r, "hidezero", bv)) s.hideZero = bv;
    if (argBool(r, "showdate", bv)) s.showDate = bv;
    if (argInt(r, "colon", iv, 0, 2)) s.colon = iv;
    if (argBool(r, "nighton", bv)) s.nightOn = bv;
    if (argInt(r, "nfrom", iv, 0, 1439)) s.nightFrom = iv;
    if (argInt(r, "nto", iv, 0, 1439)) s.nightTo = iv;
    if (argInt(r, "nbright", iv, 0, 100)) s.nightBright = iv;
    if (argBool(r, "nhvoff", bv)) s.nightHvOff = bv;
    if (argInt(r, "clean", iv, 0, 3)) s.cleanMode = iv;
    if (argInt(r, "cleanfx", iv, 0, 10)) s.cleanFx = iv;
    if (argBool(r, "deepon", bv)) s.deepOn = bv;
    if (argInt(r, "deeph", iv, 0, 23)) s.deepHour = iv;
    if (argInt(r, "deepm", iv, 1, 30)) s.deepMinutes = iv;
    if (argStr(r, "host", sv)) { if (validHostname(sv)) s.hostname = sv; else err = "Neplatný název (a-z, 0-9, -)"; }
    if (argInt(r, "txpow", iv, 8, 78)) s.txPower = iv;
    if (argInt(r, "srpulse", iv, 6, 25)) s.srPulse = iv;
    if (argBool(r, "srblank", bv)) s.srBlank = bv;
    {
      long pc = s.pinClk, pd = s.pinDin, po = s.pinOe == PIN_NONE ? -1 : s.pinOe;
      bool given = false;
      if (argInt(r, "pclk", iv, -1, 99)) { pc = iv; given = true; }
      if (argInt(r, "pdin", iv, -1, 99)) { pd = iv; given = true; }
      if (argInt(r, "poe", iv, -1, 99))  { po = iv; given = true; }
      if (given) {
        if (pinsValid(pc, pd, po)) { s.pinClk = pc; s.pinDin = pd; s.pinOe = po < 0 ? PIN_NONE : po; }
        else err = "Neplatné zapojení: každý signál na jiném povoleném GPIO";
      }
    }
    if (argInt(r, "tube", iv, 0, 1)) s.tube = iv;
    if (argStr(r, "otauser", sv)) s.otaUser = sv;
    if (argStr(r, "otapass", sv) && sv.length()) s.otaPass = sv;
    if (argBool(r, "otaclear", bv) && bv) { s.otaUser = ""; s.otaPass = ""; }
    if (err.length() == 0) { cfg = s; cfgVersion++; }
  }
  if (err.length()) r->send(400, "text/plain; charset=utf-8", err);
  else r->send(200, "application/json", "{\"ok\":true}");
}

void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html; charset=utf-8",
                                                   (const uint8_t*)WEB_PAGE, strlen_P(WEB_PAGE));
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });

  auto sendStatic = [](AsyncWebServerRequest* r, const char* type, const uint8_t* data, size_t len) {
    AsyncWebServerResponse* res = r->beginResponse(200, type, data, len);
    res->addHeader("Cache-Control", "public, max-age=604800");
    r->send(res);
  };
  server.on("/favicon.svg", HTTP_GET, [sendStatic](AsyncWebServerRequest* r) {
    sendStatic(r, "image/svg+xml", (const uint8_t*)ICON_SVG, strlen_P(ICON_SVG));
  });
  server.on("/apple-touch-icon.png", HTTP_GET, [sendStatic](AsyncWebServerRequest* r) {
    sendStatic(r, "image/png", ICON_PNG, ICON_PNG_LEN);
  });
  server.on("/favicon.ico", HTTP_GET, [sendStatic](AsyncWebServerRequest* r) {
    sendStatic(r, "image/png", ICON_PNG, ICON_PNG_LEN);
  });
  server.on("/manifest.json", HTTP_GET, [sendStatic](AsyncWebServerRequest* r) {
    sendStatic(r, "application/manifest+json", (const uint8_t*)WEB_MANIFEST, strlen_P(WEB_MANIFEST));
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", statusJson());
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", configJson());
  });

  server.on("/api/config", HTTP_POST, handleConfigPost);

  server.on("/api/ntpsync", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (WiFi.status() != WL_CONNECTED) {
      r->send(409, "text/plain; charset=utf-8", "Hodiny nejsou připojené k internetu");
      return;
    }
    reqNtpSync = true;
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/settime", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("ms", true)) { r->send(400, "text/plain", "ms?"); return; }
    // čas v ms od epochy (UTC) – z prohlížeče
    uint64_t ms = strtoull(r->getParam("ms", true)->value().c_str(), nullptr, 10);
    if (ms < 1700000000000ULL || ms > 4102444800000ULL) {
      r->send(400, "text/plain; charset=utf-8", "Neplatný čas");
      return;
    }
    reqSetUsec  = (uint32_t)((ms % 1000) * 1000);
    reqSetEpoch = (uint32_t)(ms / 1000);
    reqSetTime  = true;
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/clean", HTTP_POST, [](AsyncWebServerRequest* r) {
    long fxv = -1;
    argInt(r, "fx", fxv, 0, FX_COUNT);
    reqCleanFx = (int8_t)fxv;
    reqClean = true;
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {             // -2: neběží, žádné výsledky
      reqScan = true;
      r->send(200, "application/json", "{\"running\":true}");
      return;
    }
    if (n == WIFI_SCAN_RUNNING) {
      r->send(200, "application/json", "{\"running\":true}");
      return;
    }
    String j = "{\"running\":false,\"list\":[";
    bool first = true;
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (!s.length()) continue;
      bool dup = false;                      // výsledky jsou seřazené dle síly
      for (int k = 0; k < i; k++) if (WiFi.SSID(k) == s) { dup = true; break; }
      if (dup) continue;
      if (!first) j += ",";
      first = false;
      j += "{\"ssid\":\"" + jsonEscape(s) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
           ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    j += "]}";
    WiFi.scanDelete();
    r->send(200, "application/json", j);
  });

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* r) {
    String ssid, pass;
    if (!argStr(r, "ssid", ssid) || ssid.length() == 0 || ssid.length() > 32) {
      r->send(400, "text/plain; charset=utf-8", "Zadej SSID");
      return;
    }
    if (r->hasParam("pass", true)) pass = r->getParam("pass", true)->value();   // heslo netrimovat
    {
      CfgLock l;
      if (pass.length() == 0 && ssid == wifiSsid) pass = wifiPass;            // heslo beze změny
      if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63)) {
        r->send(400, "text/plain; charset=utf-8", "Heslo musí mít 8–63 znaků");
        return;
      }
      pendingSsid = ssid;
      pendingPass = pass;
    }
    reqWifiSave = true;
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest* r) {
    bool on; long v;
    if (argBool(r, "on", on)) testMode = on;
    testMs = millis();
    static const char* names[NT] = {"d1", "d2", "d3", "d4"};
    for (int i = 0; i < NT; i++) if (argInt(r, names[i], v, -1, 9)) testDigits[i] = (int8_t)v;
    if (argBool(r, "colon", on)) testColon = on;
    r->send(200, "application/json", testMode ? "{\"test\":true}" : "{\"test\":false}");
  });

  server.on("/api/pinscan", HTTP_POST, [](AsyncWebServerRequest* r) {
    String c;
    if (!argStr(r, "cmd", c)) { r->send(400, "text/plain", "cmd?"); return; }
    static const struct { const char* n; uint8_t c; } kCmd[] = {
      {"start", SC_START}, {"next", SC_NEXT}, {"prev", SC_PREV}, {"pause", SC_PAUSE},
      {"resume", SC_RESUME}, {"ok", SC_OK}, {"nooe", SC_NOOE}, {"stop", SC_STOP}};
    uint8_t cmd = SC_NONE;
    for (const auto& k : kCmd) if (c == k.n) cmd = k.c;
    if (cmd == SC_NONE) { r->send(400, "text/plain", "cmd?"); return; }
    long v;
    reqScanIdx = argInt(r, "idx", v, -1, 10000) ? (int16_t)v : -1;
    if (cmd == SC_START) {
      // "pins" = seznam GPIO oddělený čárkami
      String list;
      uint32_t mask = 0;
      if (argStr(r, "pins", list)) {
        const char* q = list.c_str();
        while (*q) {
          char* e;
          long g = strtol(q, &e, 10);
          if (e == q) { q++; continue; }
          if (gpioAllowed(g)) mask |= 1UL << g;
          q = e;
        }
      }
      long ph = SP_CLK;
      if (argInt(r, "phase", ph, SP_CLK, SP_OE) && ph != SP_OE) ph = SP_CLK;
      uint8_t pc, pd;
      { CfgLock l; pc = cfg.pinClk; pd = cfg.pinDin; }
      int n = 0;
      for (uint8_t g : GPIO_OK) if ((mask >> g) & 1UL && (ph == SP_CLK || (g != pc && g != pd))) n++;
      if (n < (ph == SP_CLK ? 2 : 1)) {
        r->send(400, "text/plain; charset=utf-8", ph == SP_CLK ? "Vyber aspoň 2 piny" : "Vyber aspoň 1 pin kromě CLK a DIN");
        return;
      }
      if (argInt(r, "step", v, 800, 10000)) reqScanStep = v;
      reqScanMask = mask;
      reqScanPhase = ph;
    }
    reqScanCmd = cmd;
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* r) {
    reqRestart = true;
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/factory", HTTP_POST, [](AsyncWebServerRequest* r) {
    reqFactory = true;
    r->send(200, "application/json", "{\"ok\":true}");
  });

  // captive portál v AP režimu + 404
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (apOn) {
      String host = r->host();
      String apIp = WiFi.softAPIP().toString();
      String h;
      { CfgLock l; h = cfg.hostname; }
      if (host != apIp && host != h + ".local") {
        r->redirect(String("http://") + apIp + "/");
        return;
      }
    }
    r->send(404, "text/plain", "Not found");
  });

  setupElegantOta();
  server.begin();
}

// =====================================================================
//  HLAVNÍ SMYČKA – zpracování
// =====================================================================
void applyConfigChanges() {
  if (cfgVersion == cfgSeenVersion) return;
  Settings prev = cur;
  { CfgLock l; cur = cfg; cfgSeenVersion = cfgVersion; }

  bool first = !sntpStarted;
  if (prev.tube != cur.tube) { buildHvIndex(cur.tube); disp::hasFrame = false; }
  if (!scan.phase && (prev.pinClk != cur.pinClk || prev.pinDin != cur.pinDin || prev.pinOe != cur.pinOe))
    setDisplayPins(cur.pinClk, cur.pinDin, cur.pinOe, true);
  if (disp::pulseUs != cur.srPulse || disp::blankOe != cur.srBlank) {
    disp::pulseUs = cur.srPulse; disp::blankOe = cur.srBlank;
    disp::hasFrame = false;                // pošli snímek hned novým způsobem
  }
  if (first || prev.ntp1 != cur.ntp1 || prev.ntp2 != cur.ntp2 || prev.tz != cur.tz ||
      prev.ntpIntervalMin != cur.ntpIntervalMin) {
    startSntp();                           // nastaví i TZ
    sntpStarted = true;
  }
  if (!first && prev.txPower != cur.txPower) WiFi.setTxPower((wifi_power_t)cur.txPower);
  if (!first && (prev.otaUser != cur.otaUser || prev.otaPass != cur.otaPass)) {
    if (cur.otaUser.length() && cur.otaPass.length()) ElegantOTA.setAuth(cur.otaUser.c_str(), cur.otaPass.c_str());
    else ElegantOTA.clearAuth();
  }
  if (!first) { cfgSavePending = true; cfgChangedMs = millis(); }
}

void handleRequests() {
  uint32_t now = millis();

  if (cfgSavePending && now - cfgChangedMs > 3000) {
    cfgSavePending = false;
    saveSettings(cur);
  }

  if (reqNtpSync) {
    reqNtpSync = false;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if (esp_sntp_enabled()) esp_sntp_restart();
#else
    if (sntp_enabled()) sntp_restart();
#endif
    else startSntp();
  }

  if (ntpEvent) {
    ntpEvent = false;
    timeSrc = SRC_NTP;
    lastNtpEpoch = lastSyncEpoch = (uint32_t)time(nullptr);
    rtcWritePending = true;                // zapíše se na hraně další sekundy
  }

  if (reqSetTime) {
    reqSetTime = false;
    struct timeval tv = { (time_t)reqSetEpoch, (suseconds_t)reqSetUsec };
    settimeofday(&tv, nullptr);
    timeSrc = SRC_MANUAL;
    lastSyncEpoch = reqSetEpoch;
    rtcWritePending = true;
  }

  if (reqClean) { reqClean = false; startCleanAnim(reqCleanFx); }

  if (reqScanCmd != SC_NONE) { const uint8_t c = reqScanCmd; reqScanCmd = SC_NONE; scanCommand(c); }

  if (reqScan) {
    reqScan = false;
    if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true, false);
  }

  if (reqWifiSave) {
    reqWifiSave = false;
    String s, p;
    { CfgLock l; s = pendingSsid; p = pendingPass; wifiSsid = s; wifiPass = p; }
    prefs.begin("clock", false);
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    prefs.end();
    restartAtMs = now + 1500;
  }

  if (reqFactory) {
    reqFactory = false;
    cfgSavePending = false;                // jinak by restart smazaná nastavení zase uložil
    prefs.begin("clock", false);
    prefs.clear();
    prefs.putBool("rtcutc", true);         // RTC už je v UTC
    prefs.end();
    restartAtMs = now + 1000;
  }

  if (reqRestart) { reqRestart = false; restartAtMs = now + 800; }

  // Webové OTA přerušené zavřením prohlížeče nemusí zavolat onEnd -> displej by navždy ukazoval procenta.
  if (otaActive && !otaOk && otaLastMs && now - otaLastMs > OTA_STALL_MS) { otaActive = false; otaEndMs = now; }
  if (testMode && now - testMs > TEST_TIMEOUT_MS) testMode = false;

  if (restartAtMs && (int32_t)(now - restartAtMs) >= 0) {
    if (cfgSavePending) saveSettings(cur);
    disp::blank();
    ESP.restart();
  }
}

// voláno jednou za sekundu, přesně na hraně sekundy systémového času
time_t lastSecond = 0;
int    lastMinute = -1;
uint32_t lastTempMs = 0;

void onSecond(time_t t) {
  if (rtcWritePending) {
    rtcWritePending = false;
    writeRtcNow((uint32_t)t);              // na hraně sekundy = přesnost ~ms
  }

  struct tm lt;
  localtime_r(&t, &lt);
  uint16_t mod = lt.tm_hour * 60 + lt.tm_min;

  // noční režim a hloubkové čištění
  nightActive = cur.nightOn && minuteInWindow(mod, cur.nightFrom, cur.nightTo);
  uint16_t deepFrom = cur.deepHour * 60;
  deepActive = cur.deepOn && timeIsValid() &&
               minuteInWindow(mod, deepFrom, (deepFrom + cur.deepMinutes) % 1440);

  // čištění při změně minuty
  if (lt.tm_min != lastMinute) {
    bool first = lastMinute < 0;
    lastMinute = lt.tm_min;
    bool trig = false;
    switch (cur.cleanMode) {
      case 1: trig = true; break;
      case 2: trig = lt.tm_min % 10 == 0; break;
      case 3: trig = lt.tm_min == 0; break;
    }
    if (trig && !first && !deepActive) startCleanAnim();
  }

  // pokud chybí NTP, každou hodinu se srovnej s RTC (DS3231 je přesnější než krystal ESP)
  uint32_t ms = millis();
  bool ntpRecent = lastNtpEpoch && (uint32_t)t - lastNtpEpoch < 3 * 3600UL;
  if (rtcValid && !ntpRecent && ms - lastRtcAlignMs > 3600000UL) startRtcAlign();

#if HAS_RTC
  if (rtcOk && ms - lastTempMs > 60000) {
    lastTempMs = ms;
    rtcTemp = rtc.getTemperature();
  }
#else
  (void)lastTempMs;
#endif
}

void updateOutputs() {
  // jas a VN zdroj
  uint8_t b = cur.bright;
  bool hv = true;
  if (otaActive || deepActive) {
    b = max<uint8_t>(cur.bright, 30);
  } else if (nightActive) {
    b = cur.nightBright;
    if (b == 0 && cur.nightHvOff) hv = false;
  }
  applyHv(hv);
  applyBrightness(b);
}

// =====================================================================
//  SETUP & LOOP
// =====================================================================
void setup() {
#if PIN_PWR >= 0
  pinMode(PIN_PWR, OUTPUT);
  digitalWrite(PIN_PWR, LOW);
#endif
  // zapojení displeje je v nastavení -> načíst hned a displej co nejdřív do klidového stavu
  cfgMutex = xSemaphoreCreateMutex();
  loadSettings();
  { CfgLock l; cur = cfg; }
  disp::pulseUs = cur.srPulse; disp::blankOe = cur.srBlank;
  disp::initLatch();
  buildHvIndex(cur.tube);
  pwmInit();
  applyBrightness(0);                      // jas 0 dřív, než se PWM připojí na OE
  setDisplayPins(cur.pinClk, cur.pinDin, cur.pinOe, true);
  disp::blank();
  strlcpy(tzBuf, cur.tz.c_str(), sizeof(tzBuf));

#if HAS_RTC
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
#endif
  initRtcAndClock();

  applyConfigChanges();                    // spustí SNTP + TZ
  cfgSeenVersion = cfgVersion;

  applyHv(true);
  applyBrightness(cur.bright);

  wifiInit();
  setupWeb();

  enableLoopWDT();
}

void loop() {
  ElegantOTA.loop();
  if (servicesStarted) ArduinoOTA.handle();

  wifiLoop();
  applyConfigChanges();
  handleRequests();
  updateRtcAlign();

  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec != lastSecond) {
    lastSecond = tv.tv_sec;
    if (timeIsValid()) onSecond(tv.tv_sec);
  }

  updateOutputs();

  if (scan.phase) {
    scanLoop();
  } else {
    int8_t d[NT];
    bool colon = composeDisplay(d);
    disp::show(d, colon);
  }

  delay(1);                                // nech dýchat WiFi a webserver
}
