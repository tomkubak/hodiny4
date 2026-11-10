# Digitronové hodiny – 4 digitrony

Firmware pro digitronové (nixie) hodiny se čtyřmi digitrony (HH:MM) a dvojtečkou, jedním driverem
**HV5222** a deskou **ESP32-C3 SuperMini**. Ovládání je přes webovou stránku v telefonu,
čas z internetu (NTP).

## Co umí

- **Přesný čas** – NTP na pozadí, časové pásmo s automatickým letním/zimním časem,
  ruční nastavení z telefonu. Volitelně RTC DS3231 (při sestavení `HAS_RTC 1`).
- **Webové rozhraní** – vše se nastavuje z prohlížeče, funguje i jako aplikace na ploše telefonu.
- **Wi-Fi s návratem** – když domácí síť nejde, hodiny vytvoří vlastní síť `hodiny`
  (captive portál – stránka se v telefonu otevře sama).
- **Displej** – jas přes PWM, noční režim, dvojtečka svítí / bliká / vypnutá, skrytí úvodní nuly,
  datum (MM DD) v sekundách 30–33.
- **Ochrana katod** – 10 efektů protočení číslic a denní hloubkové čištění.
- **Test digitronů** – libovolné číslice na každém digitronu z webu.
- **Aktualizace přes Wi-Fi** – webem (`/update`) nebo z PlatformIO (ArduinoOTA), průběh na digitronech.
- **Nastavitelné zapojení desky** – typ digitronů (IN-12B / Z1082M), piny CLK / DIN / OE
  a **průvodce, který zapojení sám najde** (viz níže).

## Stažení firmwaru

Hotové soubory jsou v [Releases](../../releases):

| Soubor | K čemu |
|---|---|
| `hodiny4_vX.Y.Z_ota.bin` | aktualizace hodin, které už běží (web `/update` nebo PlatformIO) |
| `hodiny4_vX.Y.Z_full.bin` | první nahrání do čisté desky přes USB (adresa `0x0`) |

### První nahrání přes USB

Nejjednodušší je [ESP Web Flasher](https://espressif.github.io/esptool-js/) v Chrome / Edge:
připoj SuperMini kabelem, *Connect*, soubor `_full.bin` na adresu **0x0**, *Program*.

Nebo z příkazové řádky:

```
esptool.py --chip esp32c3 write_flash 0x0 hodiny4_vX.Y.Z_full.bin
```

(Když se deska nepřipojí: drž tlačítko **BOOT**, krátce stiskni **RESET**, pusť BOOT.)

### Aktualizace

Otevři `http://hodiny.local/update` (nebo IP adresu hodin + `/update`) a nahraj `_ota.bin`.
Nastavení i Wi-Fi zůstanou.

## První spuštění

1. Hodiny po startu vytvoří otevřenou Wi-Fi **`hodiny`**. Připoj se k ní telefonem,
   stránka se otevře sama (jinak `http://192.168.4.1`).
2. V kartě **WiFi** vyber domácí síť a zadej heslo. Hodiny se restartují a připojí.
3. Po připojení ukážou na digitronech svou IP adresu (po oktetech). Stránka pak běží na
   `http://hodiny.local` nebo na té IP adrese.

## Zapojení

Výchozí zapojení (platí i po továrním nastavení):

| Signál | GPIO | Poznámka |
|---|---|---|
| CLK | 0 | přes invertující tranzistor |
| DIN | 2 | přes invertující tranzistor |
| OE (jas) | 4 | PWM, přes invertující tranzistor |
| RTC SDA / SCL | 8 / 9 | jen při sestavení s `HAS_RTC 1` |

Typ digitronů (mapování katod na výstupy HV5222) i piny jdou změnit v kartě **Zapojení desky**
(GPIO 0–10, 20, 21). Dvojtečku rozsvěcují výstupy 1 a 2 driveru.

### Průvodce „Hledat zapojení“

Když nevíš, na kterých pinech co je: **Zapojení desky → Hledat zapojení**. Hodiny postupně zkoušejí
piny a ty u každého kroku stiskneš tlačítko, jakmile se na digitronech něco stane:

1. **CLK** – rozsvítí se nějaké číslice,
2. **DIN** – ukáže se 12:34,
3. **OE** – displej bliká.

Ukazují-li digitrony jiné číslice než 12:34, přepni typ digitronů. Během hledání jsou vybrané piny
výstupy – odškrtni ty, na kterých je zapojené něco jiného. Průvodce počítá s invertujícími
tranzistory na CLK, DIN i OE.

## Sestavení ze zdrojáků

[PlatformIO](https://platformio.org/) (VS Code nebo příkazová řádka):

```
pio run                      # sestavení
pio run -t upload            # nahrání přes USB
pio run -e ota -t upload     # nahrání přes Wi-Fi (hodiny.local)
```

Ověřeno s Arduino core 2.0.17 (platform espressif32 6.x), ESPAsyncWebServer 3.12, AsyncTCP 3.5,
ElegantOTA 3.1.7. Na Macu s Apple Silicon potřebuje toolchain Rosettu
(`softwareupdate --install-rosetta`).

Zdrojáky: `src/main.cpp` (firmware), `src/web_ui.h` (webová stránka), `src/icons.h` (ikony).

## API

Stránka používá jednoduché JSON API – `GET /api/status`, `GET/POST /api/config`, `POST /api/settime`,
`/api/ntpsync`, `/api/clean`, `/api/test`, `/api/pinscan`, `/api/wifi`, `GET /api/scan`,
`/api/restart`, `/api/factory`.
