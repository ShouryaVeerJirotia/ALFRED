// ============================================================================
// ALFRED AI  -  AlfredsFirmware_v2.ino
// Target : AI Thinker ESP32-CAM (OV2640), ESP32 Arduino core 3.3.10
// STATUS : Compiled and flash-verified 2026-11-07. See docs/STATUS.md and
//          docs/TEST_CHECKLIST.md; runtime behavior still needs hardware checks.
// ============================================================================
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DHT.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==========================================
// NETWORK CONFIGURATION
// ==========================================
// Preferred: leave these EMPTY and use the on-device setup page (Network tab).
// Credentials saved from the setup page live in NVS flash and are never sent back to the browser.
// If you fill these in (same behaviour as v1), they act as a FALLBACK network and use the static IP below.
#define FALLBACK_SSID   ""
#define FALLBACK_PASS   ""
#define USE_STATIC_IP   1      // applies ONLY to the FALLBACK network; setup-page networks use DHCP

IPAddress localIP(192, 168, 1, 13);
IPAddress gatewayIP(192, 168, 1, 1);
IPAddress subnetMask(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// Recovery AP password is derived per board from the ESP32 eFuse MAC, never shared across devices.
#define SETUP_AP_SSID   "ALFRED-Setup"
char setupApPass[13] = {0};

#define CONNECT_TIMEOUT_MS  15000UL   // per candidate network
#define AP_RETRY_MS         60000UL   // retry saved networks every 60 s while in setup mode (only if nobody is on the AP)

// Original v1 disabled the brown-out detector. Kept for behaviour parity, but it hides power problems.
// Set to 0 once you have a solid 5 V / 2 A supply.
#define DISABLE_BROWNOUT    1

WebServer server(80);

// ==========================================
// ESP32-CAM AI-THINKER HARDWARE PINS (UNCHANGED FROM v1)
// ==========================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==========================================
// SENSOR IO PIN MAPPINGS (UNCHANGED FROM v1)
// ==========================================
#define DHTPIN            15   // DHT11 data
#define DHTTYPE           DHT11
#define TRIG_PIN          12   // HC-SR04 trigger
#define ECHO_PIN          13   // HC-SR04 echo (needs a 5V->3.3V divider; see audit)
#define FLAME_PIN         14   // IR flame sensor digital out (LOW = flame)

// ==========================================
// TELEMETRY (written by sensor task, read by HTTP handlers)
// ==========================================
struct Telemetry {
  float t, h, d;
  uint32_t tMs, hMs, dMs;   // millis() of last valid reading (0 = never)
  bool flame;
};
SemaphoreHandle_t telemetryMutex;
DHT dht(DHTPIN, DHTTYPE);
Telemetry tel = {0, 0, 0, 0, 0, 0, false};
bool cameraOK = false;
bool mdnsStarted = false;

#define TEL_FRESH_MS 15000UL

// ==========================================
// DASHBOARD (PROGMEM)
// ==========================================
const char HTML_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta http-equiv="Content-Security-Policy" content="default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self' https://*.supabase.co https://generativelanguage.googleapis.com">
<meta name="theme-color" content="#05070d">
<title>ALFRED</title>
<style>
:root{--bg:#05070d;--fg:#e8f1ff;--mut:#8aa0bd;--line:rgba(120,160,255,.18);--c:#22d3ee;--b:#3b82f6;--v:#8b5cf6;--am:#fbbf24;--rd:#f43f5e;--ok:#34d399;--pnl:rgba(14,20,36,.72)}
*{box-sizing:border-box}
[hidden]{display:none!important}
html,body{margin:0;height:100%}
body{background:radial-gradient(1100px 650px at 50% -8%,#10204a 0,var(--bg) 62%) no-repeat var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-tap-highlight-color:transparent}
#app{max-width:880px;margin:0 auto;height:100%;display:flex;flex-direction:column;padding:env(safe-area-inset-top) 14px 0}
header{display:flex;align-items:center;justify-content:space-between;padding:12px 2px 4px}
.brand{font-weight:700;letter-spacing:.34em;font-size:1.05rem;background:linear-gradient(90deg,var(--c),var(--b),var(--v));-webkit-background-clip:text;background-clip:text;color:transparent}
.pill{font-size:.68rem;letter-spacing:.14em;border:1px solid var(--line);border-radius:999px;padding:4px 10px;color:var(--mut)}
.pill.on{color:var(--ok);border-color:rgba(52,211,153,.5)}
.pill.off{color:var(--am);border-color:rgba(251,191,36,.6)}
main{flex:1;overflow-y:auto;padding:6px 0 16px}
.view{display:none}.view.on{display:block}
h2{font-size:.78rem;letter-spacing:.2em;text-transform:uppercase;color:var(--mut);font-weight:600;margin:14px 2px 8px}
.stage{display:flex;flex-direction:column;align-items:center}
#orbBox{width:min(80vw,44vh,400px);aspect-ratio:1;cursor:pointer;border-radius:50%;outline-offset:6px}
#orb{width:100%;height:100%;display:block}
.st{font-size:.74rem;letter-spacing:.22em;color:var(--c);margin-top:2px;display:flex;align-items:center;gap:8px}
.st::before{content:"";width:8px;height:8px;border-radius:50%;background:currentColor;box-shadow:0 0 10px currentColor}
body[data-state=thinking] .st::before,body[data-state=listening] .st::before{animation:bl 1s infinite}
body[data-state=speaking] .st{color:var(--v)}
@keyframes bl{50%{opacity:.25}}
.cap{min-height:3.4em;text-align:center;max-width:34em;color:var(--mut);margin:8px 0 2px;font-size:.92rem}
.cap .ai{color:var(--fg);font-size:1.02rem;margin-top:2px}
.alert{width:100%;border-radius:12px;padding:10px 12px;margin:8px 0;font-weight:600;font-size:.9rem}
.l1{background:rgba(251,191,36,.12);border:1px solid var(--am);color:var(--am)}
.l2{background:rgba(244,63,94,.14);border:1px solid var(--rd);color:#ffa3b5}
.bar{width:100%;display:flex;gap:6px;align-items:center;background:var(--pnl);border:1px solid var(--line);border-radius:999px;padding:5px;margin:10px 0}
.bar input{flex:1;min-width:0;background:none;border:0;color:var(--fg);font:inherit;padding:8px 12px;outline:none}
button{font:inherit;color:inherit;cursor:pointer}
.ib{width:44px;height:44px;border-radius:50%;border:1px solid var(--line);background:rgba(34,211,238,.08);color:var(--c);font-size:1.1rem;flex:none}
.ib[aria-pressed=true]{background:rgba(34,211,238,.25);border-color:var(--c);box-shadow:0 0 0 0 rgba(34,211,238,.6);animation:pu 1.6s infinite}
@keyframes pu{70%{box-shadow:0 0 0 12px rgba(34,211,238,0)}}
.go{height:44px;padding:0 18px;border-radius:999px;border:0;background:linear-gradient(135deg,var(--b),var(--v));color:#fff;font-weight:600;flex:none}
.btn{background:rgba(59,130,246,.16);border:1px solid rgba(59,130,246,.55);border-radius:10px;padding:10px 14px;min-height:44px;color:var(--fg)}
.btn:disabled,.go:disabled{opacity:.5;cursor:default}
.btn.warn{border-color:rgba(244,63,94,.6);background:rgba(244,63,94,.12)}
button:focus-visible,input:focus-visible,textarea:focus-visible,#orbBox:focus-visible,summary:focus-visible{outline:2px solid var(--c)}
.card{background:var(--pnl);border:1px solid var(--line);border-radius:16px;padding:14px;margin-bottom:10px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.lbl{font-size:.68rem;letter-spacing:.16em;text-transform:uppercase;color:var(--mut)}
.val{font-size:1.5rem;font-weight:600;margin:2px 0}
.sub{font-size:.8rem;color:var(--mut)}
.crit{border-color:var(--rd)}.crit .val{color:#ff9fb2}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
.row input{flex:1;min-width:120px}
input[type=text],input[type=tel],input[type=password],textarea{background:rgba(5,7,13,.7);border:1px solid var(--line);border-radius:10px;color:var(--fg);font:inherit;padding:10px 12px;width:100%}
select.settingSelect{flex:1;min-height:44px;background:#05070d;color:var(--fg);border:1px solid var(--line);border-radius:10px;padding:0 10px;font:inherit}
textarea{height:84px;resize:vertical}
summary{cursor:pointer;font-size:.8rem;letter-spacing:.14em;text-transform:uppercase;color:var(--mut)}
#log{max-height:130px;overflow-y:auto;font:12px/1.5 ui-monospace,Menlo,Consolas,monospace;color:#9fdcec;margin-top:8px}
.sources{width:100%;max-width:40em;margin:6px auto 10px;padding:8px 12px;border:1px solid var(--line);border-radius:10px;text-align:left;font-size:.82rem}
.sources strong{display:block;color:var(--mut);margin-bottom:4px}
.sources ul{margin:0;padding-left:1.2em}.sources li{margin:3px 0}.sources a{color:var(--c);overflow-wrap:anywhere}
.camwrap{position:relative;aspect-ratio:4/3;background:#000;border-radius:14px;overflow:hidden;border:1px solid var(--line)}
.camwrap img{width:100%;height:100%;object-fit:contain;display:block}
#camMsg{position:absolute;inset:auto 0 0 0;background:rgba(5,7,13,.8);padding:8px 12px;font-size:.85rem;color:var(--am)}
.net{display:block;width:100%;text-align:left;background:none;border:1px solid var(--line);border-radius:10px;padding:10px 12px;margin:6px 0;min-height:44px}
.net:hover{border-color:var(--c)}
label.chk{display:flex;gap:10px;align-items:center;margin:10px 0;font-size:.92rem}
label.chk input{width:20px;height:20px}
.note{font-size:.8rem;color:var(--mut);margin:6px 0}
#toast{position:fixed;left:50%;bottom:76px;transform:translate(-50%,20px);background:#10172a;border:1px solid var(--line);color:var(--fg);padding:10px 16px;border-radius:12px;opacity:0;pointer-events:none;transition:.25s;max-width:90vw;font-size:.88rem;z-index:9}
#toast.on{opacity:1;transform:translate(-50%,0)}
nav{display:flex;border-top:1px solid var(--line);margin:0 -14px;padding-bottom:env(safe-area-inset-bottom);background:rgba(5,7,13,.94)}
nav button{flex:1;background:none;border:0;color:var(--mut);padding:12px 2px;min-height:48px;font-size:.7rem;letter-spacing:.08em;text-transform:uppercase}
nav button[aria-current=page]{color:var(--c);box-shadow:inset 0 2px var(--c)}
@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style>
</head>
<body data-state="idle">
<div id="app">
<header><div class="brand">ALFRED</div><div class="pill" id="conn" role="status">CONNECTING</div></header>
<main>

<section class="view on" id="v-assistant">
 <div class="stage">
  <div id="orbBox" role="button" tabindex="0" aria-label="Tap to speak to ALFRED"><canvas id="orb" aria-hidden="true"></canvas></div>
  <div class="st" id="state" role="status" aria-live="polite">IDLE</div>
  <div class="cap" id="cap" aria-live="polite">Type a request, tap the orb, or press the mic and speak naturally.</div>
  <div class="sources" id="sources" aria-label="Web sources" hidden></div>
  <div class="alert" id="alert" role="alert" hidden></div>
  <div class="card" id="confirm" hidden style="width:100%">
   <div id="cfText"></div>
   <div class="row"><button class="btn" id="cfOk">Allow</button><button class="btn warn" id="cfNo">Cancel</button></div>
  </div>
  <div class="bar">
   <input type="text" id="cmd" maxlength="500" placeholder="Ask ALFRED…" aria-label="Message to ALFRED" autocomplete="off">
   <button class="ib" id="mic" aria-pressed="false" aria-label="Start voice listening" title="Tap to start listening; tap again to stop">🎙</button>
   <button class="go" id="send">Send</button>
  </div>
 </div>
 <details class="card"><summary>Phone actions</summary>
  <div class="row"><input type="text" id="musicTrack" maxlength="120" placeholder="Track or artist" aria-label="Track or artist"><select id="musicApp" class="settingSelect" aria-label="Music app"><option value="any">Any app</option><option value="spotify">Spotify</option><option value="ytmusic">YouTube Music</option></select><button class="btn" id="bMusic">Play</button></div>
  <div class="row"><input type="text" id="navDest" maxlength="120" placeholder="Destination" aria-label="Destination"><button class="btn" id="bNav">Drive</button></div>
  <div class="row"><input type="tel" id="phone" maxlength="20" placeholder="Phone number or choose contact" aria-label="Phone number"><button class="btn" id="bContact">Contacts</button></div>
  <div class="row"><input type="text" id="smsBody" maxlength="300" placeholder="SMS text" aria-label="SMS text"></div>
  <div class="row"><button class="btn" id="bCall">Call</button><button class="btn" id="bSms">Send text</button></div>
 </details>
 <details class="card"><summary>Code projects (Codex on this computer)</summary>
  <p class="note">Connect the desktop companion to let Codex edit a separate copy of a selected project. ALFRED shows you the patch; it is applied to the project only after you choose “Apply patch”. This works in the computer browser running the companion.</p>
  <div class="sub" id="codexState" role="status">Checking for the desktop companion…</div>
  <textarea id="codexTask" maxlength="1200" placeholder="Describe a code change for the selected project" aria-label="Coding task for Codex"></textarea>
  <div class="row"><button class="btn" id="codexRun">Send coding task to Codex</button><button class="btn warn" id="codexApply" hidden>Apply patch to project</button><button class="btn" id="codexDiscard" hidden>Discard patch</button></div>
  <div class="sub" id="codexResult" aria-live="polite"></div><pre id="codexDiff" class="sub" hidden style="white-space:pre-wrap;overflow:auto;max-height:28rem"></pre>
 </details>
 <details class="card"><summary>Activity log</summary><div id="log"></div></details>
</section>

<section class="view" id="v-vision">
 <h2>Vision</h2>
 <div class="camwrap"><img id="cam" alt="Live camera frame"><div id="camMsg">Connecting to camera…</div></div>
 <div class="row"><button class="btn" id="visBtn">Describe scene</button><button class="btn" id="readTextBtn">Read visible text</button></div>
 <div class="row"><input type="text" id="objectQuery" maxlength="100" placeholder="Object to look for" aria-label="Object to look for"><button class="btn" id="findObjectBtn">Find in frame</button></div>
 <p class="note">Frames are 320×240. A frame is sent to Gemini only when you ask a vision question or use one of these actions. ALFRED describes the image; it cannot give safe movement or distance guidance. OCR may miss small or unclear text, so check critical details.</p>
</section>

<section class="view" id="v-sensors">
 <h2>Sensors</h2>
 <div class="grid">
  <div class="card"><div class="lbl">Distance</div><div class="val" id="vD">Loading…</div><div class="sub" id="sD">HC-SR04</div></div>
  <div class="card"><div class="lbl">Temperature</div><div class="val" id="vT">Loading…</div><div class="sub" id="sT">DHT11</div></div>
  <div class="card"><div class="lbl">Humidity</div><div class="val" id="vH">Loading…</div><div class="sub" id="sH">DHT11</div></div>
  <div class="card" id="cF"><div class="lbl">Flame</div><div class="val" id="vF">Loading…</div><div class="sub" id="sF">IR flame sensor</div></div>
 </div>
 <p class="note">The IR flame sensor is a hobby-grade detector and is not a safety device.</p>
</section>

<section class="view" id="v-network">
 <h2>Device</h2>
 <div class="card">
  <div class="lbl">Status</div><div class="val" id="nMode" style="font-size:1.1rem">—</div>
  <div class="sub">Network: <span id="nSsid">—</span> · IP: <span id="nIp">—</span> · Signal: <span id="nRssi">—</span></div>
  <div class="sub">Uptime: <span id="nUp">—</span> · Free memory: <span id="nHeap">—</span> · Camera: <span id="nCam">—</span></div>
 </div>
 <h2>Wi-Fi setup</h2>
 <div class="card">
  <div class="row"><button class="btn" id="scanBtn">Scan for networks</button><span class="sub" id="scanMsg"></span></div>
  <div id="nets"></div>
  <div class="row"><input type="text" id="ssid" maxlength="32" placeholder="Network name (2.4 GHz)" aria-label="Network name" autocomplete="off"></div>
  <div class="row"><input type="password" id="pass" maxlength="63" placeholder="Password (empty for open networks)" aria-label="Wi-Fi password" autocomplete="off"></div>
  <div class="row"><button class="btn" id="connBtn">Connect and save</button><button class="btn warn" id="forgetBtn">Forget saved Wi-Fi</button></div>
  <div class="sub" id="netMsg" role="status"></div>
  <p class="note">The password is sent to the device over plain HTTP. Do this on a network you trust. It is never shown back to you.</p>
 </div>
</section>

<section class="view" id="v-settings">
 <h2>Cloud assistant &amp; memory</h2>
 <div class="card">
  <p class="note">ALFRED-Showcase is pre-connected. Create or sign in to the same account on each device to use cloud chat and private saved memory. Shared Gemini keys rotate on the server; your own keys can be added as a fallback.</p>
  <div class="row"><input type="url" id="sbUrl" placeholder="Supabase project URL" aria-label="Supabase project URL" autocomplete="url"></div>
  <div class="row"><input type="password" id="sbAnon" placeholder="Supabase publishable or anon key" aria-label="Supabase public key" autocomplete="off"></div>
  <div class="row"><input type="email" id="sbEmail" placeholder="Account email" aria-label="Account email" autocomplete="username"></div>
  <div class="row"><input type="password" id="sbPassword" placeholder="Account password" aria-label="Account password" autocomplete="current-password"></div>
  <div class="row"><button class="btn" id="cloudSignIn">Sign in</button><button class="btn" id="cloudSignUp">Create account</button><button class="btn warn" id="cloudSignOut">Sign out</button></div>
  <div class="sub" id="cloudState" role="status">Cloud memory is not configured.</div>
  <div class="row"><button class="btn" id="memoryReview">Review saved memories</button><button class="btn warn" id="memoryClear">Clear cloud history &amp; memory</button></div>
  <div id="memoryList" class="sub" aria-live="polite"></div>
 </div>
 <h2>Gemini API keys</h2>
 <div class="card">
  <textarea id="keysArea" placeholder="Paste Gemini API key(s), one per line" aria-label="Gemini API keys" autocomplete="off" spellcheck="false"></textarea>
  <label class="chk"><input type="checkbox" id="remember"> Keep keys in this browser</label>
  <p class="note">When enabled, your own keys are saved in this browser profile’s local storage; ALFRED does not encrypt them. In Cloud mode, keys are sent over HTTPS for that request and are not saved by ALFRED's server. Shared rotation is handled by Supabase. Never commit a key to firmware or GitHub.</p>
  <div class="row"><button class="btn" id="saveKeys">Save and engage</button><button class="btn warn" id="clearKeys">Flush keys</button></div>
  <div class="sub" id="keysInfo">No key loaded.</div>
 </div>
 <h2>Behaviour</h2>
 <div class="card">
  <div class="row"><label for="voiceLanguage" class="sub">Voice language</label><select id="voiceLanguage" class="settingSelect" aria-label="Voice language"><option value="auto">Auto (Hindi/English)</option><option value="en-IN">English (India)</option><option value="hi-IN">Hindi</option></select></div>
  <p class="note">Auto follows Hindi device locales and uses English (India) for other locales. Speech support and installed voices vary by browser and phone. Text chat can still reply in the language you use.</p>
  <label class="chk"><input type="checkbox" id="voiceChk" checked> Speak replies aloud</label>
  <label class="chk"><input type="checkbox" id="neuralVoiceChk" checked disabled> Use the device voice for faster replies</label>
  <p class="note">Spoken replies use Android or the browser's installed Hindi/English voice so the answer can start without waiting for a second cloud request. Voice quality varies with the voice data installed on each device.</p>
  <button class="btn" id="handsFreeBtn" type="button">Start hands-free listening (Android app)</button>
  <p class="note">In the Android app, hands-free listening starts after you grant microphone and notification access and stays active with an ongoing Stop notification, including when ALFRED is minimized. After “Alfred,” the service listens for your command; if the app is closed, it queues the command for processing when you reopen ALFRED. Android may stop the service if you force-stop the app or revoke permission.</p>
  <label class="chk"><input type="checkbox" id="visChk" checked> Attach a camera frame when a request sounds visual</label>
  <label class="chk"><input type="checkbox" id="srchChk" checked> Use Google Search for current questions</label>
  <p class="note">Enabled by default for current questions. In signed-in cloud mode, if Gemini Search is unavailable or out of quota, ALFRED can summarize live search excerpts and show source links; if summarization also fails, it offers a live search link. Gemini Search grounding may require an eligible paid-tier project.</p>
  <div class="row"><label for="modelSel" class="sub">Model</label><select id="modelSel" style="flex:1;min-height:44px;background:#05070d;color:var(--fg);border:1px solid var(--line);border-radius:10px;padding:0 10px">
   <option value="gemini-3.5-flash-lite">Gemini 3.5 Flash-Lite (default)</option>
   <option value="gemini-3.6-flash">Gemini 3.6 Flash</option>
   <option value="gemini-3.8-flash">Gemini 3.8 Flash</option></select></div>
  <p class="note">The orb reflects assistant state. Its speaking motion is simulated and pulses on speech word events when the browser provides them. It is not true audio analysis.</p>
 </div>
</section>

</main>
<nav aria-label="Sections">
 <button id="t-assistant" aria-current="page">Assistant</button>
 <button id="t-vision">Vision</button>
 <button id="t-sensors">Sensors</button>
 <button id="t-network">Network</button>
 <button id="t-settings">Settings</button>
</nav>
</div>
<div id="toast" role="status" aria-live="polite"></div>

<script>
(()=>{'use strict';
const $=id=>document.getElementById(id);
const API='https://generativelanguage.googleapis.com/v1beta/models/';
const ALFRED_SUPABASE_URL='https://rviwomejtiiezezofmpn.supabase.co';
const ALFRED_SUPABASE_PUBLIC_KEY='sb_publishable_gtNnN20kA6QxILW6_n35jg_7vCPKWAU';
const MODELS=['gemini-3.5-flash-lite','gemini-3.6-flash','gemini-3.8-flash'];
let model=MODELS[0],useSearch=false;
const SR=window.SpeechRecognition||window.webkitSpeechRecognition;
const ls={g:k=>{try{return localStorage.getItem(k);}catch(e){return null;}},s:(k,v)=>{try{localStorage.setItem(k,v);}catch(e){}},d:k=>{try{localStorage.removeItem(k);}catch(e){}}};
const br=f=>{const a=window.AlfredAndroid;return a&&typeof a[f]==='function'?a:null;};
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let keys=[],ki=0,hist=[],busy=false,reqId=0,voiceOn=true,neuralVoice=true,autoVis=true,voiceLanguage='en-IN',speaking=false,spTok=0,greeted=false,cur='assistant',last={},activeAudio=null;
let rec=null,wantListen=false,awake=false,awT=0,rsT=0,recFail=0,recRun=false,nativeTapPending=false,pendingLocationPrompt='',cfT=0,tt=0;
let codexJob=null,codexPoll=0;
let pollT=0,pollBusy=false,fails=0,camOn=false,camT=0,camBusy=false,camFail=0,scanning=false,camBad=false;

/* ---------- Orb (Canvas 2D) ---------- */
const Orb=(()=>{
const c=$('orb'),x=c.getContext('2d'),RM=matchMedia('(prefers-reduced-motion: reduce)').matches;
let W=1,H=1,P=[],E=[],X,Y,Z,raf=0,lt=0,t=0,slow=0,lowq=false,st='idle',md=0,pl=0,on=true,rt=0;
const T={idle:[.25,.05,1],listening:[.75,.1,1.04],thinking:[1.7,.035,.8],speaking:[.5,.07,1]};
const cu={sp:.25,am:.05,sc:1};
function size(){const d=Math.min(window.devicePixelRatio||1,lowq?1:2),r=c.getBoundingClientRect();W=c.width=Math.max(1,Math.round(r.width*d));H=c.height=Math.max(1,Math.round(r.height*d));}
function build(){const n=lowq?70:110,g=Math.PI*(3-Math.sqrt(5));P=[];E=[];
 for(let i=0;i<n;i++){const y=1-2*(i+.5)/n,r=Math.sqrt(1-y*y),a=g*i;P.push([Math.cos(a)*r,y,Math.sin(a)*r]);}
 for(let i=0;i<n;i++){const d=[];for(let j=0;j<n;j++)if(j!==i){const a=P[i],b=P[j];d.push([(a[0]-b[0])**2+(a[1]-b[1])**2+(a[2]-b[2])**2,j]);}
  d.sort((a,b)=>a[0]-b[0]);for(let k=0;k<4;k++)if(i<d[k][1])E.push([i,d[k][1]]);}
 X=new Float32Array(n);Y=new Float32Array(n);Z=new Float32Array(n);}
function col(y,a){const h=md===2?350+y*8:md===1?38+y*10:190+(y+1)*42;return 'hsla('+h.toFixed(0)+',95%,62%,'+a.toFixed(2)+')';}
function draw(){
 const s=T[st]||T.idle;let am=s[1];
 if(st==='speaking')am=.04+.08*Math.abs(Math.sin(t*9)*Math.sin(t*4.3))+pl*.1;else am+=pl*.08;
 if(md)am+=.03*md;
 cu.sp+=(s[0]*(RM?.4:1)*(md?1.4:1)-cu.sp)*.07;cu.am+=(am-cu.am)*.15;cu.sc+=(s[2]-cu.sc)*.07;pl*=.92;
 const cx=W/2,cy=H/2,R=Math.min(W,H)*.34*cu.sc*(1+.03*Math.sin(t*1.2));
 x.clearRect(0,0,W,H);
 const an=t*cu.sp*.6,ca=Math.cos(an),sa=Math.sin(an),ct=Math.cos(.35),sn=Math.sin(.35);
 for(let i=0;i<P.length;i++){const p=P[i];
  const w=1+cu.am*(Math.sin(p[1]*4+t*cu.sp*3)+Math.sin(p[0]*3-t*cu.sp*2.2)+Math.sin(p[2]*5+t*cu.sp*1.7));
  const px=p[0]*w,py=p[1]*w,pz=p[2]*w;
  const rx=px*ca+pz*sa,rz=-px*sa+pz*ca,ry=py*ct-rz*sn,z2=py*sn+rz*ct,f=1/(1.8-z2*.5);
  X[i]=cx+rx*R*f*1.8;Y[i]=cy+ry*R*f*1.8;Z[i]=z2;}
 x.globalCompositeOperation='lighter';
 const hb=md===2?355:md===1?40:225,g=x.createRadialGradient(cx,cy,R*.1,cx,cy,R*1.6);
 g.addColorStop(0,'hsla('+hb+',90%,60%,.22)');g.addColorStop(1,'hsla('+hb+',90%,60%,0)');x.fillStyle=g;x.fillRect(0,0,W,H);
 const lw=Math.max(1,W/420);x.lineWidth=lw;
 for(let e=0;e<E.length;e++){if(lowq&&(e&1))continue;const a=E[e][0],b=E[e][1],al=.1+.4*((Z[a]+Z[b])/2+1)/2;
  x.strokeStyle=col(P[a][1],al);x.beginPath();x.moveTo(X[a],Y[a]);x.lineTo(X[b],Y[b]);x.stroke();}
 for(let i=0;i<P.length;i++){x.fillStyle=col(P[i][1],.35+.6*(Z[i]+1)/2);x.beginPath();x.arc(X[i],Y[i],lw*(1+(Z[i]+1)*.7),0,6.283);x.fill();}
 if(st==='thinking'){x.strokeStyle='hsla(200,100%,70%,.6)';x.lineWidth=lw*2;
  x.beginPath();x.arc(cx,cy,R*1.3,t*3,t*3+1.6);x.stroke();x.beginPath();x.arc(cx,cy,R*1.3,t*3+3.14,t*3+4.7);x.stroke();}
 x.globalCompositeOperation='source-over';}
function frame(ts){raf=requestAnimationFrame(frame);const dt=ts-lt;if(dt<(lowq?32:15))return;lt=ts;
 if(dt>45){if(++slow>25&&!lowq){lowq=true;build();size();}}else if(slow>0)slow--;
 t+=Math.min(dt,60)/1000;draw();}
function start(){if(on&&!raf&&!document.hidden){lt=performance.now();raf=requestAnimationFrame(frame);}}
function stop(){cancelAnimationFrame(raf);raf=0;}
document.addEventListener('visibilitychange',()=>{document.hidden?stop():start();});
window.addEventListener('resize',()=>{clearTimeout(rt);rt=setTimeout(size,150);});
build();size();start();
return{set(s){st=s;},mood(m){md=m;},pulse(){pl=1;},run(b){on=b;if(b){size();start();}else stop();}};
})();

/* ---------- UI helpers ---------- */
function log(m){const b=$('log'),d=document.createElement('div');d.textContent=m;b.appendChild(d);while(b.childElementCount>60)b.removeChild(b.firstChild);b.scrollTop=b.scrollHeight;}
function toast(m){const e=$('toast');e.textContent=m;e.classList.add('on');clearTimeout(tt);tt=setTimeout(()=>e.classList.remove('on'),4500);}
function put(id,v){$(id).textContent=v;}
function caption(u,a){const c=$('cap');c.textContent='';if(u){const p=document.createElement('div');p.textContent='You: '+u;c.appendChild(p);}if(a){const p=document.createElement('div');p.className='ai';p.textContent=a;c.appendChild(p);}}
function renderSources(list){const box=$('sources');box.textContent='';const safe=[];const seen=new Set();
 (Array.isArray(list)?list:[]).slice(0,8).forEach(s=>{try{const u=new URL(s.url);if(u.protocol!=='https:'||seen.has(u.href))return;seen.add(u.href);safe.push({url:u.href,title:String(s.title||u.hostname).slice(0,160)});}catch(e){}});
 if(!safe.length){box.hidden=true;return;}box.hidden=false;const title=document.createElement('strong');title.textContent='Web sources';box.appendChild(title);const ul=document.createElement('ul');
 safe.slice(0,5).forEach(s=>{const li=document.createElement('li'),a=document.createElement('a');a.href=s.url;a.target='_blank';a.rel='noopener noreferrer';a.textContent=s.title;li.appendChild(a);ul.appendChild(li);});box.appendChild(ul);}
const LBL={idle:'IDLE',standby:'STANDBY',listening:'LISTENING · SPEAK NATURALLY',thinking:'THINKING',speaking:'SPEAKING'};
function setState(s){put('state',LBL[s]);document.body.dataset.state=s==='standby'?'idle':s;Orb.set(s==='standby'?'idle':s);}
const AL={};
function alertSet(k,l,m){AL[k]=[l,m];alertRender();}
function alertClear(k){if(AL[k]){delete AL[k];alertRender();}}
function alertRender(){let b=null;for(const k in AL){if(!b||AL[k][0]>b[0])b=AL[k];}
 const e=$('alert');if(!b){e.hidden=true;Orb.mood(0);return;}
 e.hidden=false;e.className='alert l'+b[0];e.textContent=(b[0]>1?'CRITICAL: ':'WARNING: ')+b[1];Orb.mood(b[0]);}
function go(v){['assistant','vision','sensors','network','settings'].forEach(n=>{$('v-'+n).classList.toggle('on',n===v);$('t-'+n).setAttribute('aria-current',n===v?'page':'false');});
 cur=v;Orb.run(v==='assistant');if(v==='vision')camStart();else camStop();if(v==='network')netRefresh();}
function setConn(ok){const e=$('conn');e.textContent=ok?'ONLINE':'OFFLINE';e.className='pill '+(ok?'on':'off');
 if(ok)alertClear('dev');else alertSet('dev',1,'ALFRED device is not responding. Retrying…');}

/* ---------- Telemetry ---------- */
const MODE={sta:'Connected to Wi-Fi',ap:'Setup network “ALFRED-Setup” active',connecting:'Connecting to Wi-Fi…'};
function fmt(v,u,f){return typeof v==='number'?v.toFixed(f)+u:null;}
function dur(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h+'h '+m+'m';}
function render(d){last=d;setConn(true);
 put('vT',fmt(d.temperature,' °C',1)||'—');put('sT',d.temperature!=null?'Reading OK':'No recent reading');
 put('vH',fmt(d.humidity,' %',0)||'—');put('sH',d.humidity!=null?'Reading OK':'No recent reading');
 put('vD',fmt(d.distance,' cm',0)||'—');put('sD',d.distance!=null?'Echo received':'No echo (out of range or not wired)');
 put('vF',d.flame?'DETECTED':'None');put('sF',d.flame?'IR sensor triggered':'Clear');$('cF').classList.toggle('crit',!!d.flame);
 if(d.flame)alertSet('flame',2,'Flame detected by the IR sensor. Check the area.');else alertClear('flame');
 camBad=d.cam===false;
 put('nMode',MODE[d.mode]||'—');put('nIp',d.ip||'—');put('nRssi',typeof d.rssi==='number'&&d.mode==='sta'?d.rssi+' dBm':'—');
 put('nUp',dur(d.uptime||0));put('nHeap',Math.round((d.heap||0)/1024)+' KB');put('nCam',camBad?'FAILED':'OK');}
async function poll(){if(pollBusy)return;pollBusy=true;
 const ac=new AbortController(),to=setTimeout(()=>ac.abort(),3500);
 try{const r=await fetch('/status',{cache:'no-store',signal:ac.signal});if(!r.ok)throw new Error('http');render(await r.json());fails=0;}
 catch(e){if(++fails>=3){setConn(false);['vT','vH','vD','vF'].forEach(i=>put(i,'—'));}}
 finally{clearTimeout(to);pollBusy=false;clearTimeout(pollT);pollT=setTimeout(poll,document.hidden?6000:1500);}}

/* ---------- Camera ---------- */
function camStart(){if(camOn)return;camOn=true;camTick();}
function camStop(){camOn=false;clearTimeout(camT);}
function camTick(){if(!camOn||camBusy)return;if(document.hidden){camT=setTimeout(camTick,1000);return;}
 camBusy=true;const im=new Image();
 im.onload=()=>{$('cam').src=im.src;camBusy=false;camFail=0;$('camMsg').hidden=true;camT=setTimeout(camTick,150);};
 im.onerror=()=>{camBusy=false;camFail++;$('camMsg').hidden=false;put('camMsg',camBad?'Camera failed to initialise. Check power and the ribbon cable.':'Camera not responding. Retrying…');camT=setTimeout(camTick,Math.min(4000,500*camFail));};
 im.src='/camera.jpg?t='+Date.now();}
async function grabFrame(){const ac=new AbortController(),to=setTimeout(()=>ac.abort(),5000);
 try{const r=await fetch('/camera.jpg?t='+Date.now(),{cache:'no-store',signal:ac.signal});
  if(!r.ok||!/image\/jpeg/.test(r.headers.get('content-type')||''))return null;
  const b=await r.blob();
  return await new Promise(res=>{const f=new FileReader();f.onload=()=>res(String(f.result).split(',')[1]||null);f.onerror=()=>res(null);f.readAsDataURL(b);});}
 catch(e){return null;}finally{clearTimeout(to);}}

/* ---------- Phone actions ---------- */
const phoneOK=p=>/^\+?[0-9 ()-]{3,20}$/.test(p);
function openUri(p,f){const t=Date.now();window.location.href=p;if(f)setTimeout(()=>{if(Date.now()-t<2200&&!document.hidden)window.location.href=f;},900);}
function doMusic(track,app){const t=(track||$('musicTrack').value).trim().slice(0,120);if(!t)return;
 const provider=app||$('musicApp').value||'any';$('musicTrack').value=t;$('musicApp').value=['spotify','ytmusic','any'].includes(provider)?provider:'any';
 const b=br('playMusic');
 if(b){log('Sending playback request to '+(provider==='ytmusic'?'YouTube Music':provider==='spotify'?'Spotify':'a music app')+' for “'+t+'”…');b.playMusic(provider,t);return;}
 log('Opening '+(provider==='spotify'?'Spotify':'YouTube Music')+' search for “'+t+'”. In this browser, select a result to start playback.');
 const e=encodeURIComponent(t);if(provider==='ytmusic'){openUri('https://music.youtube.com/search?q='+e);return;}
 if(provider==='spotify'){openUri('spotify:search:'+e,'https://open.spotify.com/search/'+e);return;}
 if(provider==='any'){openUri('https://music.youtube.com/search?q='+e,'https://open.spotify.com/search/'+e);return;}}
function doNav(){const d=$('navDest').value.trim().slice(0,120);if(!d)return;log('Opening navigation…');
 const b=br('navigate');if(b){b.navigate(d);return;}
 const e=encodeURIComponent(d);openUri('google.navigation:q='+e+'&mode=d','https://www.google.com/maps/dir/?api=1&destination='+e+'&travelmode=driving');}
function doCall(){const p=$('phone').value.trim();if(!p)return;if(!phoneOK(p)){toast('Enter a valid phone number (digits, +, spaces, dashes).');return;}
 log('Placing call…');const b=br('callNumber');if(b){b.callNumber(p);return;}window.location.href='tel:'+p.replace(/[\s()-]/g,'');}
function chooseContact(){const b=br('selectContact');if(b){b.selectContact();return;}toast('Choose a contact from the ALFRED Android app to use your phone contacts.');}
function startContactCall(name){const b=br('callContact');if(b){b.callContact((name||'').slice(0,100));return;}toast('Contact calling is available in the ALFRED Android app.');}
window.alfredContactSelected=function(name,phone,action){if(!phoneOK(phone||'')){toast('That contact does not have a usable phone number.');return;}
 $('phone').value=phone;$('phone').setAttribute('aria-label',name?'Phone number for '+name:'Phone number');log('Selected contact '+(name||'')+'.');
 if(action==='call')confirmAct('call',phone);else toast((name?name+': ':'')+'contact number added.');};
window.alfredNativeTranscript=function(text){text=String(text||'').trim();if(nativeTapPending){nativeTapPending=false;wantListen=false;$('mic').setAttribute('aria-pressed','false');$('mic').setAttribute('aria-label','Start voice listening');$('mic').title='Tap to start listening';}if(text){log('Heard: '+text);ask(text);}};
window.alfredNativeListeningState=function(active,message){if(active){$('mic').setAttribute('aria-pressed','true');$('mic').title='Listening — tap to stop';log(message||'Listening.');}else if(nativeTapPending){nativeTapPending=false;wantListen=false;$('mic').setAttribute('aria-pressed','false');$('mic').setAttribute('aria-label','Start voice listening');$('mic').title='Tap to start listening';if(message)log(message);}};
window.alfredNativeLocation=function(lat,lon,accuracy){const prompt=pendingLocationPrompt||'Current location';pendingLocationPrompt='';const msg='Your phone location is approximately '+Number(lat).toFixed(5)+', '+Number(lon).toFixed(5)+(Number(accuracy)>0?' (accuracy about '+Math.round(Number(accuracy))+' meters).':'.');caption(prompt,msg);renderSources([{title:'Open this location in Google Maps',url:'https://www.google.com/maps/search/?api=1&query='+encodeURIComponent(lat+','+lon)}]);log('ALFRED: '+msg);speak(msg);};
window.alfredNativeLocationError=function(message){const prompt=pendingLocationPrompt||'Current location';pendingLocationPrompt='';caption(prompt,message||'Could not get a current location.');log('Location: '+(message||'unavailable'));toast(message||'Could not get a current location.');};
window.alfredNativeListeningState=function(active,message){if(active){$('mic').setAttribute('aria-pressed','true');$('mic').title='Listening — tap to stop';log(message||'Listening.');}else if(nativeTapPending){$('mic').setAttribute('aria-pressed','false');$('mic').setAttribute('aria-label','Start voice listening');$('mic').title='Tap to start listening';if(message)log(message);}};
window.alfredWakeDetected=function(){log('Wake phrase heard. Listening for your command…');setState('listening');if(voiceOn){const b=br('acknowledgeWake');if(b){try{b.acknowledgeWake();return;}catch(e){}}speak('Yes?');}else{const b=br('resumeHandsFree');if(b)try{b.resumeHandsFree();}catch(e){}}};
window.alfredHandsFreeState=function(active){$('handsFreeBtn').textContent=active?'Stop hands-free listening':'Start hands-free listening (Android app)';$('handsFreeBtn').setAttribute('aria-pressed',active?'true':'false');if(active&&wantListen)stopListening();log(active?'Native hands-free listening is active.':'Native hands-free listening stopped.');};
function doSms(){const p=$('phone').value.trim(),m=$('smsBody').value.trim().slice(0,300);if(!p)return;if(!phoneOK(p)){toast('Enter a valid phone number (digits, +, spaces, dashes).');return;}
 log('Opening messages…');const b=br('sendSms');if(b){b.sendSms(p,m);return;}window.location.href='sms:'+p.replace(/[\s()-]/g,'')+'?&body='+encodeURIComponent(m);}
function cfClose(){$('confirm').hidden=true;clearTimeout(cfT);cfAct=null;}
let cfAct=null;
function confirmAct(kind,phone,body){
 if(kind==='contact'){$('phone').value='';put('cfText','Open your phone contacts to choose '+phone+'? ALFRED will call only after you choose a contact.');cfAct=()=>startContactCall(phone);$('confirm').hidden=false;clearTimeout(cfT);cfT=setTimeout(cfClose,20000);return;}
 if(!phoneOK(phone)){log('Ignored an invalid phone number in the model reply.');return;}
 $('phone').value=phone;if(kind==='sms')$('smsBody').value=body.slice(0,300);
 put('cfText','Gemini wants to '+(kind==='call'?'call ':'text ')+phone+(kind==='sms'?': “'+body.slice(0,120)+'”':'')+'. Allow?');
 cfAct=kind==='call'?doCall:doSms;$('confirm').hidden=false;clearTimeout(cfT);cfT=setTimeout(cfClose,20000);}
function asksForCodeTask(text){return /\b(code|coding|firmware|sketch|repository|repo|project files|source file|program)\b/i.test(text||'')&&/\b(edit|change|modify|fix|update|write|implement|add|remove|refactor|build)\b/i.test(text||'');}
function confirmCodeTask(task){task=(task||'').trim().slice(0,1200);if(!task)return;if(codexJob){toast('Review, apply, or discard the current Codex patch first.');return;}
 put('cfText','Send this coding request to Codex on the connected computer? Codex works in an isolated copy. You will review the patch and choose whether to apply it.');
 cfAct=()=>startCodexTask(task);$('confirm').hidden=false;clearTimeout(cfT);cfT=setTimeout(cfClose,30000);}
async function codexFetch(path,body){const r=await fetch('/__alfred/'+path,{method:body?'POST':'GET',headers:body?{'Content-Type':'application/json'}:{},body:body?JSON.stringify(body):undefined,cache:'no-store'});const d=await r.json().catch(()=>({}));if(!r.ok)throw new Error(d.error||'Desktop companion is not available in this browser.');return d;}
async function codexProbe(){try{const d=await codexFetch('health');put('codexState',d.ready?'Codex is ready on this computer. Project: '+d.workspace+'. Changes need review before applying.':(d.error||'Start Codex on this computer to connect.'));}
 catch(e){put('codexState','Not connected. Open ALFRED through the desktop companion on the computer where Codex is installed.');}}
function codexRender(d){if(d.status==='queued'||d.status==='running'){put('codexResult',d.status==='queued'?'Codex task queued…':'Codex is working in an isolated project copy…');$('codexApply').hidden=true;$('codexDiscard').hidden=true;$('codexDiff').hidden=true;return;}
 if(d.status==='review'){put('codexResult',(d.summary||'Codex finished. Review the patch below before applying it.')+'\nPatch preview from the isolated copy; ALFRED has not applied it yet.');$('codexDiff').textContent=d.diff||'(No file changes.)';$('codexDiff').hidden=!d.diff;$('codexApply').hidden=!d.diff;$('codexDiscard').hidden=false;return;}
 $('codexApply').hidden=true;$('codexDiscard').hidden=true;$('codexDiff').hidden=true;put('codexResult',d.status==='applied'?'Patch applied to the selected project.':d.status==='discarded'?'Patch discarded.':(d.error||d.summary||'Codex task failed.'));}
async function codexPollTask(id){clearTimeout(codexPoll);try{const d=await codexFetch('tasks/'+encodeURIComponent(id));codexRender(d);
 if(d.status==='queued'||d.status==='running')codexPoll=setTimeout(()=>codexPollTask(id),1200);
 else if(d.status!=='review')codexJob=null;
 }catch(e){put('codexResult',e.message||'Could not check the Codex task.');}}
async function startCodexTask(task){put('codexResult','Starting an isolated Codex task…');$('codexTask').value=task;go('assistant');
 try{const d=await codexFetch('tasks',{task:task});codexJob=d.id;codexPollTask(codexJob);}
 catch(e){put('codexResult',e.message||'Could not start Codex.');codexProbe();}}
async function codexApply(){if(!codexJob)return;put('codexResult','Checking and applying the reviewed patch…');$('codexApply').disabled=true;
 try{const d=await codexFetch('tasks/'+encodeURIComponent(codexJob)+'/apply',{});codexRender(d);codexJob=null;}
 catch(e){put('codexResult',e.message||'Could not apply patch.');}
 finally{$('codexApply').disabled=false;}}
async function codexDiscard(){if(!codexJob)return;put('codexResult','Discarding the isolated patch…');
 try{const d=await codexFetch('tasks/'+encodeURIComponent(codexJob)+'/discard',{});codexRender(d);codexJob=null;}
 catch(e){put('codexResult',e.message||'Could not discard patch.');}}
function infer(raw){const p=(raw||'').trim(),lo=p.toLowerCase();
 const cleanTrack=s=>String(s||'').trim().replace(/^(?:(?:the|a)\s+)?(?:song|track)\s+/i,'').trim();
 const targeted=lo.match(/^(?:please\s+)?(?:play|put on|start)\s+(?:the\s+)?(.+?)\s+(?:on|in)\s+(?:the\s+)?(youtube music|yt music|spotify)\s*$/);
 if(targeted){const track=cleanTrack(p.slice(lo.lastIndexOf(targeted[1])));if(!track){toast('Tell me the song name to play.');return 'missing-track';}const provider=/spotify/.test(targeted[2])?'spotify':'ytmusic';doMusic(track,provider);return 'music';}
 const pm=lo.match(/^(?:please\s+)?(?:play\s+|(?:start|open)\s+(?:music|song|spotify)\s+)(?:(?:music|song|spotify)\s+)?(.+)$/);
 if(pm&&!/\bmaps?\b/.test(lo)){$('musicTrack').value=cleanTrack(p.slice(lo.lastIndexOf(pm[1])).replace(/\s+(?:on|in)\s+(?:the\s+)?(?:spotify|youtube music|yt music)$/i,''));const provider=/spotify/.test(lo)?'spotify':/youtube music|yt music/.test(lo)?'ytmusic':null;doMusic($('musicTrack').value,provider);return 'music';}
 const app=lo.match(/^(?:please\s+)?(?:open|launch|start|switch to)\s+(?:the\s+)?(.+?)(?:\s+app)?[.!?]*$/);
 if(app&&!/^(?:music|a song|the song|song)$/.test(app[1])){const name=p.slice(lo.lastIndexOf(app[1])).replace(/\s+app[.!?]*$/i,'').replace(/[.!?]+$/,'').trim();const b=br('openApp');if(b){b.openApp(name);}else toast('Opening phone apps requires the ALFRED Android app.');return 'app';}
 const nm=lo.match(/\b(?:navigate|drive|take me|route|directions|maps?)\s+(?:to\s+)?(.+)/);
 if(nm){$('navDest').value=p.slice(lo.lastIndexOf(nm[1])).trim();doNav();return 'navigation';}
 return false;}
function localDeviceAnswer(text){const q=(text||'').toLowerCase();if(!last||!Object.keys(last).length)return null;
 const mentionsSensor=/\b(humidity|humid|temperature|temp|distance|how far|obstacle|proximity|hc\s*-?sr04|ultrasonic|flame|fire|ir sensor|sensor|readings)\b/.test(q);
 const wants=/\b(what|how much|tell me|read|current|currently|show|give me|check)\b/.test(q)||/\?$/.test(text||'');if(!wants&&!mentionsSensor)return null;
 if(/\b(humidity|humid)\b/.test(q)&&/\b(temperature|temp)\b/.test(q)){const h=typeof last.humidity==='number'?Math.round(last.humidity)+' percent':'unavailable',t=typeof last.temperature==='number'?Number(last.temperature).toFixed(1)+' degrees Celsius':'unavailable';return 'Current temperature is '+t+' and humidity is '+h+'.';}
 if(/\b(humidity|humid)\b/.test(q))return typeof last.humidity==='number'?'Current humidity is '+Math.round(last.humidity)+' percent.':'The humidity sensor has no current reading.';
 if(/\b(temperature|how hot|how cold)\b/.test(q))return typeof last.temperature==='number'?'Current temperature is '+Number(last.temperature).toFixed(1)+' degrees Celsius.':'The temperature sensor has no current reading.';
 if(/\b(distance|how far|obstacle|proximity|hc\s*-?sr04|ultrasonic)\b/.test(q))return typeof last.distance==='number'?'The HC-SR04 ultrasonic sensor reads '+Math.round(last.distance)+' centimeters. This is a sensor reading, not a safe navigation instruction.':'The HC-SR04 distance sensor has no current reading.';
 if(/\b(flame|fire|ir sensor|flame detector)\b/.test(q))return typeof last.flame==='boolean'?'The flame sensor currently reports '+(last.flame?'a trigger':'no trigger')+'.':'The flame sensor has no current reading.';
 if(/\b(sensor|readings)\b/.test(q)){const a=[];if(typeof last.temperature==='number')a.push('temperature '+Number(last.temperature).toFixed(1)+' degrees Celsius');if(typeof last.humidity==='number')a.push('humidity '+Math.round(last.humidity)+' percent');if(typeof last.distance==='number')a.push('distance '+Math.round(last.distance)+' centimeters');if(typeof last.flame==='boolean')a.push('flame '+(last.flame?'triggered':'clear'));return a.length?'Current sensor readings: '+a.join(', ')+'.':'There are no current sensor readings.';}
 return null;}
async function refreshSensorAnswer(text){if(!/\b(humidity|humid|temperature|temp|distance|how far|obstacle|proximity|hc\s*-?sr04|ultrasonic|flame|fire|ir sensor|sensor|readings)\b/i.test(text||''))return null;
 try{const ac=new AbortController(),timer=setTimeout(()=>ac.abort(),2500);try{const r=await fetch('/status',{cache:'no-store',signal:ac.signal});if(r.ok){const data=await r.json();render(data);}}finally{clearTimeout(timer);}}catch(e){}
 return localDeviceAnswer(text)||'I could not get a current reading from the device sensors. Check the sensor wiring and power.';}

/* ---------- Speech output ---------- */
function estMs(t){const w=(t||'').trim().split(/\s+/).filter(Boolean).length;return Math.max(1400,Math.min(9000,650+w*420));}
function speechTag(){if(voiceLanguage==='hi-IN'||voiceLanguage==='en-IN')return voiceLanguage;return (navigator.language||'en-IN').toLowerCase().startsWith('hi')?'hi-IN':'en-IN';}
function settle(){if(busy)setState('thinking');else setState(wantListen?'listening':'idle');resumeRec();const a=br('isHandsFreeActive');if(a&&a.isHandsFreeActive()){const b=br('resumeHandsFree');if(b)try{b.resumeHandsFree();}catch(e){}}}
function speakDone(){if(!speaking)return;speaking=false;settle();}
function speak(t,audioData){
 if(!t||!voiceOn){settle();return;}
 const tok=++spTok;speaking=true;setState('speaking');pauseRec();
  if(audioData){playNeuralAudio(t,audioData,tok);return;}
 const b=br('speakLocalized')||br('speak');
 if(b){try{if(typeof b.speakLocalized==='function')b.speakLocalized(t,speechTag());else b.speak(t);}catch(e){}setTimeout(()=>{if(tok===spTok)speakDone();},estMs(t));return;}
 if(!('speechSynthesis' in window)){speakDone();return;}
 try{window.speechSynthesis.cancel();
  const tag=speechTag(),language=tag.slice(0,2).toLowerCase(),u=new SpeechSynthesisUtterance(t),v=window.speechSynthesis.getVoices();u.lang=tag;
  const bv=v.find(a=>a.lang.toLowerCase()===tag.toLowerCase())||v.find(a=>a.lang.toLowerCase().startsWith(language+'-'))||null;
  if(bv)u.voice=bv;u.rate=.96;u.pitch=.92;
  u.onend=u.onerror=()=>{if(tok===spTok)speakDone();};u.onboundary=()=>Orb.pulse();
  window.speechSynthesis.speak(u);}
 catch(e){speakDone();}}
function playNeuralAudio(text,audioData,tok){try{if(activeAudio){activeAudio.pause();activeAudio=null;}
 const data=typeof audioData==='string'?audioData:audioData.data,mime=typeof audioData==='string'?'audio/wav':(audioData.mimeType||'audio/wav');
 if(!data)throw new Error('No audio');const player=new Audio('data:'+mime+';base64,'+data);activeAudio=player;player.onended=()=>{if(tok===spTok){activeAudio=null;speakDone();}};
 player.onerror=()=>{if(tok===spTok){activeAudio=null;speakFallback(text,tok);}};const p=player.play();if(p&&p.catch)p.catch(()=>{if(tok===spTok){activeAudio=null;speakFallback(text,tok);}});
 }catch(e){speakFallback(text,tok);}}
function speakFallback(text,tok){const b=br('speakLocalized')||br('speak');if(b){try{if(typeof b.speakLocalized==='function')b.speakLocalized(text,speechTag());else b.speak(text);setTimeout(()=>{if(tok===spTok)speakDone();},estMs(text));return;}catch(e){}}
 if(!('speechSynthesis'in window)){speakDone();return;}try{window.speechSynthesis.cancel();const tag=speechTag(),language=tag.slice(0,2).toLowerCase(),u=new SpeechSynthesisUtterance(text),v=window.speechSynthesis.getVoices();u.lang=tag;const bv=v.find(a=>a.lang.toLowerCase()===tag.toLowerCase())||v.find(a=>a.lang.toLowerCase().startsWith(language+'-'))||null;if(bv)u.voice=bv;u.rate=.96;u.pitch=.92;u.onend=u.onerror=()=>{if(tok===spTok)speakDone();};window.speechSynthesis.speak(u);}catch(e){speakDone();}}

/* ---------- Speech input ---------- */
function micMsg(m){log(m);toast(m);}
function micBlocked(){wantListen=false;awake=false;$('mic').setAttribute('aria-pressed','false');$('mic').setAttribute('aria-label','Start voice listening');$('mic').title='Microphone unavailable — check site permissions';
 const m=window.isSecureContext?'Microphone permission was denied. Allow it in your browser’s site settings, then tap the mic again.':'The browser blocked the microphone for this plain-HTTP page. Accept the permission prompt if it appears, use the text box, or open ALFRED in the Android app.';
 alertSet('mic',1,m);settle();}
function ensureRec(){if(rec)return;rec=new SR();rec.continuous=false;rec.interimResults=false;rec.lang=speechTag();
 rec.onstart=()=>{recRun=true;$('mic').title='Microphone active — tap to stop';log('Microphone opened. Speak naturally; saying “Alfred” is optional while listening is on. Tap the mic to stop.');};
 rec.onspeechstart=rec.onsoundstart=()=>{Orb.pulse();if(awake&&!busy&&!speaking){clearTimeout(awT);setState('listening');}};
 rec.onresult=e=>{recFail=0;const raw=((e.results[0]&&e.results[0][0]&&e.results[0][0].transcript)||'').trim();if(!raw)return;
  const wake=/(?:alfred|अल्फ्रेड|अलफ्रेड|एल्फ्रेड)/i.exec(raw);log('Heard: '+raw);
  if(wake){const cmd=raw.slice(wake.index+wake[0].length).replace(/^[\s,.:;!?—–।-]+/,'').trim();if(cmd){sleepAwake();ask(cmd);}else awaken();}
  else{sleepAwake();ask(raw);}};
 rec.onnomatch=()=>{log('Speech was detected but could not be recognized. Try speaking clearly or check the selected language.');};
 rec.onerror=e=>{const c=e.error;if(c==='aborted')return;if(c==='no-speech'){log('No speech detected. Check the selected microphone and input level.');return;}
  if(c==='not-allowed'||c==='service-not-allowed'){micBlocked();return;}
  recFail++;log('Mic error: '+c);
  if(c==='audio-capture')micMsg('No microphone found, or it is in use by another app.');
  else if(c==='network')micMsg('Speech recognition needs an internet connection.');
  else if(c==='language-not-supported')micMsg('This browser does not support the selected recognition language. Choose Auto or English (India).');
  else micMsg('Speech recognition stopped ('+c+'). Check Chrome site and operating-system microphone permissions.');};
 rec.onend=()=>{recRun=false;if(wantListen&&!speaking&&!busy){clearTimeout(rsT);rsT=setTimeout(startRec,Math.min(5000,250*Math.pow(2,Math.min(recFail,5))));}};}
function startRec(){if(!wantListen||recRun||speaking||busy||!rec)return;try{rec.start();}catch(e){if(e&&e.name==='InvalidStateError')return;recFail++;log('Could not start microphone recognition: '+(e&&e.name||'unknown error'));}}
function pauseRec(){clearTimeout(rsT);try{if(rec)rec.abort();}catch(e){}}
function resumeRec(){if(wantListen){clearTimeout(rsT);rsT=setTimeout(startRec,300);}}
function awaken(){awake=true;setState('listening');log('Awake. Waiting for your command…');clearTimeout(awT);
 awT=setTimeout(()=>{if(awake){awake=false;log('Back to standby (silence).');if(!busy&&!speaking)settle();}},6000);}
function sleepAwake(){awake=false;clearTimeout(awT);}
function startListening(quiet){
 const native=br('listenOnce');if(!native&&!SR){micMsg('Speech recognition isn’t supported here. Open ALFRED in its Android app or type your request.');return;}
 alertClear('mic');wantListen=true;recFail=0;$('mic').setAttribute('aria-pressed','true');$('mic').setAttribute('aria-label','Stop voice listening');$('mic').title='Starting microphone…';
 if(native){nativeTapPending=true;try{native.listenOnce();}catch(e){nativeTapPending=false;wantListen=false;micMsg('Could not start voice input. Check microphone permission and try again.');}return;}
 ensureRec();if(!window.isSecureContext)log('This page is plain HTTP. The browser may block microphone access; use a secure HTTPS page for reliable microphone access.');
 if(!greeted&&!quiet){greeted=true;speak('ALFRED online. How can I help?');}else{settle();}}
function stopListening(){wantListen=false;sleepAwake();pauseRec();if(nativeTapPending){nativeTapPending=false;const b=br('stopListenOnce');if(b)try{b.stopListenOnce();}catch(e){}}$('mic').setAttribute('aria-pressed','false');$('mic').setAttribute('aria-label','Start voice listening');$('mic').title='Tap to start listening; tap again to stop';settle();}
function awakenNow(){if(busy||speaking)return;if(!wantListen){greeted=true;startListening(true);}if(wantListen)awaken();}

/* ---------- Gemini ---------- */
const VIS=/\b(see|look|looking|view|watch|notice|holding|wearing|colou?r|face|person|camera|photo|picture|snapshot|eyes)\b|\bread (this|that|the|what)|what(?:’s|'s| is) (this|that|in)|who is (this|that|there)|in front of (me|you)|\b(?:find|locate)\s+(?:my|the|a|an)\b|\bwhere(?:'s| is)\s+(?:my|the)\b|इसे पढ़|इसे देख|यह क्या|क्या दिख|क्या दिखाई|कहाँ है|ढूँढो|मेरे सामने|इस तस्वीर|इस फोटो/i;
const LIVE=/\b(search|google|web|online|internet|look up|find online|on the internet|latest|current|today|right now|recent|news|weather|price|prices|stock|score|schedule|release date|this week|this month|as of now|what happened|who is the current|who is currently)\b|आज|अभी|ताज़ा|ताजा|हाल की खबर|समाचार|मौसम|कीमत|रेट|स्कोर|इस सप्ताह|वेब पर खोज|इंटरनेट पर खोज/i;
function wantsLiveInfo(text){return LIVE.test(text||'');}
function groundedSources(data){const raw=data&&data.candidates&&data.candidates[0]&&data.candidates[0].groundingMetadata&&data.candidates[0].groundingMetadata.groundingChunks||[];
 return raw.map(x=>x&&x.web?{title:x.web.title||'',url:x.web.uri||''}:null).filter(Boolean);}
function cloudConfig(){return{url:(ls.g('alfred_sb_url')||ALFRED_SUPABASE_URL).replace(/\/$/,''),anon:ls.g('alfred_sb_anon')||ALFRED_SUPABASE_PUBLIC_KEY,session:JSON.parse(ls.g('alfred_sb_session')||'null')};}
function cloudReady(){try{const c=cloudConfig();return !!(c.url&&c.anon&&c.session&&c.session.access_token);}catch(e){return false;}}
async function cloudFetch(path,body){
 const c=cloudConfig();if(!c.url||!c.anon)throw new Error('Enter the Supabase URL and public key first.');
 let session=c.session;
 if(session&&session.refresh_token&&session.expires_at&&session.expires_at*1000<Date.now()+120000){
  const rr=await fetch(c.url+'/auth/v1/token?grant_type=refresh_token',{method:'POST',headers:{apikey:c.anon,'Content-Type':'application/json'},body:JSON.stringify({refresh_token:session.refresh_token})});
  if(rr.ok){const fresh=await rr.json();fresh.expires_at=Math.floor(Date.now()/1000)+(fresh.expires_in||3600);ls.s('alfred_sb_session',JSON.stringify(fresh));session=fresh;}
 }
 if(!session||!session.access_token)throw new Error('Sign in to your ALFRED cloud account first.');
 const r=await fetch(c.url+'/functions/v1/alfred-assistant',{method:'POST',headers:{apikey:c.anon,Authorization:'Bearer '+session.access_token,'Content-Type':'application/json'},body:JSON.stringify(body)});
 const d=await r.json().catch(()=>({}));if(!r.ok){let message=d.error||('Cloud request failed ('+r.status+').');
  if(body&&body.googleSearch===true&&r.status===502)message+=' If this Gemini API project is on the Free tier, Google Search grounding is unavailable. Turn off Google Search for normal chat or use an eligible paid-tier project.';
  throw new Error(message);}return d;
}
async function cloudGem(contents,sys,searchNow){
 try{const d=await cloudFetch('alfred-assistant',{action:'chat',contents:contents,systemInstruction:sys,apiKeys:keys,model:model,googleSearch:searchNow,speech:false,speechLanguage:speechTag(),remember:/^\s*(please\s+)?(remember|save to memory|don.t forget)\b/i.test((contents[contents.length-1].parts||[]).map(p=>p.text||'').join(''))});
  return{ok:!!d.text,rotate:false,text:d.text||'',sources:d.sources||[],audio:d.audio?{data:d.audio,mimeType:d.audioMimeType||'audio/wav'}:null,msg:d.error||''};
 }catch(e){if(!searchNow)throw e;const query=(contents[contents.length-1].parts||[]).map(p=>p.text||'').join(' ').trim();return{ok:true,rotate:false,text:'I could not fetch a verified live-search summary. Open these live results instead.',sources:[{title:'Open Google Search results for “'+query.slice(0,100)+'”',url:'https://www.google.com/search?q='+encodeURIComponent(query)}],audio:null,msg:''};}
}
async function gem(key,contents,sys,searchNow){
 const ac=new AbortController(),to=setTimeout(()=>ac.abort(),searchNow?12000:18000);
 try{const r=await fetch(API+model+':generateContent',{method:'POST',headers:{'Content-Type':'application/json','x-goog-api-key':key},signal:ac.signal,
   body:JSON.stringify(Object.assign({contents:contents,systemInstruction:{parts:[{text:sys}]}},searchNow?{tools:[{google_search:{}}]}:{}))});
  let d=null;try{d=await r.json();}catch(e){}
  if(!r.ok||(d&&d.error)){const er=(d&&d.error)||{},m=String(er.message||('HTTP '+r.status)).split(key).join('***').slice(0,240);
   const hint=r.status===429?' Keys from the same Google project share one quota, so new keys do not reset it. Try the Flash-Lite model, turn off Google Search, or wait.':((r.status===401||r.status===403)?' (Check this is a Gemini API key from AI Studio; if it keeps failing, create a new key.)':'');
   const searchHint=searchNow&&(r.status===400||r.status===403)?' Google Search grounding was rejected. Check that this Gemini project and model are eligible for Search grounding; Google currently lists it as unavailable on the Gemini API Free tier.':'';
   return{ok:false,rotate:r.status===401||(/API key/i.test(m)&&r.status!==429),msg:m+hint+searchHint};}
  const cd=d&&d.candidates&&d.candidates[0],ps=cd&&cd.content&&cd.content.parts;
  const t=ps?ps.map(p=>p.text||'').join('').trim():'';
  if(!t){const why=(cd&&cd.finishReason)||(d&&d.promptFeedback&&d.promptFeedback.blockReason)||'';return{ok:false,rotate:false,msg:'Empty or blocked response'+(why?' ('+why+')':'')+'.'};}
  return{ok:true,text:t,sources:groundedSources(d)};}
 catch(e){return{ok:false,rotate:false,msg:e.name==='AbortError'?'Timed out waiting for Gemini.':'Network error. Is this device connected to the internet?'};}
 finally{clearTimeout(to);}}
function fail(text,msg){busy=false;caption(text,'Request failed: '+msg);log('Gemini error: '+msg);toast(msg);settle();}
function searchFallback(text){const q=String(text||'').trim();const url='https://www.google.com/search?q='+encodeURIComponent(q);const message='Gemini could not complete the AI search. Open the live search results below.';caption(q,message);renderSources([{title:'Open Google Search results for “'+q.slice(0,100)+'”',url:url}]);log('ALFRED: '+message);busy=false;speak(message);}
async function synthesizeWithKey(key,text){if(!key||!text)return null;const ac=new AbortController(),to=setTimeout(()=>ac.abort(),18000);
 try{const hi=/hi-IN/.test(speechTag())||/[\u0900-\u097f]/.test(text);const style=hi?'Speak in clear, natural Hindi with a warm, calm, confident conversational delivery. Use an unhurried pace and crisp pronunciation.':'Speak in clear, natural Indian English with a warm, calm, confident conversational delivery. Use an unhurried pace and crisp pronunciation.';
  const r=await fetch('https://generativelanguage.googleapis.com/v1beta/interactions',{method:'POST',signal:ac.signal,headers:{'Content-Type':'application/json','x-goog-api-key':key},body:JSON.stringify({model:'gemini-3.8-flash-lite-tts',input:[{type:'user_input',content:[{type:'text',text:text.slice(0,1400),annotations:[{type:'speech_metadata',style:style}]}]}],response_format:{type:'audio'},generation_config:{speech_config:[{voice:'Orus'}]}})});
  const d=await r.json().catch(()=>({}));if(!r.ok)return null;const a=d.output_audio||d.interaction?.output_audio||{};return a.data?{data:a.data,mimeType:a.mime_type||a.mimeType||'audio/wav'}:null;
 }catch(e){return null;}finally{clearTimeout(to);}}
async function synthesizeLocal(text){if(!neuralVoice||!voiceOn||!keys.length)return null;
 for(let n=0;n<Math.min(keys.length,3);n++){const index=(ki+n)%keys.length,key=keys[index],a=await synthesizeWithKey(key,text);if(a){ki=index;return a;}}return null;}
async function ask(text,forceVis){
 text=(text||'').trim().slice(0,500);if(!text)return;
 if(busy){toast('ALFRED is still working on the previous request.');return;}
 if(!forceVis&&asksForCodeTask(text)){confirmCodeTask(text);return;}
 if(!forceVis){
  const lower=text.toLowerCase();
  if(/\b(where am i|where is my (?:current )?location|what is my (?:current )?location|my (?:current )?location|my coordinates|gps location)\b/i.test(text)){const locationBridge=br('getCurrentLocation');if(locationBridge){pendingLocationPrompt=text;caption(text,'Getting your phone location…');log('Requesting a current phone location.');try{locationBridge.getCurrentLocation();}catch(e){window.alfredNativeLocationError('Could not request location. Check ALFRED’s location permission.');}}else{const msg='Current phone location is available in the ALFRED Android app.';caption(text,msg);speak(msg);}return;}
  const immediateAction=infer(text);if(immediateAction){const openMatch=text.match(/^(?:please\s+)?(?:open|launch|start|switch to)\s+(?:the\s+)?(.+?)(?:\s+app)?[.!?]*$/i);const target=openMatch?openMatch[1].trim():'your request';const msg=immediateAction==='music'?'Opening the requested music app. Check the app to confirm playback.':immediateAction==='app'?'Trying to open '+target+'. Check your phone to see whether it launched.':immediateAction==='missing-track'?'Which song should I play?': 'Opening the requested destination in Maps. Check Maps to confirm it opened.';caption(text,msg);log('ALFRED: '+msg);speak(msg);return;}
  const sensorReply=await refreshSensorAnswer(text);if(sensorReply){caption(text,sensorReply);log('ALFRED: '+sensorReply);speak(sensorReply);return;}
  if(/\b(who|what)\s+(?:created|made|built)\s+(?:you|alfred)\b|\bcreator of (?:you|alfred)\b/i.test(text)){const msg='I was created by SVJ Labs. Gemini is one of the AI models I can use.';caption(text,msg);log('ALFRED: '+msg);speak(msg);return;}
 }
 if(!keys.length&&!cloudReady()){toast('Connect the cloud assistant or add a Gemini API key in Settings.');go('settings');return;}
 busy=true;const id=++reqId;sleepAwake();pauseRec();setState('thinking');caption(text,'');renderSources([]);let sources=[];
 try{
  const parts=[{text:text}],wantsVision=forceVis||(autoVis&&VIS.test(text));
  if(wantsVision){const b=await grabFrame();if(id!==reqId)return;
   if(b){parts.push({inlineData:{mimeType:'image/jpeg',data:b}});log('Camera frame attached.');}
   else{log('Camera frame unavailable.');fail(text,'Could not capture a camera frame. Check the camera and try again.');return;}}
  const answerLanguage=voiceLanguage==='hi-IN'?'Hindi':voiceLanguage==='en-IN'?'English (India)':'the user’s language when clear';
  const liveInfo=wantsLiveInfo(text),searchNow=useSearch&&liveInfo;
  if(searchNow)log('Searching the web for current information…');
  else if(liveInfo&&!useSearch)log('Live search is off. Enable “Allow Google Search for current questions” in Settings.');
  let sys='You are ALFRED, a calm, observant and capable assistant with a warm, natural voice. ALFRED was created by SVJ Labs; Gemini is an AI model provider, not ALFRED’s creator. Today is '+new Date().toLocaleDateString(navigator.language||undefined)+'. Reply in '+answerLanguage+' unless the user explicitly asks for another language. Be concise by default, acknowledge naturally, and ask one short clarifying question only when needed. Use saved personal details only when relevant. Be honest about uncertainty and do not claim actions you have not completed.\nLive device sensors: Temperature '+(last.temperature!=null?last.temperature.toFixed(1)+' °C':'unavailable')+', humidity '+(last.humidity!=null?Math.round(last.humidity)+'%':'unavailable')+', HC-SR04 distance '+(last.distance!=null?Math.round(last.distance)+' cm':'unavailable')+', flame sensor '+(typeof last.flame==='boolean'?(last.flame?'triggered':'clear'):'unavailable')+'. Answer sensor questions from these readings and never claim the sensors are inaccessible when a reading is present.\nCurrent music app selection: '+$('musicApp').value+'. For an explicit request to play music, append [INTENT:PLAY,data:TRACK,app:APP] where APP is spotify, ytmusic, or any; honor a named app, and otherwise use the selected app.\nFor explicit maps/driving requests append exactly [INTENT:NAV,data:DESTINATION].\nFor an explicit request to call a person by name, append [INTENT:CALL_CONTACT,data:NAME]. For a request to call a provided phone number append [INTENT:CALL,data:PHONE]. Never invent a number or contact name.\nFor explicit SMS append exactly [INTENT:SMS,data:PHONE,body:MESSAGE]. Never send without user confirmation.\nOnly say an action is complete after the app confirms it. Calls and texts require the user to approve and finish Android UI; a text is never sent by ALFRED itself. Maps opens directions but cannot confirm navigation started. Music playback cannot be verified by ALFRED. Reminders and general computer/app control are unavailable in this build; say so clearly instead of claiming they were done. ALFRED is not a navigation safety device.';
  if(searchNow)sys+='\nUse live web search for this answer. Distinguish sourced facts from uncertainty and do not invent sources.';
  else if(liveInfo)sys+='\nLive web search is not enabled for this request. Do not claim to have searched or verified current facts; say that you cannot confirm live information and ask the user to enable Google Search in Settings.';
  const contents=hist.concat([{role:'user',parts:parts}]);
  let out=null,err='',audio=null;
  if(cloudReady()){
   const r=await cloudGem(contents,sys,searchNow);if(id!==reqId)return;
   if(r.ok){out=r.text;sources=r.sources||[];audio=r.audio||null;}else err=r.msg;
  }else for(let n=0;n<keys.length;n++){
   const r=await gem(keys[ki],contents,sys,searchNow);if(id!==reqId)return;
   if(r.ok){out=r.text;sources=r.sources||[];break;}
   err=r.msg;
   if(r.rotate&&keys.length>1){log('Key #'+(ki+1)+' was rejected or rate-limited. Switching key.');ki=(ki+1)%keys.length;continue;}
   break;}
  if(out===null){if(searchNow){searchFallback(text);return;}fail(text,err||'No response.');return;}
  const reply=out.replace(/[*#`]/g,'');let x,did=false,musicRequest=null,actionStatus='';
  if((x=reply.match(/\[INTENT:NAV,data:(.*?)\]/))){$('navDest').value=x[1].trim().slice(0,120);setTimeout(doNav,1500);actionStatus='Opening Maps directions to '+$('navDest').value+'. Check Maps to confirm it opened.';did=true;}
  if((x=reply.match(/\[INTENT:PLAY,data:([^,\]]+)(?:,app:(spotify|ytmusic|any))?\]/i))){musicRequest={track:x[1].trim(),app:(x[2]||$('musicApp').value||'any').toLowerCase()};setTimeout(()=>doMusic(musicRequest.track,musicRequest.app),1100);actionStatus='Sent a play request for '+musicRequest.track+' to '+(musicRequest.app==='spotify'?'Spotify':musicRequest.app==='ytmusic'?'YouTube Music':'your selected music app')+'. I cannot confirm playback.';did=true;}
  if((x=reply.match(/\[INTENT:CALL_CONTACT,data:([^\]]+)\]/i))){confirmAct('contact',x[1].trim());actionStatus='Please confirm opening contacts, then choose the person. Android will ask again before calling.';did=true;}
  if((x=reply.match(/\[INTENT:CALL,data:(.*?)\]/))){confirmAct('call',x[1].trim());actionStatus='I need your confirmation before asking Android to place this call.';did=true;}
  if((x=reply.match(/\[INTENT:SMS,data:(.*?),body:(.*?)\]/))){confirmAct('sms',x[1].trim(),x[2].trim());actionStatus='I prepared this text for review. Confirm to open Messages, then review and tap Send there.';did=true;}
  const inferred=!did?infer(text):false;
  if(inferred)actionStatus='I started the requested app action. Check the phone to confirm it opened; ALFRED cannot verify completion.';
  let spoken=reply.replace(/\[INTENT:[^\]]*\]/g,'').trim();
  if(actionStatus)spoken=actionStatus;
  if(musicRequest&&!br('playMusic'))spoken='Opening '+(musicRequest.app==='spotify'?'Spotify':'YouTube Music')+' search for '+musicRequest.track+'. Select a result to start playback; this browser cannot confirm playback.';
  hist.push({role:'user',parts:[{text:text}]},{role:'model',parts:[{text:reply}]});
  hist=hist.slice(-6);while(hist.length&&hist[0].role!=='user')hist.shift();
  caption(text,spoken||'Done.');renderSources(sources);log('ALFRED: '+spoken);
  busy=false;speak(spoken,audio);
 }catch(e){if(id===reqId){const message=e&&typeof e.message==='string'&&e.message.trim()?e.message:'Unexpected error.';fail(text,message);}}
 finally{if(id===reqId&&busy){busy=false;settle();}}}

/* ---------- Network setup ---------- */
async function jf(url,opt,ms){const ac=new AbortController(),to=setTimeout(()=>ac.abort(),ms||5000);
 try{const r=await fetch(url,Object.assign({cache:'no-store',signal:ac.signal},opt||{}));let d=null;try{d=await r.json();}catch(e){}return{ok:r.ok,s:r.status,d:d};}
 finally{clearTimeout(to);}}
async function netRefresh(){try{const r=await jf('/net/status');if(r.ok&&r.d)put('nSsid',r.d.ssid||'—');}catch(e){}}
function renderNets(list){const box=$('nets');box.textContent='';const m={};
 list.forEach(n=>{if(!m[n.ssid]||m[n.ssid].rssi<n.rssi)m[n.ssid]=n;});
 const a=Object.keys(m).map(k=>m[k]).sort((p,q)=>q.rssi-p.rssi);
 if(!a.length){put('scanMsg','No 2.4 GHz networks found.');return;}
 put('scanMsg',a.length+' network'+(a.length>1?'s':'')+' found. Tap one to select.');
 a.forEach(n=>{const b=document.createElement('button');b.className='net';b.textContent=n.ssid+' · '+n.rssi+' dBm · '+(n.open?'open':'secured');
  b.addEventListener('click',()=>{$('ssid').value=n.ssid;$('pass').focus();});box.appendChild(b);});}
async function scan(){if(scanning)return;scanning=true;$('scanBtn').disabled=true;put('scanMsg','Scanning… the camera feed may pause briefly.');
 try{const r=await jf('/net/scan',{method:'POST'});if(r.s!==202)throw new Error('busy');
  for(let i=0;i<15;i++){await sleep(1000);const q=await jf('/net/scan');
   if(q.d&&q.d.state==='done'){renderNets(q.d.networks||[]);return;}
   if(q.d&&q.d.state==='idle')break;}
  put('scanMsg','Scan did not finish. Try again.');}
 catch(e){put('scanMsg','Scan failed: device busy or unreachable.');}
 finally{scanning=false;$('scanBtn').disabled=false;}}
async function netConnect(){const s=$('ssid').value,p=$('pass').value,m=$('netMsg');
 if(!s||new TextEncoder().encode(s).length>32){put('netMsg','Enter a network name (up to 32 bytes).');return;}
 if(p&&(p.length<8||p.length>63)){put('netMsg','Password must be 8–63 characters, or empty for an open network.');return;}
 $('connBtn').disabled=true;put('netMsg','Sending…');
 try{const r=await jf('/net/connect',{method:'POST',body:new URLSearchParams({ssid:s,pass:p})},8000);
  if(r.s!==202){put('netMsg',(r.d&&r.d.err)||'The device rejected the request.');return;}
  $('pass').value='';put('netMsg','Connecting… this can take up to 20 seconds.');
  let lost=0;
  for(let i=0;i<28;i++){await sleep(1500);
   try{const q=await jf('/net/status');lost=0;
    if(q.d&&q.d.attempt==='ok'){put('netMsg','Connected and saved. New address: http://'+q.d.ip+'. If this page stops responding, rejoin your normal Wi-Fi and open that address.');return;}
    if(q.d&&q.d.attempt==='failed'){put('netMsg','Could not connect: '+(q.d.err||'unknown error')+'. Saved Wi-Fi was not changed.');return;}}
   catch(e){if(++lost>=4){put('netMsg','Lost contact with ALFRED. If it joined the new network, rejoin that Wi-Fi and find its new address.');return;}}}
  put('netMsg','Still waiting. Check the Device status above.');}
 catch(e){put('netMsg','Request failed. Is the device reachable?');}
 finally{$('connBtn').disabled=false;}}
async function netForget(){if(!window.confirm('Forget the saved Wi-Fi? ALFRED will open its “ALFRED-Setup” network.'))return;
 try{const r=await jf('/net/forget',{method:'POST'});put('netMsg',r.ok?'Saved Wi-Fi cleared. Join “ALFRED-Setup” and open http://192.168.4.1':'Request failed.');}
 catch(e){put('netMsg','Request failed. Is the device reachable?');}}

/* ---------- Settings ---------- */
function parseKeys(raw){const a=raw.split(/[\s,;]+/).map(s=>s.replace(/^["']+|["']+$/g,'')).filter(Boolean);
 if(!a.length)return{err:'Nothing to save. Paste at least one key.'};
 if(a.length>8)return{err:'Up to 8 keys are supported; you pasted '+a.length+'.'};
 const i=a.findIndex(k=>!/^[\x21-\x7e]{20,200}$/.test(k));
 if(i>=0)return{err:'Key '+(i+1)+' looks wrong: it needs 20+ visible characters with no spaces.'};
 return{k:a};}
function keysUI(){put('keysInfo',keys.length?keys.length+' key'+(keys.length>1?'s':'')+' loaded: '+keys.map(k=>'••••'+k.slice(-4)).join(', '):'No key loaded.');}
function saveKeys(){const raw=$('keysArea').value,r=parseKeys(raw);
 if(!r.k){toast(r.err);return;}
 const k=r.k;keys=k;ki=0;if($('remember').checked){ls.s('alfred_secure_pool',k.join('\n'));}else ls.d('alfred_secure_pool');
 $('keysArea').value='';keysUI();log('Key(s) loaded.');toast('Keys loaded.');go('assistant');if(!wantListen)startListening();}
function flushKeys(){stopListening();reqId++;busy=false;keys=[];ki=0;hist=[];ls.d('alfred_secure_pool');$('keysArea').value='';
 try{window.speechSynthesis.cancel();}catch(e){}speaking=false;keysUI();settle();log('Keys cleared from memory and this browser.');toast('Keys flushed.');}

/* ---------- Cloud account and persistent memory ---------- */
function cloudUI(){let c;try{c=cloudConfig();}catch(e){c={url:'',anon:'',session:null};}
 $('sbUrl').value=c.url;$('sbAnon').value=c.anon;
 put('cloudState',c.session&&c.session.user?'Signed in as '+(c.session.user.email||'ALFRED user')+'. Cloud AI and private memory are ready.':(c.url&&c.anon?'Cloud configured. Sign in or create an account.':'Cloud memory is not configured.'));
}
function saveCloudConfig(){const url=$('sbUrl').value.trim().replace(/\/$/,''),anon=$('sbAnon').value.trim();
 if(url&&!/^https:\/\/[a-z0-9-]+\.supabase\.co$/i.test(url)){toast('Use the project URL ending in .supabase.co.');return false;}
 if(url&&(!anon||anon.length<20)){toast('Paste the Supabase publishable or anon key.');return false;}
 ls.s('alfred_sb_url',url);ls.s('alfred_sb_anon',anon);cloudUI();return true;}
async function cloudAuth(mode){if(!saveCloudConfig())return;const c=cloudConfig(),email=$('sbEmail').value.trim(),password=$('sbPassword').value;
 if(!email||!password){toast('Enter your account email and password.');return;}
 const path=mode==='signup'?'/auth/v1/signup':'/auth/v1/token?grant_type=password';
 try{const r=await fetch(c.url+path,{method:'POST',headers:{apikey:c.anon,'Content-Type':'application/json'},body:JSON.stringify({email:email,password:password})});const d=await r.json().catch(()=>({}));
  if(!r.ok)throw new Error(d.msg||d.message||d.error_description||d.error||'Account request failed.');
  if(!d.access_token){put('cloudState','Check your email to confirm the account, then sign in here.');return;}
  d.expires_at=Math.floor(Date.now()/1000)+(d.expires_in||3600);ls.s('alfred_sb_session',JSON.stringify(d));$('sbPassword').value='';cloudUI();toast('Cloud assistant connected.');}
 catch(e){put('cloudState',e.message||'Could not connect. Check the URL, key and network.');}}
async function memoryReview(){try{const d=await cloudFetch('alfred-assistant',{action:'memory_list'}),box=$('memoryList');box.textContent='';
 if(!d.memories||!d.memories.length){box.textContent='No saved facts yet. Tell ALFRED “Remember that …” to save one.';return;}
 d.memories.forEach(m=>{const row=document.createElement('div');row.className='card';row.textContent=m.memory_text;box.appendChild(row);});}
 catch(e){put('memoryList',e.message||'Could not load saved memories.');}}
async function memoryClear(){if(!window.confirm('Delete all saved ALFRED conversations and personal memories for this account?'))return;
 try{await cloudFetch('alfred-assistant',{action:'memory_clear'});$('memoryList').textContent='Cloud history and saved memories cleared.';toast('Cloud memory cleared.');}
 catch(e){toast(e.message||'Could not clear cloud memory.');}}

/* ---------- Wiring ---------- */
function sendText(){const f=$('cmd'),v=f.value.trim();if(!v)return;if(busy){toast('ALFRED is still working on the previous request.');return;}f.value='';ask(v);}
['assistant','vision','sensors','network','settings'].forEach(n=>$('t-'+n).addEventListener('click',()=>go(n)));
$('send').addEventListener('click',sendText);
$('cmd').addEventListener('keydown',e=>{if(e.key==='Enter')sendText();});
$('mic').addEventListener('click',()=>{if(wantListen)stopListening();else startListening();});
$('orbBox').addEventListener('click',awakenNow);
$('orbBox').addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();awakenNow();}});
$('bMusic').addEventListener('click',doMusic);$('bNav').addEventListener('click',doNav);
$('bCall').addEventListener('click',doCall);$('bSms').addEventListener('click',doSms);$('bContact').addEventListener('click',chooseContact);
$('handsFreeBtn').addEventListener('click',()=>{const b=br('setHandsFree');if(!b){toast('Hands-free listening requires the ALFRED Android app.');return;}
 try{if(b.isHandsFreeActive())b.setHandsFree(false);else{stopListening();b.setHandsFree(true);}}catch(e){toast('Could not change hands-free listening. Check microphone permission.');}});
$('cfOk').addEventListener('click',()=>{const a=cfAct;cfClose();if(a)a();});$('cfNo').addEventListener('click',cfClose);
$('codexRun').addEventListener('click',()=>confirmCodeTask($('codexTask').value));$('codexApply').addEventListener('click',codexApply);$('codexDiscard').addEventListener('click',codexDiscard);
$('visBtn').addEventListener('click',()=>{go('assistant');ask('Describe the current camera image briefly. Mention only details that are clearly visible and state uncertainty.',true);});
$('readTextBtn').addEventListener('click',()=>{go('assistant');ask('Read the clearly legible text in the camera image. Transcribe only what is visible, preserve the original language and reading order, mark unclear portions as [unclear], and do not guess. If no text is legible, say so briefly.',true);});
function findObjectInFrame(){const item=$('objectQuery').value.trim().replace(/["'<>]/g,'').slice(0,100);if(!item){toast('Enter an object to look for.');$('objectQuery').focus();return;}
 go('assistant');ask('Check the current camera image for this item: '+item+'. Say whether it is clearly visible. If visible, describe its position within the image (left, center or right; upper, middle or lower) and the visible clues. State uncertainty. Do not estimate real-world distance or give movement or navigation directions. If it is not clearly visible, say so.',true);}
$('findObjectBtn').addEventListener('click',findObjectInFrame);
$('objectQuery').addEventListener('keydown',e=>{if(e.key==='Enter')findObjectInFrame();});
$('scanBtn').addEventListener('click',scan);$('connBtn').addEventListener('click',netConnect);$('forgetBtn').addEventListener('click',netForget);
$('saveKeys').addEventListener('click',saveKeys);$('clearKeys').addEventListener('click',flushKeys);
$('cloudSignIn').addEventListener('click',()=>cloudAuth('signin'));$('cloudSignUp').addEventListener('click',()=>cloudAuth('signup'));
$('cloudSignOut').addEventListener('click',()=>{ls.d('alfred_sb_session');cloudUI();$('memoryList').textContent='';toast('Signed out of cloud memory.');});
$('memoryReview').addEventListener('click',memoryReview);$('memoryClear').addEventListener('click',memoryClear);
$('sbUrl').addEventListener('change',saveCloudConfig);$('sbAnon').addEventListener('change',saveCloudConfig);
$('voiceChk').addEventListener('change',e=>{voiceOn=e.target.checked;ls.s('alfred_voice',voiceOn?'1':'0');if(!voiceOn){try{window.speechSynthesis.cancel();}catch(x){}}});
$('voiceLanguage').addEventListener('change',e=>{voiceLanguage=e.target.value;ls.s('alfred_voice_language',voiceLanguage);const b=br('setVoiceLanguage');if(b)b.setVoiceLanguage(voiceLanguage);if(rec){pauseRec();rec.lang=speechTag();if(wantListen)resumeRec();}log('Voice language: '+(voiceLanguage==='hi-IN'?'Hindi':voiceLanguage==='auto'?'device language':'English (India)'));});
$('srchChk').addEventListener('change',e=>{useSearch=e.target.checked;ls.s('alfred_search',useSearch?'1':'0');});
$('modelSel').addEventListener('change',e=>{model=e.target.value;ls.s('alfred_model',model);log('Model: '+model);});
$('visChk').addEventListener('change',e=>{autoVis=e.target.checked;ls.s('alfred_autovis',autoVis?'1':'0');});
document.addEventListener('visibilitychange',()=>{if(!document.hidden){clearTimeout(pollT);poll();if(wantListen&&!speaking&&!busy)resumeRec();}});

voiceOn=ls.g('alfred_voice')!=='0';neuralVoice=false;autoVis=ls.g('alfred_autovis')!=='0';voiceLanguage=['en-IN','hi-IN','auto'].includes(ls.g('alfred_voice_language'))?ls.g('alfred_voice_language'):'auto';$('voiceChk').checked=voiceOn;$('neuralVoiceChk').checked=true;$('visChk').checked=autoVis;$('voiceLanguage').value=voiceLanguage;const voiceBridge=br('setVoiceLanguage');if(voiceBridge)voiceBridge.setVoiceLanguage(voiceLanguage);useSearch=ls.g('alfred_search')!=='0';$('srchChk').checked=useSearch;{const m=ls.g('alfred_model');if(MODELS.indexOf(m)>=0)model=m;}$('modelSel').value=model;
const saved=ls.g('alfred_secure_pool');if(saved){const r=parseKeys(saved);if(r.k){keys=r.k;log('Loaded saved key(s) from this browser.');}}
keysUI();cloudUI();poll();codexProbe();
})();
</script>
</body>
</html>
)rawliteral";

// ==========================================
// WI-FI MANAGER (non-blocking state machine)
// ==========================================
Preferences prefs;
enum NetState : uint8_t { NS_CONNECTING, NS_CONNECTED, NS_AP_ONLY };
enum Attempt  : uint8_t { AT_IDLE, AT_TRYING, AT_OK, AT_FAILED };
enum Deferred : uint8_t { DF_NONE, DF_USER, DF_FORGET };
struct Cred { char ssid[33]; char pass[65]; bool useStatic; };

Cred     cand[3];
uint8_t  candN = 0, candI = 0;
NetState netState = NS_CONNECTING;
Attempt  attempt = AT_IDLE;
Deferred deferred = DF_NONE;
Cred     userCred;
bool     userPending = false, apOn = false;
char     attemptErr[48] = "";
uint32_t netT0 = 0, connT0 = 0, lostT0 = 0, deferAt = 0;

static bool loadSaved(Cred &c) {
  prefs.begin("alfred", false);
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  prefs.end();
  if (!s.length()) return false;
  strlcpy(c.ssid, s.c_str(), sizeof(c.ssid));
  strlcpy(c.pass, p.c_str(), sizeof(c.pass));
  c.useStatic = false;
  return true;
}
static void saveCred(const Cred &c) {
  prefs.begin("alfred", false);
  prefs.putString("ssid", c.ssid);
  prefs.putString("pass", c.pass);
  prefs.end();
}
static void forgetCred() {
  prefs.begin("alfred", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

static void buildCandidates() {
  candN = 0;
  const char *fbS = FALLBACK_SSID;
  const char *fbP = FALLBACK_PASS;
  Cred c;
  bool haveSaved = loadSaved(c);
  if (haveSaved) cand[candN++] = c;
  if (fbS[0]) {
    if (haveSaved && strcmp(c.ssid, fbS) == 0) {
      cand[0].useStatic = USE_STATIC_IP;          // same network as fallback: keep v1 static IP
    } else {
      Cred f;
      strlcpy(f.ssid, fbS, sizeof(f.ssid));
      strlcpy(f.pass, fbP, sizeof(f.pass));
      f.useStatic = USE_STATIC_IP;
      cand[candN++] = f;
    }
  }
}

static void startCandidate() {
  if (mdnsStarted) { MDNS.end(); mdnsStarted = false; }
  Cred &c = cand[candI];
  if (c.useStatic) {
    if (!WiFi.config(localIP, gatewayIP, subnetMask, primaryDNS, secondaryDNS)) {
      Serial.println("[WIFI] Static IP config failed; using DHCP.");
    }
  } else {
    IPAddress z(0, 0, 0, 0);
    WiFi.config(z, z, z);                          // back to DHCP
  }
  WiFi.disconnect(false, false);
  WiFi.begin(c.ssid, c.pass[0] ? c.pass : nullptr);
  netState = NS_CONNECTING;
  netT0 = millis();
  Serial.printf("[WIFI] Connecting to \"%s\" (%s)...\n", c.ssid, c.useStatic ? "static IP" : "DHCP");
}

static void startAP() {
  if (!apOn) {
    WiFi.mode(WIFI_AP_STA);
    apOn = WiFi.softAP(SETUP_AP_SSID, setupApPass);
    Serial.printf("[WIFI] Setup AP \"%s\" %s at %s\n", SETUP_AP_SSID, apOn ? "up" : "FAILED",
                  WiFi.softAPIP().toString().c_str());
    if (apOn) Serial.printf("[WIFI] Recovery password (unique to this board): %s\n", setupApPass);
  }
  netState = NS_AP_ONLY;
  netT0 = millis();
}

static void stopAP() {
  if (apOn) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apOn = false;
    Serial.println("[WIFI] Setup AP closed.");
  }
}

static void onConnected() {
  netState = NS_CONNECTED;
  connT0 = millis();
  lostT0 = 0;
  WiFi.setSleep(false);                            // lower latency for the camera feed
  if (!mdnsStarted) {
    mdnsStarted = MDNS.begin("alfred");
    if (!mdnsStarted) Serial.println("[WIFI] mDNS unavailable; use the DHCP address shown below.");
    else Serial.println("[WIFI] Dashboard alias: http://alfred.local/");
  }
  if (userPending) {
    saveCred(userCred);
    userPending = false;
    attempt = AT_OK;
  }
  Serial.print("[WIFI] Connected. IP: ");
  Serial.println(WiFi.localIP());
}

static void connectFailed() {
  wl_status_t st = WiFi.status();
  const char *why = (st == WL_NO_SSID_AVAIL) ? "network not found"
                  : (st == WL_CONNECT_FAILED) ? "refused (check the password)"
                  : "timed out";
  Serial.printf("[WIFI] Attempt failed: %s\n", why);
  if (userPending) {                               // user-supplied credentials failed: report, then fall back
    userPending = false;
    attempt = AT_FAILED;
    strlcpy(attemptErr, why, sizeof(attemptErr));
    buildCandidates();
    candI = 0;
    if (candN) startCandidate(); else startAP();
    return;
  }
  if (++candI < candN) startCandidate(); else startAP();
}

static void netBegin() {
  buildCandidates();
  candI = 0;
  if (candN) startCandidate(); else startAP();
}

static void netTick() {
  uint32_t now = millis();
  if (deferred != DF_NONE && (int32_t)(now - deferAt) >= 0) {   // deferred so the HTTP reply is delivered first
    Deferred d = deferred;
    deferred = DF_NONE;
    if (d == DF_USER) {
      attempt = AT_TRYING;
      attemptErr[0] = 0;
      userPending = true;
      cand[0] = userCred;
      candN = 1;
      candI = 0;
      startCandidate();
    } else {
      forgetCred();
      userPending = false;
      WiFi.disconnect(false, false);
      buildCandidates();
      candI = 0;
      startAP();
    }
    return;
  }
  wl_status_t st = WiFi.status();
  switch (netState) {
    case NS_CONNECTING:
      if (st == WL_CONNECTED) onConnected();
      else if (now - netT0 > CONNECT_TIMEOUT_MS) connectFailed();
      break;
    case NS_CONNECTED:
      if (st != WL_CONNECTED) {
        if (!lostT0) lostT0 = now;
        else if (now - lostT0 > 10000UL) {         // let the stack auto-reconnect first, then take over
          lostT0 = 0;
          Serial.println("[WIFI] Link lost; reconnecting.");
          buildCandidates();
          candI = 0;
          if (candN) startCandidate(); else startAP();
        }
      } else {
        lostT0 = 0;
        if (apOn && now - connT0 > 60000UL) stopAP();
      }
      break;
    case NS_AP_ONLY:
      if (st == WL_CONNECTED) onConnected();
      else if (now - netT0 > AP_RETRY_MS && WiFi.softAPgetStationNum() == 0) {
        buildCandidates();
        candI = 0;
        if (candN) startCandidate(); else netT0 = now;
      }
      break;
  }
}

// ==========================================
// HELPERS
// ==========================================
static void jsonEsc(const char *s, char *o, size_t n) {
  size_t j = 0;
  for (size_t i = 0; s[i] && j + 8 < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') { o[j++] = '\\'; o[j++] = c; }
    else if (c < 0x20) { j += snprintf(o + j, n - j, "\\u%04x", c); }
    else o[j++] = c;
  }
  o[j] = 0;
}
static bool hasCtrl(const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c == 0x7f) return true;
  }
  return false;
}
static const char *modeName() {
  return netState == NS_CONNECTED ? "sta" : (apOn ? "ap" : "connecting");
}

// ==========================================
// WEB SERVER HANDLERS
// ==========================================
void handleRootDashboard() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", HTML_DASHBOARD);
}

void handleCameraSnapshot() {
  if (!cameraOK) { server.send(503, "text/plain", "Camera unavailable"); return; }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { server.send(503, "text/plain", "Frame acquisition failed"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  WiFiClient client = server.client();
  size_t len = fb->len;
  size_t sent = client.write(fb->buf, len);
  esp_camera_fb_return(fb);                        // always release the frame buffer; do not touch fb afterwards
  if (sent != len) Serial.println("[CAM] Short write (client left early).");
}

void handleStatusPayload() {
  Telemetry s;
  xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  s = tel;
  xSemaphoreGive(telemetryMutex);

  uint32_t now = millis();
  char t[16] = "null", h[16] = "null", d[16] = "null", ip[20] = "";
  if (s.tMs && now - s.tMs < TEL_FRESH_MS) snprintf(t, sizeof(t), "%.1f", s.t);
  if (s.hMs && now - s.hMs < TEL_FRESH_MS) snprintf(h, sizeof(h), "%.1f", s.h);
  if (s.dMs && now - s.dMs < 3000UL)       snprintf(d, sizeof(d), "%.1f", s.d);
  if (netState == NS_CONNECTED) strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  else if (apOn)                strlcpy(ip, WiFi.softAPIP().toString().c_str(), sizeof(ip));

  char out[300];
  snprintf(out, sizeof(out),
           "{\"temperature\":%s,\"humidity\":%s,\"distance\":%s,\"flame\":%s,"
           "\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"uptime\":%lu,\"heap\":%u,\"cam\":%s}",
           t, h, d, s.flame ? "true" : "false", modeName(), ip,
           netState == NS_CONNECTED ? WiFi.RSSI() : 0,
           (unsigned long)(now / 1000UL), (unsigned)ESP.getFreeHeap(), cameraOK ? "true" : "false");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void handleNetStatus() {
  char ss[72], out[320];
  jsonEsc(netState == NS_CONNECTED ? WiFi.SSID().c_str() : "", ss, sizeof(ss));
  const char *st = netState == NS_CONNECTED ? "connected" : (netState == NS_CONNECTING ? "connecting" : "setup");
  const char *at = attempt == AT_TRYING ? "trying" : attempt == AT_OK ? "ok" : attempt == AT_FAILED ? "failed" : "idle";
  snprintf(out, sizeof(out),
           "{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"apIp\":\"%s\",\"attempt\":\"%s\",\"err\":\"%s\"}",
           st, ss, netState == NS_CONNECTED ? WiFi.localIP().toString().c_str() : "",
           apOn ? WiFi.softAPIP().toString().c_str() : "", at, attemptErr);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void handleScanStart() {
  if (netState == NS_CONNECTING || deferred != DF_NONE) {
    server.send(409, "application/json", "{\"err\":\"busy\"}");
    return;
  }
  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false, false, 300);    // async: never blocks the web server
  }
  server.send(202, "application/json", "{\"state\":\"running\"}");
}

void handleScanResult() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { server.send(200, "application/json", "{\"state\":\"running\"}"); return; }
  if (n < 0)                  { server.send(200, "application/json", "{\"state\":\"idle\"}");    return; }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"state\":\"done\",\"networks\":[");
  int lim = n > 20 ? 20 : n;
  bool first = true;
  for (int i = 0; i < lim; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    char e[72], b[140];
    jsonEsc(ssid.c_str(), e, sizeof(e));
    snprintf(b, sizeof(b), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}", first ? "" : ",", e, (int)WiFi.RSSI(i),
             WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false");
    server.sendContent(b);
    first = false;
  }
  server.sendContent("]}");
  server.sendContent("");                          // terminate chunked response
  WiFi.scanDelete();
}

void handleNetConnect() {
  if (attempt == AT_TRYING || deferred != DF_NONE) {
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"Another attempt is in progress\"}");
    return;
  }
  String s = server.arg("ssid");
  String p = server.arg("pass");
  if (s.length() < 1 || s.length() > 32 || hasCtrl(s)) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"Network name must be 1-32 characters\"}");
    return;
  }
  if ((p.length() > 0 && (p.length() < 8 || p.length() > 63)) || hasCtrl(p)) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"Password must be 8-63 characters, or empty for an open network\"}");
    return;
  }
  strlcpy(userCred.ssid, s.c_str(), sizeof(userCred.ssid));
  strlcpy(userCred.pass, p.c_str(), sizeof(userCred.pass));
  userCred.useStatic = false;
  attempt = AT_TRYING;
  attemptErr[0] = 0;
  deferred = DF_USER;
  deferAt = millis() + 400;
  server.send(202, "application/json", "{\"ok\":true}");   // never echo the password
}

void handleNetForget() {
  deferred = DF_FORGET;
  deferAt = millis() + 400;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ==========================================
// SENSOR WORKER TASK (CORE 0, same as v1)
// ==========================================
void hardwareExecutionWorker(void *parameter) {
  uint32_t lastDht = 0;
  uint8_t flameCnt = 0;
  for (;;) {
    uint32_t now = millis();
    if (lastDht == 0 || now - lastDht >= 3000) {
      float rawT = dht.readTemperature();
      float rawH = dht.readHumidity();
      xSemaphoreTake(telemetryMutex, portMAX_DELAY);
      if (!isnan(rawT)) { tel.t = rawT; tel.tMs = millis(); }
      if (!isnan(rawH)) { tel.h = rawH; tel.hMs = millis(); }
      xSemaphoreGive(telemetryMutex);
      lastDht = now;
    }

    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 25000);          // 25 ms timeout, as in v1
    float dist = (duration * 0.0343f) / 2.0f;

    if (digitalRead(FLAME_PIN) == LOW) { if (flameCnt < 3) flameCnt++; } else flameCnt = 0;   // 2-read debounce

    xSemaphoreTake(telemetryMutex, portMAX_DELAY);
    if (duration > 0) { tel.d = dist; tel.dMs = millis(); }  // no echo => no fresh reading (v1 reported a fake 400 cm)
    tel.flame = (flameCnt >= 2);
    xSemaphoreGive(telemetryMutex);

    vTaskDelay(pdMS_TO_TICKS(400));
  }
}

// ==========================================
// CAMERA INIT
// ==========================================
static bool initCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));              // v1 left newer struct fields uninitialised
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;         // with 2 buffers, avoids serving stale frames
  } else {
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }
  for (int a = 0; a < 2; a++) {
    esp_err_t err = esp_camera_init(&config);
    if (err == ESP_OK) return true;
    Serial.printf("[CAM] Init failed (attempt %d): 0x%x\n", a + 1, err);
    esp_camera_deinit();
    delay(200);
  }
  return false;
}

