#pragma once
#include <Arduino.h>

// Webové rozhraní – steampunk. Vše se načítá přes JSON API, stránka je statická.
// Písma z Google Fonts se načítají neblokujícím způsobem; bez internetu (AP režim)
// se použijí systémová patková písma a stránka funguje stejně.
static const char WEB_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="cs">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#1a110a">
<title>Digitronové hodiny</title>
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="icon" href="/favicon.ico" sizes="180x180" type="image/png">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="manifest" href="/manifest.json">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-title" content="Hodiny">
<link rel="stylesheet" media="print" onload="this.media='all'" href="https://fonts.googleapis.com/css2?family=Cinzel:wght@600;700&family=EB+Garamond:wght@400;500;600&display=swap">
<style>
:root{
  --brass:#c9a35a;--brass-hi:#f3d995;--brass-lo:#7a5a26;--copper:#c0703f;
  --leather:#2b1d14;--leather2:#21160f;--ink:#f1e4c8;--mut:#b39b78;
  --nix:#ffb35c;--nix2:#ff7a1a;--ok:#b9d98a;--warn:#f0c060;--bad:#ff8a70;
  --head:"Cinzel","Trajan Pro","Palatino Linotype",Palatino,Georgia,serif;
  --body:"EB Garamond",Garamond,"Palatino Linotype",Palatino,Georgia,serif;
}
*{box-sizing:border-box}
html{background:#0d0805}
body{margin:0;padding:14px 14px 40px;color:var(--ink);font:17px/1.45 var(--body);min-height:100vh;
  background:
    radial-gradient(ellipse at 50% -10%,rgba(201,163,90,.18),transparent 55%),
    repeating-linear-gradient(93deg,rgba(255,255,255,.012) 0 2px,transparent 2px 7px),
    repeating-linear-gradient(87deg,rgba(0,0,0,.18) 0 1px,transparent 1px 11px),
    radial-gradient(ellipse at 50% 30%,#3a2516 0%,#1d130b 55%,#0d0805 100%);
  background-attachment:fixed}
.wrap{max-width:480px;margin:0 auto}

/* ---------- hlavička ---------- */
.top{display:flex;align-items:center;gap:14px;margin:4px 4px 18px;position:relative}
.top img{width:46px;height:46px;border-radius:11px;border:2px solid var(--brass-lo);box-shadow:0 0 0 1px #000,0 0 14px rgba(255,120,30,.35)}
.top h1{margin:0;font:700 1.2em/1.15 var(--head);letter-spacing:.06em;
  background:linear-gradient(180deg,var(--brass-hi),var(--brass) 55%,var(--brass-lo));-webkit-background-clip:text;background-clip:text;color:transparent;
  filter:drop-shadow(0 1px 0 #000)}
.top small{display:block;font:500 .62em var(--body);letter-spacing:.12em;color:var(--mut);-webkit-text-fill-color:var(--mut);text-transform:uppercase;margin-top:3px}
.gears{margin-left:auto;width:58px;height:46px;position:relative;flex:none}
.gears svg{position:absolute;fill:var(--brass);filter:drop-shadow(0 1px 1px #000)}
.g1{width:38px;height:38px;left:0;top:4px;animation:spin 24s linear infinite}
.g2{width:26px;height:26px;right:0;top:0;fill:var(--copper)!important;animation:spin 16.4s linear infinite reverse}
@keyframes spin{to{transform:rotate(360deg)}}
@media (prefers-reduced-motion:reduce){.g1,.g2{animation:none}}

/* ---------- panely ---------- */
.card{position:relative;margin-bottom:18px;padding:24px 20px 20px;border-radius:12px;border:2px solid #9c7a3c;
  background:
    radial-gradient(circle,#f6e1a6 0 1.3px,#b08a42 1.8px 2.8px,#3b2810 3.4px,transparent 4px) 7px 7px/10px 10px no-repeat,
    radial-gradient(circle,#f6e1a6 0 1.3px,#b08a42 1.8px 2.8px,#3b2810 3.4px,transparent 4px) right 7px top 7px/10px 10px no-repeat,
    radial-gradient(circle,#f6e1a6 0 1.3px,#b08a42 1.8px 2.8px,#3b2810 3.4px,transparent 4px) 7px bottom 7px/10px 10px no-repeat,
    radial-gradient(circle,#f6e1a6 0 1.3px,#b08a42 1.8px 2.8px,#3b2810 3.4px,transparent 4px) right 7px bottom 7px/10px 10px no-repeat,
    linear-gradient(180deg,#33241a,var(--leather) 40%,var(--leather2));
  box-shadow:inset 0 0 0 1px #3d2b17,inset 0 0 0 5px #1a110a,inset 0 0 0 6px rgba(201,163,90,.28),inset 0 12px 30px rgba(0,0,0,.35),0 8px 22px rgba(0,0,0,.65)}
h2{margin:0 0 14px;padding-bottom:9px;display:flex;justify-content:space-between;align-items:baseline;gap:10px;
  font:700 .95em var(--head);letter-spacing:.14em;text-transform:uppercase;color:var(--brass-hi);text-shadow:0 1px 0 #000,0 0 10px rgba(201,163,90,.25);
  border-bottom:1px solid transparent;border-image:linear-gradient(90deg,transparent,var(--brass) 15%,var(--brass) 85%,transparent) 1}
h2 .ic{display:inline-block;width:15px;height:15px;margin-right:8px;vertical-align:-2px;background:var(--brass);
  -webkit-mask:var(--gear) center/contain no-repeat;mask:var(--gear) center/contain no-repeat}
:root{--gear:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath d='M10.3 1h3.4l.5 2.9 1.8.8 2.4-1.7 2.4 2.4-1.7 2.4.8 1.8 2.9.5v3.4l-2.9.5-.8 1.8 1.7 2.4-2.4 2.4-2.4-1.7-1.8.8-.5 2.9h-3.4l-.5-2.9-1.8-.8-2.4 1.7-2.4-2.4 1.7-2.4-.8-1.8L1 13.7v-3.4l2.9-.5.8-1.8L3 5.6 5.4 3.2l2.4 1.7 1.8-.8zM12 8a4 4 0 100 8 4 4 0 000-8z'/%3E%3C/svg%3E")}
h2 small{font:500 .78em var(--body);letter-spacing:.02em;text-transform:none;color:var(--mut);text-shadow:none;text-align:right}

/* ---------- digitrony ---------- */
.tubes{display:flex;justify-content:center;align-items:flex-end;gap:clamp(3px,1.2vw,7px);padding:6px 0 0}
.tube{position:relative;width:clamp(36px,11.5vw,52px);height:clamp(64px,20vw,90px);
  border-radius:48% 48% 12% 12%/32% 32% 6% 6%;
  background:
    linear-gradient(90deg,rgba(255,255,255,.10),transparent 22%,transparent 75%,rgba(255,255,255,.05)),
    repeating-linear-gradient(60deg,rgba(255,200,140,.06) 0 1px,transparent 1px 5px),
    repeating-linear-gradient(-60deg,rgba(255,200,140,.06) 0 1px,transparent 1px 5px),
    radial-gradient(ellipse at 50% 55%,#3b230f,#140c06 75%);
  border:1px solid rgba(255,225,190,.22);
  box-shadow:inset 0 2px 6px rgba(255,255,255,.08),inset 0 -10px 16px rgba(0,0,0,.6),0 4px 8px rgba(0,0,0,.6);
  display:flex;align-items:center;justify-content:center;
  font:300 clamp(40px,13vw,60px)/1 "Helvetica Neue",Arial,sans-serif}
.tube::after{content:"";position:absolute;left:-4px;right:-4px;bottom:-9px;height:10px;border-radius:2px 2px 4px 4px;
  background:linear-gradient(180deg,var(--brass-hi),var(--brass) 40%,var(--brass-lo));box-shadow:0 2px 3px rgba(0,0,0,.7)}
.tube .ghost{position:absolute;color:rgba(150,110,70,.13)}
.tube .d{position:relative;color:var(--nix);text-shadow:0 0 4px var(--nix2),0 0 12px rgba(255,110,20,.85),0 0 26px rgba(255,90,10,.45)}
.tubes.off .d{color:rgba(150,110,70,.25);text-shadow:none}
.colon{display:flex;flex-direction:column;gap:clamp(10px,3.5vw,16px);padding-bottom:clamp(14px,5vw,24px)}
.colon i{width:6px;height:6px;border-radius:50%;background:#ff9a4a;box-shadow:0 0 5px #ff7a1a,0 0 12px rgba(255,90,10,.6)}
.tubes.off .colon i,.colon.dim i{background:#4a3424;box-shadow:none}
.plaque{display:block;width:max-content;max-width:100%;margin:22px auto 0;padding:4px 16px;border-radius:4px;
  font:600 .82em var(--head);letter-spacing:.1em;color:#2a1a08;text-shadow:0 1px 0 rgba(255,240,200,.6);
  background:linear-gradient(180deg,var(--brass-hi),var(--brass) 50%,#a88540);border:1px solid #5a3f18;box-shadow:inset 0 1px 0 rgba(255,255,255,.5),0 2px 4px rgba(0,0,0,.6)}
.chips{display:flex;flex-wrap:wrap;gap:6px;justify-content:center;margin-top:12px}
.chip{border:1px solid rgba(201,163,90,.45);border-radius:3px;padding:2px 9px;font-size:.8em;color:var(--mut);background:rgba(0,0,0,.25)}
.chip.ok{color:var(--ok);border-color:rgba(185,217,138,.45)}.chip.warn{color:var(--warn)}.chip.bad{color:var(--bad);border-color:rgba(255,138,112,.5)}

/* ---------- jas – velký regulátor ---------- */
.dial{margin-top:18px;padding:12px 14px 14px;border-radius:10px;background:rgba(0,0,0,.28);border:1px solid rgba(201,163,90,.3);box-shadow:inset 0 2px 8px rgba(0,0,0,.6)}
.dial .lbl{display:flex;justify-content:space-between;align-items:center;font:700 .82em var(--head);letter-spacing:.12em;text-transform:uppercase;color:var(--brass)}
.dial .lbl b{font:600 1.15em var(--body);letter-spacing:0;color:var(--nix);text-shadow:0 0 8px rgba(255,120,30,.6)}

/* ---------- formuláře ---------- */
label{display:block;margin-top:13px;font-size:.92em;color:var(--mut)}
input[type=text],input[type=password],input[type=number],input[type=time],input[type=datetime-local],select{
  width:100%;padding:10px 11px;margin-top:4px;border-radius:6px;border:1px solid #6b5228;background:#170f09;color:var(--ink);
  font:1em var(--body);box-shadow:inset 0 2px 5px rgba(0,0,0,.7);color-scheme:dark}
select{appearance:none;-webkit-appearance:none;padding-right:34px;
  background:#170f09 url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 12 8'%3E%3Cpath d='M1 1l5 5 5-5' fill='none' stroke='%23c9a35a' stroke-width='2'/%3E%3C/svg%3E") right 12px center/12px no-repeat}
input:focus,select:focus{outline:none;border-color:var(--brass-hi);box-shadow:inset 0 2px 5px rgba(0,0,0,.7),0 0 0 2px rgba(243,217,149,.25)}
input::placeholder{color:#7d6a50;font-style:italic}

input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:30px;margin:6px 0 0;background:transparent;cursor:pointer}
input[type=range]::-webkit-slider-runnable-track{height:10px;border-radius:5px;border:1px solid #6b5228;
  background:linear-gradient(90deg,#4a2a12,#c0703f 60%,#ffb35c);box-shadow:inset 0 2px 4px rgba(0,0,0,.8)}
input[type=range]::-moz-range-track{height:10px;border-radius:5px;border:1px solid #6b5228;background:linear-gradient(90deg,#4a2a12,#c0703f 60%,#ffb35c)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:28px;height:28px;margin-top:-10px;border-radius:50%;border:1px solid #3d2a10;
  background:radial-gradient(circle at 35% 30%,#fff3c8,var(--brass) 45%,var(--brass-lo) 80%);
  box-shadow:0 2px 5px rgba(0,0,0,.8),inset 0 -2px 3px rgba(0,0,0,.4)}
input[type=range]::-moz-range-thumb{width:28px;height:28px;border-radius:50%;border:1px solid #3d2a10;
  background:radial-gradient(circle at 35% 30%,#fff3c8,var(--brass) 45%,var(--brass-lo) 80%);box-shadow:0 2px 5px rgba(0,0,0,.8)}

.sw{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:14px;font-size:.98em}
.sw input{-webkit-appearance:none;appearance:none;flex:none;width:46px;height:24px;margin:0;border-radius:12px;cursor:pointer;position:relative;
  background:#170f09;border:1px solid #6b5228;box-shadow:inset 0 2px 4px rgba(0,0,0,.8);transition:background .2s}
.sw input::after{content:"";position:absolute;top:2px;left:2px;width:18px;height:18px;border-radius:50%;transition:left .2s;
  background:radial-gradient(circle at 35% 30%,#fff3c8,var(--brass) 50%,var(--brass-lo));box-shadow:0 1px 3px rgba(0,0,0,.8)}
.sw input:checked{background:linear-gradient(90deg,#7a3a14,#c0703f)}
.sw input:checked::after{left:24px}

button,.btn{display:block;width:100%;padding:11px 10px;margin-top:14px;border-radius:6px;cursor:pointer;text-align:center;text-decoration:none;
  font:700 .8em var(--head);letter-spacing:.12em;text-transform:uppercase;color:#2a1a08;text-shadow:0 1px 0 rgba(255,240,200,.55);
  border:1px solid #4d3412;background:linear-gradient(180deg,var(--brass-hi),var(--brass) 48%,#a88540 52%,var(--brass-lo));
  box-shadow:inset 0 1px 0 rgba(255,255,255,.55),inset 0 -2px 0 rgba(0,0,0,.25),0 3px 6px rgba(0,0,0,.6)}
button.sec,.btn.sec{color:var(--brass-hi);text-shadow:0 1px 0 #000;border-color:#7a5a26;
  background:linear-gradient(180deg,#4b3423,#2e2016 55%,#251a11);box-shadow:inset 0 1px 0 rgba(243,217,149,.18),0 3px 6px rgba(0,0,0,.6)}
button.danger{color:#ffc2b0;text-shadow:0 1px 0 #000;border-color:#7a2a1e;background:linear-gradient(180deg,#6b2318,#45140e)}
button:active,.btn:active{transform:translateY(1px);box-shadow:inset 0 2px 4px rgba(0,0,0,.5)}
button:disabled{opacity:.5;cursor:default}
.row{display:flex;gap:10px}.row>*{flex:1;min-width:0}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:0 10px}
.pins{display:flex;flex-wrap:wrap;gap:6px;margin-top:6px}
.pins label{margin:0;display:flex;align-items:center;gap:4px;padding:3px 8px;border:1px solid #7a5a26;border-radius:6px;color:var(--ink);font-size:.9em;cursor:pointer}
.scanInfo{margin:14px 0 4px;padding:10px 12px;border:1px solid #7a5a26;border-radius:8px;text-align:center;font-size:1.05em}
.scanInfo b{color:var(--nix);font-size:1.2em}
.val{float:right;color:var(--nix)}
.muted{color:var(--mut);font-size:.88em;font-style:italic;margin-top:10px}

details{margin-top:14px;border-top:1px dashed rgba(201,163,90,.3);padding-top:10px}
summary{cursor:pointer;list-style:none;font:600 .78em var(--head);letter-spacing:.12em;text-transform:uppercase;color:var(--brass)}
summary::-webkit-details-marker{display:none}
summary::before{content:"";display:inline-block;width:12px;height:12px;margin-right:8px;vertical-align:-1px;background:var(--copper);
  -webkit-mask:var(--gear) center/contain no-repeat;mask:var(--gear) center/contain no-repeat;transition:transform .4s}
details[open] summary::before{transform:rotate(90deg)}

.nets{margin-top:10px;max-height:230px;overflow:auto}
.net{display:flex;justify-content:space-between;gap:10px;padding:9px 11px;margin-bottom:5px;border-radius:5px;cursor:pointer;
  background:rgba(0,0,0,.3);border:1px solid rgba(201,163,90,.22)}
.net:hover{border-color:var(--brass)}
.net span:last-child{color:var(--mut);white-space:nowrap}
.kv{display:grid;grid-template-columns:auto 1fr;gap:0 14px;margin:0;font-size:.95em}
.kv dt,.kv dd{padding:5px 0;border-bottom:1px dotted rgba(201,163,90,.25)}
.kv dt{color:var(--mut)}.kv dd{margin:0;text-align:right;word-break:break-word}

#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);max-width:90%;padding:10px 18px;border-radius:5px;opacity:0;pointer-events:none;transition:opacity .25s;
  font:600 .95em var(--body);color:#2a1a08;background:linear-gradient(180deg,var(--brass-hi),var(--brass));border:1px solid #4d3412;box-shadow:0 8px 24px rgba(0,0,0,.7)}
#toast.show{opacity:1}#toast.err{color:#ffd9cc;background:linear-gradient(180deg,#7a2a1e,#4a160f);border-color:#2a0a05}
</style>
</head>
<body>
<div class="wrap">
<header class="top">
  <img src="/favicon.svg" alt="">
  <h1>Digitronové hodiny<small id="topHost">&nbsp;</small></h1>
  <div class="gears" aria-hidden="true">
    <svg class="g1" viewBox="0 0 24 24"><path d="M10.3 1h3.4l.5 2.9 1.8.8 2.4-1.7 2.4 2.4-1.7 2.4.8 1.8 2.9.5v3.4l-2.9.5-.8 1.8 1.7 2.4-2.4 2.4-2.4-1.7-1.8.8-.5 2.9h-3.4l-.5-2.9-1.8-.8-2.4 1.7-2.4-2.4 1.7-2.4-.8-1.8L1 13.7v-3.4l2.9-.5.8-1.8L3 5.6 5.4 3.2l2.4 1.7 1.8-.8zM12 8a4 4 0 100 8 4 4 0 000-8z"/></svg>
    <svg class="g2" viewBox="0 0 24 24"><path d="M10.3 1h3.4l.5 2.9 1.8.8 2.4-1.7 2.4 2.4-1.7 2.4.8 1.8 2.9.5v3.4l-2.9.5-.8 1.8 1.7 2.4-2.4 2.4-2.4-1.7-1.8.8-.5 2.9h-3.4l-.5-2.9-1.8-.8-2.4 1.7-2.4-2.4 1.7-2.4-.8-1.8L1 13.7v-3.4l2.9-.5.8-1.8L3 5.6 5.4 3.2l2.4 1.7 1.8-.8zM12 8a4 4 0 100 8 4 4 0 000-8z"/></svg>
  </div>
</header>

<div class="card">
  <h2><span><span class="ic"></span>Čas</span><small id="hostLbl"></small></h2>
  <div class="tubes off" id="clk"></div>
  <span class="plaque" id="dt">&nbsp;</span>
  <div class="chips" id="chips"></div>

  <div class="dial">
    <div class="lbl">Jas digitronů <b id="brightV"></b></div>
    <input type="range" min="0" max="100" id="bright" aria-label="Jas">
  </div>

  <div class="row">
    <button class="sec" id="bNtp">NTP synchronizace</button>
    <button class="sec" id="bBrowser">Čas ze zařízení</button>
  </div>
  <details>
    <summary>Ruční nastavení</summary>
    <label>Datum a čas (podle tohoto zařízení)</label>
    <input type="datetime-local" id="manDt" step="1">
    <button class="sec" id="bManual">Nastavit</button>
  </details>
</div>

<div class="card">
  <h2><span><span class="ic"></span>Synchronizace</span></h2>
  <label>NTP server</label>
  <input type="text" id="ntp1" list="ntpl" autocapitalize="off" spellcheck="false">
  <label>Záložní NTP server</label>
  <input type="text" id="ntp2" list="ntpl" autocapitalize="off" spellcheck="false">
  <datalist id="ntpl">
    <option value="cz.pool.ntp.org"><option value="tik.cesnet.cz"><option value="tak.cesnet.cz">
    <option value="ntp.nic.cz"><option value="time.cloudflare.com"><option value="time.google.com"><option value="pool.ntp.org">
  </datalist>
  <label>Synchronizovat každých</label>
  <select id="ntpint">
    <option value="60">1 hodinu</option><option value="180">3 hodiny</option><option value="360">6 hodin</option>
    <option value="720">12 hodin</option><option value="1440">24 hodin</option>
  </select>
  <label>Časové pásmo</label>
  <select id="tzSel"></select>
  <div id="tzCustomWrap" hidden>
    <label>POSIX TZ řetězec</label>
    <input type="text" id="tzCustom" autocapitalize="off" spellcheck="false" placeholder="CET-1CEST,M3.5.0,M10.5.0/3">
  </div>
  <button id="bSaveTime">Uložit</button>
  <div class="muted">Hodiny si v RTC drží UTC, letní a zimní čas se přepíná samo podle pásma – i bez internetu.</div>
</div>

<div class="card">
  <h2><span><span class="ic"></span>Displej</span></h2>
  <div class="sw">Skrýt úvodní nulu hodin<input type="checkbox" id="hidezero"></div>
  <div class="sw">Ukázat datum (v :30, jako MM DD)<input type="checkbox" id="showdate"></div>
  <label>Dvojtečka</label>
  <select id="colon">
    <option value="0">Svítí</option><option value="1">Bliká</option><option value="2">Vypnutá</option>
  </select>

  <details>
    <summary>Noční režim</summary>
    <div class="sw">Zapnout noční režim<input type="checkbox" id="nighton"></div>
    <div class="grid2">
      <div><label>Od</label><input type="time" id="nfrom"></div>
      <div><label>Do</label><input type="time" id="nto"></div>
    </div>
    <label>Jas v noci <span class="val" id="nbrightV"></span></label>
    <input type="range" min="0" max="100" id="nbright">
    <div id="hvWrap" hidden>
      <div class="sw">Při jasu 0 % vypnout VN zdroj<input type="checkbox" id="nhvoff"></div>
      <div class="muted">Vypnutý VN zdroj šetří digitrony. Hloubkové čištění si ho na svou dobu zapne.</div>
    </div>
  </details>

  <details>
    <summary>Ochrana katod</summary>
    <label>Rychlé protočení číslic</label>
    <select id="clean">
      <option value="0">Vypnuto</option><option value="1">Každou minutu</option>
      <option value="2">Každých 10 minut</option><option value="3">Každou hodinu</option>
    </select>
    <label>Efekt</label>
    <select id="cleanfx">
      <option value="0">Postupně zleva</option>
      <option value="1">Náhodné rozsvícení</option>
      <option value="2">Hrací automat</option>
      <option value="3">Vlna</option>
      <option value="4">Ob jeden</option>
      <option value="5">Počítadlo</option>
      <option value="6">Šum</option>
      <option value="7">Rozpad</option>
      <option value="8">Ze středu ven</option>
      <option value="9">Do středu</option>
      <option value="10">Náhodný efekt</option>
    </select>
    <button class="sec" id="bClean">Vyzkoušet efekt</button>
    <div class="muted">Každý efekt projede na každém digitronu všech deset číslic.</div>
    <div class="sw">Denní hloubkové čištění<input type="checkbox" id="deepon"></div>
    <div class="grid2">
      <div><label>Začátek (hodina)</label><input type="number" id="deeph" min="0" max="23"></div>
      <div><label>Délka (min)</label><input type="number" id="deepm" min="1" max="30"></div>
    </div>
  </details>

  <details>
    <summary>Přenos do registrů</summary>
    <label>Délka hodinového pulzu</label>
    <select id="srpulse">
      <option value="6">6 µs</option><option value="7">7 µs</option><option value="8">8 µs</option>
      <option value="9">9 µs</option><option value="10">10 µs</option><option value="12">12 µs</option>
      <option value="15">15 µs</option><option value="18">18 µs</option><option value="20">20 µs</option>
      <option value="25">25 µs</option>
    </select>
    <div class="sw">Zhasnout během přenosu<input type="checkbox" id="srblank"></div>
    <div class="muted">Přepíná se okamžitě. Když glitchuje, zkus delší pulz.</div>
  </details>
  <details>
    <summary>Test digitronů</summary>
    <div class="sw">Testovací režim<input type="checkbox" id="tmode"></div>
    <div class="grid2">
      <div><label>1. digitron</label><select id="td0"></select></div>
      <div><label>2. digitron</label><select id="td1"></select></div>
      <div><label>3. digitron</label><select id="td2"></select></div>
      <div><label>4. digitron</label><select id="td3"></select></div>
    </div>
    <div class="sw">Dvojtečka<input type="checkbox" id="tcolon" checked></div>
    <div class="muted">Hodiny místo času ukazují zvolené číslice. Režim se vypne restartem nebo přepínačem.</div>
  </details>
  <button id="bSaveDisp">Uložit</button>
</div>

<div class="card">
  <h2><span><span class="ic"></span>Zapojení desky</span><small id="pinState"></small></h2>
  <label>Typ digitronů (mapování katod)</label>
  <select id="tube"><option value="0">IN-12B</option><option value="1">Z1082M</option></select>
  <div class="grid3">
    <div><label>CLK</label><select id="pclk"></select></div>
    <div><label>DIN</label><select id="pdin"></select></div>
    <div><label>OE (jas)</label><select id="poe"></select></div>
  </div>
  <button id="bPins">Uložit zapojení</button>
  <div class="muted">Projeví se ihned. Typ digitronů poznáš podle testu: zvolená číslice musí svítit správně.</div>
  <details id="scanBox">
    <summary>Hledat zapojení</summary>
    <div class="muted">Hledá se ve třech krocích, u každého stiskni tlačítko, jakmile se na digitronech něco stane:
      1) CLK – rozsvítí se číslice, 2) DIN – ukáže se <b>12:34</b>, 3) OE – displej bliká.
      Vybrané piny jsou během hledání výstupy – odškrtni piny, na kterých je zapojené něco jiného.</div>
    <label>Zkoušet GPIO</label>
    <div class="pins" id="scanPins"></div>
    <label>Doba jedné kombinace</label>
    <select id="scanStep">
      <option value="1500">1,5 s</option><option value="2500" selected>2,5 s</option>
      <option value="4000">4 s</option><option value="6000">6 s</option>
    </select>
    <div class="row">
      <button class="sec" id="bScan1">Hledat zapojení</button>
      <button class="sec" id="bScan2">Hledat jen OE</button>
    </div>
    <div id="scanRun" hidden>
      <div class="scanInfo" id="scanInfo"></div>
      <div class="row">
        <button class="sec" id="bSPrev">◀ Zpět</button>
        <button class="sec" id="bSPause">Pauza</button>
        <button class="sec" id="bSNext">Další ▶</button>
      </div>
      <button id="bSOk">Svítí – použít</button>
      <button class="sec" id="bSNoOe" hidden>OE nenalezeno – uložit bez OE</button>
      <button class="danger" id="bSStop">Ukončit hledání</button>
    </div>
  </details>
</div>

<div class="card">
  <h2><span><span class="ic"></span>WiFi</span><small id="wifiState"></small></h2>
  <button class="sec" id="bScan">Vyhledat sítě</button>
  <div class="nets" id="nets"></div>
  <label>SSID</label>
  <input type="text" id="ssid" autocapitalize="off" spellcheck="false">
  <label>Heslo</label>
  <input type="password" id="pass" placeholder="">
  <button id="bWifi">Uložit a restartovat</button>
  <details>
    <summary>Pokročilé</summary>
    <label>Název zařízení (mDNS: <span id="hostEx">hodiny</span>.local)</label>
    <input type="text" id="host" autocapitalize="off" spellcheck="false">
    <label>Vysílací výkon</label>
    <select id="txpow">
      <option value="34">8,5 dBm</option><option value="44">11 dBm</option><option value="52">13 dBm</option>
      <option value="60">15 dBm</option><option value="68">17 dBm</option><option value="78">19,5 dBm</option>
    </select>
    <div class="muted">Nižší výkon často pomůže stabilitě i rušení displeje. Projeví se ihned, název po restartu.</div>
    <button class="sec" id="bNet">Uložit</button>
  </details>
</div>

<div class="card">
  <h2><span><span class="ic"></span>Systém</span></h2>
  <dl class="kv" id="sys"></dl>
  <a href="/update" class="btn">Aktualizace firmwaru</a>
  <details>
    <summary>Zabezpečení OTA</summary>
    <label>Uživatel</label>
    <input type="text" id="otauser" autocapitalize="off" spellcheck="false">
    <label>Heslo</label>
    <input type="password" id="otapass">
    <div class="muted">Platí pro /update i pro nahrávání z PlatformIO (heslo). Prázdné heslo = beze změny.</div>
    <div class="row">
      <button class="sec" id="bOta">Uložit</button>
      <button class="sec" id="bOtaClr">Zrušit ochranu</button>
    </div>
  </details>
  <div class="row">
    <button class="sec" id="bRestart">Restart</button>
    <button class="danger" id="bFactory">Tovární nastavení</button>
  </div>
</div>

</div>
<div id="toast"></div>

<script>
const $=id=>document.getElementById(id);
const TZ=[
 ["Praha, Bratislava, Berlín, Paříž","CET-1CEST,M3.5.0,M10.5.0/3"],
 ["Londýn, Lisabon","GMT0BST,M3.5.0/1,M10.5.0"],
 ["Helsinky, Kyjev, Atény","EET-2EEST,M3.5.0/3,M10.5.0/4"],
 ["Moskva","MSK-3"],
 ["UTC","UTC0"],
 ["New York","EST5EDT,M3.2.0,M11.1.0"],
 ["Los Angeles","PST8PDT,M3.2.0,M11.1.0"],
 ["Vlastní…",""]
];
const DAYS=["neděle","pondělí","úterý","středa","čtvrtek","pátek","sobota"];
let cfg={}, st={}, tSync=0, tLocal=0;

function toast(m,err){const t=$("toast");t.textContent=m;t.className="show"+(err?" err":"");clearTimeout(t._h);t._h=setTimeout(()=>t.className="",2600)}
async function post(url,data){
  const r=await fetch(url,{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams(data||{})});
  const txt=await r.text();
  if(!r.ok) throw new Error(txt||("HTTP "+r.status));
  return txt;
}
async function act(btn,fn,okMsg){
  btn.disabled=true;
  try{await fn(); if(okMsg) toast(okMsg)}catch(e){toast(e.message||"Chyba sítě",true)}
  finally{btn.disabled=false}
}
const pad=n=>String(n).padStart(2,"0");
const m2t=m=>pad(Math.floor(m/60))+":"+pad(m%60);
const t2m=t=>{const [h,m]=(t||"0:0").split(":").map(Number);return h*60+m};
function ago(s){if(s<60)return "před chvílí";if(s<3600)return "před "+Math.floor(s/60)+" min";if(s<86400)return "před "+Math.floor(s/3600)+" h";return "před "+Math.floor(s/86400)+" d"}
function upt(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+" d ":"")+h+" h "+m+" min"}

// ---------- digitrony ----------
const clk=$("clk"), tubeDigits=[];
(function buildTubes(){
  for(let i=0;i<4;i++){
    if(i===2){const c=document.createElement("div");c.className="colon";c.id="colonEl";c.innerHTML="<i></i><i></i>";clk.appendChild(c)}
    const t=document.createElement("div");t.className="tube";t.innerHTML='<span class="ghost">8</span><span class="d">0</span>';
    clk.appendChild(t);tubeDigits.push(t.lastChild);
  }
})();
let lastClk="";
function setClock(s,colOn){ // s = "HHMM" nebo null
  const v=(s||"----")+(colOn?"1":"0");
  if(v===lastClk) return; lastClk=v;
  clk.classList.toggle("off",!s);
  $("colonEl").classList.toggle("dim",!colOn);
  for(let i=0;i<4;i++) tubeDigits[i].textContent=s?v[i]:"0";
}

// ---------- stav ----------
async function poll(){
  if(document.hidden) return;
  try{
    st=await (await fetch("/api/status")).json();
    tSync=st.epoch*1000+st.ms; tLocal=performance.now();
    render();
  }catch(e){setClock(null)}
}
function render(){
  if(st.valid){
    const d=new Date(tSync+(performance.now()-tLocal)+st.utcoff*1000);
    const cm=cfg.colon|0; setClock(pad(d.getUTCHours())+pad(d.getUTCMinutes()),cm===0||(cm===1&&d.getUTCMilliseconds()<500));
    $("dt").textContent=DAYS[d.getUTCDay()]+" · "+d.getUTCDate()+". "+(d.getUTCMonth()+1)+". "+d.getUTCFullYear();
  }else{setClock(null);$("dt").textContent="čas není nastaven"}
  const c=[];
  const z=st.zone==="CEST"?"letní čas (SELČ)":st.zone==="CET"?"zimní čas (SEČ)":(st.zone+(st.dst?" · letní":""));
  if(st.valid) c.push(["",z]);
  const now=st.epoch;
  if(st.src==="ntp") c.push(["ok","NTP "+ago(now-st.lastSync)]);
  else if(st.src==="manual") c.push(["warn","nastaveno ručně "+ago(now-st.lastSync)]);
  else if(st.src==="rtc") c.push(["warn","z RTC"+(st.lastNtp?" · NTP "+ago(now-st.lastNtp):"")]);
  else c.push(["bad","bez zdroje času"]);
  if(st.hasRtc&&!st.rtc) c.push(["bad","RTC nenalezeno"]);
  if(st.test) c.push(["warn","testovací režim"]);
  if(st.night) c.push(["",st.hv?"noční režim":"noční režim · VN vyp."]);
  if(st.deep) c.push(["","hloubkové čištění"]);
  if(st.ota) c.push(["warn","probíhá OTA"]);
  if(st.scan&&st.scan.phase) c.push(["warn","hledání zapojení"]);
  const ch=c.map(x=>`<span class="chip ${x[0]}">${x[1]}</span>`).join("");
  if($("chips").innerHTML!==ch) $("chips").innerHTML=ch;
  $("hvWrap").hidden=!st.hasHv;$("tmode").checked=!!st.test;$("hostLbl").textContent=st.host+".local";$("topHost").textContent="firmware "+st.ver;
  let w;
  if(st.sta) w=st.ssid+" · "+st.rssi+" dBm";
  else if(st.ap) w="AP „"+st.apSsid+"“";
  else w="nepřipojeno";
  $("wifiState").textContent=w;
  const kv=[["IP adresa",st.sta?st.ip:(st.ap?st.apIp+" (AP)":"–")],["Firmware",st.ver],["Sestaveno",st.build],["Běží",upt(st.uptime)],["Poslední restart",st.reset],
    ["Volná paměť",Math.round(st.heap/1024)+" kB (min "+Math.round(st.heapMin/1024)+" kB)"],
    ...(st.hasRtc?[["Teplota RTC",st.temp!==null?st.temp.toFixed(1)+" °C":"–"]]:[]),["Jas",st.bright+" %"]];
  $("sys").innerHTML=kv.map(x=>`<dt>${x[0]}</dt><dd>${x[1]}</dd>`).join("");
  renderScan();
}
setInterval(()=>{if(st.valid)render()},250);
setInterval(poll,2000);
document.addEventListener("visibilitychange",poll);

// ---------- konfigurace ----------
function fillTz(){
  const s=$("tzSel");s.innerHTML=TZ.map((z,i)=>`<option value="${i}">${z[0]}</option>`).join("");
  let i=TZ.findIndex(z=>z[1]===cfg.tz); if(i<0)i=TZ.length-1;
  s.value=i; $("tzCustom").value=cfg.tz; $("tzCustomWrap").hidden=i!==TZ.length-1;
}
$("tzSel").onchange=()=>{$("tzCustomWrap").hidden=$("tzSel").value!=TZ.length-1};
async function loadCfg(){
  cfg=await (await fetch("/api/config")).json();
  $("ntp1").value=cfg.ntp1;$("ntp2").value=cfg.ntp2;$("ntpint").value=cfg.ntpint;fillTz();
  $("bright").value=cfg.bright;$("brightV").textContent=cfg.bright+" %";
  $("hidezero").checked=cfg.hidezero;$("showdate").checked=cfg.showdate;$("colon").value=cfg.colon;
  $("nighton").checked=cfg.nighton;$("nfrom").value=m2t(cfg.nfrom);$("nto").value=m2t(cfg.nto);
  $("nbright").value=cfg.nbright;$("nbrightV").textContent=cfg.nbright+" %";$("nhvoff").checked=cfg.nhvoff;
  $("clean").value=cfg.clean;$("cleanfx").value=cfg.cleanfx;$("deepon").checked=cfg.deepon;$("deeph").value=cfg.deeph;$("deepm").value=cfg.deepm;
  $("srpulse").value=cfg.srpulse;$("srblank").checked=cfg.srblank;
  fillPins();
  $("ssid").value=cfg.ssid;$("pass").placeholder=cfg.passset?"beze změny":"";
  $("host").value=cfg.host;$("hostEx").textContent=cfg.host;$("txpow").value=cfg.txpow;
  $("otauser").value=cfg.otauser;$("otapass").placeholder=cfg.otapassset?"nastaveno – beze změny":"";
}

// jas – živě a hned uloženo, s omezením počtu požadavků
let bT=0,bPend=null;
function sendBright(v){bPend=v;const n=Date.now();if(n-bT>150){bT=n;post("/api/config",{bright:v}).catch(()=>{});bPend=null}else setTimeout(()=>{if(bPend!==null){const x=bPend;bPend=null;bT=Date.now();post("/api/config",{bright:x}).catch(()=>{})}},160)}
$("bright").oninput=e=>{$("brightV").textContent=e.target.value+" %";sendBright(e.target.value)};
$("nbright").oninput=e=>{$("nbrightV").textContent=e.target.value+" %"};

$("bSaveTime").onclick=e=>act(e.target,async()=>{
  const i=+$("tzSel").value; const tz=i===TZ.length-1?$("tzCustom").value.trim():TZ[i][1];
  await post("/api/config",{ntp1:$("ntp1").value,ntp2:$("ntp2").value,ntpint:$("ntpint").value,tz});
  cfg.tz=tz; setTimeout(poll,300);
},"Uloženo");

$("bSaveDisp").onclick=e=>act(e.target,()=>post("/api/config",{
  hidezero:+$("hidezero").checked,showdate:+$("showdate").checked,colon:$("colon").value,
  nighton:+$("nighton").checked,nfrom:t2m($("nfrom").value),nto:t2m($("nto").value),
  nbright:$("nbright").value,nhvoff:+$("nhvoff").checked,
  clean:$("clean").value,cleanfx:$("cleanfx").value,deepon:+$("deepon").checked,deeph:$("deeph").value,deepm:$("deepm").value,
  srpulse:$("srpulse").value,srblank:+$("srblank").checked
}),"Uloženo");

$("bNtp").onclick=e=>act(e.target,async()=>{
  const before=st.lastNtp; await post("/api/ntpsync"); toast("Dotazuji NTP server…");
  for(let i=0;i<20;i++){await new Promise(r=>setTimeout(r,500)); await poll(); if(st.lastNtp!==before){toast("Synchronizováno z NTP");return}}
  throw new Error("NTP server neodpověděl");
});
$("bBrowser").onclick=e=>act(e.target,async()=>{await post("/api/settime",{ms:Date.now()});setTimeout(poll,300)},"Čas nastaven z tohoto zařízení");
$("bManual").onclick=e=>act(e.target,async()=>{
  const v=$("manDt").value; if(!v) throw new Error("Zadej datum a čas");
  await post("/api/settime",{ms:new Date(v).getTime()}); setTimeout(poll,300);
},"Čas nastaven");
const srSend=()=>post("/api/config",{srpulse:$("srpulse").value,srblank:+$("srblank").checked}).then(()=>toast("Uloženo")).catch(x=>toast(x.message,true));
$("srpulse").onchange=srSend;$("srblank").onchange=srSend;
$("bClean").onclick=e=>act(e.target,()=>post("/api/clean",{fx:$("cleanfx").value}));

$("bScan").onclick=e=>act(e.target,async()=>{
  $("nets").innerHTML='<div class="muted">Hledám…</div>';
  for(let i=0;i<30;i++){
    const r=await (await fetch("/api/scan")).json();
    if(!r.running){
      $("nets").innerHTML=r.list.length?r.list.map(n=>`<div class="net" data-s="${encodeURIComponent(n.ssid)}"><span>${n.ssid.replace(/</g,"&lt;")}${n.open?"":" 🔒"}</span><span>${n.rssi} dBm</span></div>`).join(""):'<div class="muted">Nic nenalezeno</div>';
      document.querySelectorAll(".net").forEach(el=>el.onclick=()=>{$("ssid").value=decodeURIComponent(el.dataset.s);$("pass").value="";$("pass").placeholder="";$("pass").focus()});
      return;
    }
    await new Promise(r=>setTimeout(r,700));
  }
  throw new Error("Vyhledávání trvá příliš dlouho");
});
$("bWifi").onclick=e=>act(e.target,async()=>{
  await post("/api/wifi",{ssid:$("ssid").value,pass:$("pass").value});
  toast("Uloženo, hodiny se restartují…");
});
$("bNet").onclick=e=>act(e.target,()=>post("/api/config",{host:$("host").value.trim().toLowerCase(),txpow:$("txpow").value}),"Uloženo");
$("bOta").onclick=e=>act(e.target,async()=>{
  const d={otauser:$("otauser").value}; if($("otapass").value) d.otapass=$("otapass").value;
  if(d.otauser&&!d.otapass&&!cfg.otapassset) throw new Error("Zadej i heslo");
  await post("/api/config",d); await loadCfg(); $("otapass").value="";
},"Uloženo");
$("bOtaClr").onclick=e=>act(e.target,async()=>{await post("/api/config",{otaclear:1});await loadCfg()},"Ochrana zrušena");
$("bRestart").onclick=e=>{if(confirm("Restartovat hodiny?"))act(e.target,()=>post("/api/restart"),"Restartuji…")};
$("bFactory").onclick=e=>{if(confirm("Smazat všechna nastavení včetně WiFi? Hodiny pak naběhnou jako otevřený AP „hodiny“."))act(e.target,()=>post("/api/factory"),"Mažu a restartuji…")};

// testovací režim
[3,10,6,10].forEach((n,i)=>{const s=$("td"+i);s.innerHTML='<option value="-1">zhasnuto</option>'+[...Array(n).keys()].map(k=>`<option value="${k}">${k}</option>`).join("");s.value="0"});
function sendTest(){post("/api/test",{on:+$("tmode").checked,d1:$("td0").value,d2:$("td1").value,d3:$("td2").value,d4:$("td3").value,colon:+$("tcolon").checked}).catch(x=>toast(x.message,true))}
["tmode","td0","td1","td2","td3","tcolon"].forEach(id=>$(id).onchange=sendTest);
$("colon").onchange=()=>{cfg.colon=+$("colon").value};

// ---------- zapojení desky ----------
const gpioName=g=>"GPIO "+g;
function fillPins(){
  const g=cfg.gpios||[], opt=g.map(x=>`<option value="${x}">${gpioName(x)}</option>`).join("");
  $("pclk").innerHTML=opt;$("pdin").innerHTML=opt;$("poe").innerHTML='<option value="-1">nezapojeno</option>'+opt;
  $("pclk").value=cfg.pclk;$("pdin").value=cfg.pdin;$("poe").value=cfg.poe;$("tube").value=cfg.tube;
  $("pinState").textContent="CLK "+cfg.pclk+" · DIN "+cfg.pdin+" · OE "+(cfg.poe<0?"–":cfg.poe);
  if(!$("scanPins").children.length)
    $("scanPins").innerHTML=g.map(x=>`<label><input type="checkbox" value="${x}" checked>${x}</label>`).join("");
}
$("bPins").onclick=e=>act(e.target,async()=>{
  const c=$("pclk").value,d=$("pdin").value,o=$("poe").value;
  if(c===d||o===c||o===d) throw new Error("Každý signál musí mít jiný pin");
  await post("/api/config",{pclk:c,pdin:d,poe:o,tube:$("tube").value}); await loadCfg();
},"Zapojení uloženo");
$("tube").onchange=()=>post("/api/config",{tube:$("tube").value}).then(()=>{cfg.tube=+$("tube").value;toast("Typ digitronů uložen")}).catch(x=>toast(x.message,true));

const scanPost=(cmd,extra)=>post("/api/pinscan",Object.assign({cmd},extra||{})).then(()=>setTimeout(poll,250));
function startScan(btn,phase){
  const pins=[...document.querySelectorAll("#scanPins input:checked")].map(x=>x.value).join(",");
  act(btn,()=>scanPost("start",{phase,pins,step:$("scanStep").value}));
}
$("bScan1").onclick=e=>startScan(e.target,1);
$("bScan2").onclick=e=>startScan(e.target,4);
const shownIdx=()=>st.scan?st.scan.idx:-1;   // kombinace, kterou uživatel právě vidí
$("bSPrev").onclick=e=>act(e.target,()=>scanPost("prev"));
$("bSNext").onclick=e=>act(e.target,()=>scanPost("next"));
$("bSPause").onclick=e=>act(e.target,()=>scanPost(st.scan.paused?"resume":"pause",{idx:shownIdx()}));
$("bSOk").onclick=e=>act(e.target,async()=>{
  const ph=st.scan.phase; await scanPost("ok",{idx:shownIdx()});
  if(ph===4){await loadCfg();toast("Zapojení nalezeno a uloženo")}
});
$("bSNoOe").onclick=e=>act(e.target,async()=>{await scanPost("nooe");await loadCfg()},"Uloženo bez OE");
$("bSStop").onclick=e=>act(e.target,()=>scanPost("stop"),"Hledání ukončeno");
let scanWas=0;
function renderScan(){
  const sc=st.scan||{phase:0}, on=sc.phase>0;
  $("scanRun").hidden=!on;$("bScan1").disabled=on;$("bScan2").disabled=on;
  if(on){
    $("scanBox").open=true;
    const step=`${sc.idx+1} / ${sc.count}${sc.paused?" · pauza":""}`;
    const P=[null,
      [`zkouším <b>CLK ${sc.clk}</b>`,"Svítí nějaké číslice?","Svítí – CLK nalezen"],
      [`CLK ${sc.clk} · zkouším <b>DIN ${sc.din1}</b>`,"Ukazují digitrony 12:34?","Ukazuje 12:34 – DIN nalezen"],
      null,
      [`CLK ${sc.clk} · DIN ${sc.din1} · zkouším <b>OE ${sc.oe}</b>`,"Bliká displej?","Bliká – OE nalezeno"]][sc.phase]||["","",""];
    $("scanInfo").innerHTML=`${P[0]}<br><small>${step} – ${P[1]}</small>`;
    $("bSPause").textContent=sc.paused?"Pokračovat":"Pauza";
    $("bSOk").textContent=P[2];
    $("bSNoOe").hidden=sc.phase!==4;
  }else if(scanWas) loadCfg().catch(()=>{});
  scanWas=sc.phase;
}
setInterval(()=>{if(st.scan&&st.scan.phase)poll()},600);   // během hledání častěji

setClock(null);
loadCfg().catch(()=>toast("Nepodařilo se načíst nastavení",true));
poll();
</script>
</body>
</html>
)HTML";