// ==========================================
// SETUP / LOOP
// ==========================================
static void initSetupApPassword() {
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(setupApPass, sizeof(setupApPass), "%06lX%06lX",
           (unsigned long)((chipId >> 24) & 0xFFFFFF),
           (unsigned long)(chipId & 0xFFFFFF));
}

void setup() {
#if DISABLE_BROWNOUT
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif
  Serial.begin(115200);
  Serial.println("\n[SYSTEM] ALFRED v2 starting...");
  initSetupApPassword();

  telemetryMutex = xSemaphoreCreateMutex();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(FLAME_PIN, INPUT);
  dht.begin();

  WiFi.persistent(false);                          // we manage credentials ourselves (NVS "alfred")
  WiFi.mode(WIFI_STA);                             // bring up the network stack before the server

  cameraOK = initCamera();
  if (!cameraOK) Serial.println("[CAM] Camera unavailable. Dashboard and sensors still start.");

  server.on("/", HTTP_GET, handleRootDashboard);
  server.on("/camera.jpg", HTTP_GET, handleCameraSnapshot);
  server.on("/status", HTTP_GET, handleStatusPayload);
  server.on("/net/status", HTTP_GET, handleNetStatus);
  server.on("/net/scan", HTTP_GET, handleScanResult);
  server.on("/net/scan", HTTP_POST, handleScanStart);
  server.on("/net/connect", HTTP_POST, handleNetConnect);
  server.on("/net/forget", HTTP_POST, handleNetForget);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound(handleNotFound);
  server.begin();

  xTaskCreatePinnedToCore(hardwareExecutionWorker, "SensorWorker", 4096, NULL, 1, NULL, 0);
  Serial.println("[SYSTEM] Sensor worker started.");

  netBegin();                                      // non-blocking; loop() drives it
}

void loop() {
  server.handleClient();
  netTick();
  delay(1);
}
