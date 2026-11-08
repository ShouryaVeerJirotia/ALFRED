// ============================================================================
// ALFRED AI  -  AlfredsFirmware_v2.ino
// Target : AI Thinker ESP32-CAM (OV2640), Arduino IDE, ESP32 Arduino core 2.0.x / 3.x
// STATUS : NOT compiled or hardware-tested by the author of this revision.
//          See AlfredAudit_v2.md for the audit, test checklist and known risks.
// ============================================================================
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
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

// Recovery / setup access point (opens automatically when Wi-Fi cannot connect). CHANGE THE PASSWORD.
#define SETUP_AP_SSID   "ALFRED-Setup"
#define SETUP_AP_PASS   "REPLACE_WITH_A_UNIQUE_PASSWORD"           // WPA2 needs >= 8 characters
static_assert(sizeof(SETUP_AP_PASS) >= 9, "SETUP_AP_PASS must be at least 8 characters");

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
<meta http-equiv="Content-Security-Policy" content="default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self' https://generativelanguage.googleapis.com">
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
textarea{height:84px;resize:vertical}
summary{cursor:pointer;font-size:.8rem;letter-spacing:.14em;text-transform:uppercase;color:var(--mut)}
#log{max-height:130px;overflow-y:auto;font:12px/1.5 ui-monospace,Menlo,Consolas,monospace;color:#9fdcec;margin-top:8px}
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
  <div class="cap" id="cap" aria-live="polite">Type a request, tap the orb, or press the mic and say “Alfred”.</div>
  <div class="alert" id="alert" role="alert" hidden></div>
  <div class="card" id="confirm" hidden style="width:100%">
   <div id="cfText"></div>
   <div class="row"><button class="btn" id="cfOk">Allow</button><button class="btn warn" id="cfNo">Cancel</button></div>
  </div>
  <div class="bar">
   <input type="text" id="cmd" maxlength="500" placeholder="Ask ALFRED…" aria-label="Message to ALFRED" autocomplete="off">
   <button class="ib" id="mic" aria-pressed="false" aria-label="Toggle wake-word listening" title="Listen for “Alfred”">🎙</button>
   <button class="go" id="send">Send</button>
  </div>
 </div>
 <details class="card"><summary>Phone actions</summary>
  <div class="row"><input type="text" id="musicTrack" maxlength="120" placeholder="Track name" aria-label="Track name"><button class="btn" id="bMusic">Play</button></div>
  <div class="row"><input type="text" id="navDest" maxlength="120" placeholder="Destination" aria-label="Destination"><button class="btn" id="bNav">Drive</button></div>
  <div class="row"><input type="tel" id="phone" maxlength="20" placeholder="Phone number" aria-label="Phone number"><input type="text" id="smsBody" maxlength="300" placeholder="SMS text" aria-label="SMS text"></div>
  <div class="row"><button class="btn" id="bCall">Call</button><button class="btn" id="bSms">Send text</button></div>
 </details>
 <details class="card"><summary>Activity log</summary><div id="log"></div></details>
</section>

<section class="view" id="v-vision">
 <h2>Vision</h2>
 <div class="camwrap"><img id="cam" alt="Live camera frame"><div id="camMsg">Connecting to camera…</div></div>
 <div class="row"><button class="btn" id="visBtn">Describe what you see</button></div>
 <p class="note">Frames are 320×240 and are sent to Google Gemini only when you ask a vision question. The live feed runs only while this tab is open.</p>
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
 <h2>Gemini API keys</h2>
 <div class="card">
  <textarea id="keysArea" placeholder="Paste Gemini API key(s), one per line" aria-label="Gemini API keys" autocomplete="off" spellcheck="false"></textarea>
  <label class="chk"><input type="checkbox" id="remember" checked> Remember on this browser</label>
  <p class="note">Saved keys are stored unencrypted in this browser’s localStorage, and requests go straight from your browser to Google. Anyone with access to this browser profile can read them. Use a restricted key with a spending cap.</p>
  <div class="row"><button class="btn" id="saveKeys">Save and engage</button><button class="btn warn" id="clearKeys">Flush keys</button></div>
  <div class="sub" id="keysInfo">No key loaded.</div>
 </div>
 <h2>Behaviour</h2>
 <div class="card">
  <label class="chk"><input type="checkbox" id="voiceChk" checked> Speak replies aloud</label>
  <label class="chk"><input type="checkbox" id="visChk" checked> Attach a camera frame when a request sounds visual</label>
  <label class="chk"><input type="checkbox" id="srchChk"> Use Google Search for live answers (may need a paid Gemini plan)</label>
  <div class="row"><label for="modelSel" class="sub">Model</label><select id="modelSel" style="flex:1;min-height:44px;background:#05070d;color:var(--fg);border:1px solid var(--line);border-radius:10px;padding:0 10px">
   <option value="gemini-3.5-flash-lite">gemini-3.5-flash-lite (best for free keys)</option>
   <option value="gemini-3.1-flash-lite">gemini-3.1-flash-lite</option>
   <option value="gemini-3.6-flash">gemini-3.6-flash (small free quota)</option></select></div>
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
const MODELS=['gemini-3.5-flash-lite','gemini-3.1-flash-lite','gemini-3.6-flash'];
let model=MODELS[0],useSearch=false;
const SR=window.SpeechRecognition||window.webkitSpeechRecognition;
const ls={g:k=>{try{return localStorage.getItem(k);}catch(e){return null;}},s:(k,v)=>{try{localStorage.setItem(k,v);}catch(e){}},d:k=>{try{localStorage.removeItem(k);}catch(e){}}};
const br=f=>{const a=window.AlfredAndroid;return a&&typeof a[f]==='function'?a:null;};
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let keys=[],ki=0,hist=[],busy=false,reqId=0,voiceOn=true,autoVis=true,speaking=false,spTok=0,greeted=false,cur='assistant',last={};
let rec=null,wantListen=false,awake=false,awT=0,rsT=0,recFail=0,recRun=false,cfT=0,tt=0;
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
const LBL={idle:'IDLE',standby:'STANDBY · SAY “ALFRED”',listening:'LISTENING',thinking:'THINKING',speaking:'SPEAKING'};
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
function doMusic(){const t=$('musicTrack').value.trim().slice(0,120);if(!t)return;log('Opening Spotify…');
 const b=br('playSpotify');if(b){b.playSpotify(t);return;}
 const e=encodeURIComponent(t);openUri('spotify:search:'+e,'https://open.spotify.com/search/'+e);}
function doNav(){const d=$('navDest').value.trim().slice(0,120);if(!d)return;log('Opening navigation…');
 const b=br('navigate');if(b){b.navigate(d);return;}
 const e=encodeURIComponent(d);openUri('google.navigation:q='+e+'&mode=d','https://www.google.com/maps/dir/?api=1&destination='+e+'&travelmode=driving');}
function doCall(){const p=$('phone').value.trim();if(!p)return;if(!phoneOK(p)){toast('Enter a valid phone number (digits, +, spaces, dashes).');return;}
 log('Placing call…');const b=br('callNumber');if(b){b.callNumber(p);return;}window.location.href='tel:'+p.replace(/[\s()-]/g,'');}
function doSms(){const p=$('phone').value.trim(),m=$('smsBody').value.trim().slice(0,300);if(!p)return;if(!phoneOK(p)){toast('Enter a valid phone number (digits, +, spaces, dashes).');return;}
 log('Opening messages…');const b=br('sendSms');if(b){b.sendSms(p,m);return;}window.location.href='sms:'+p.replace(/[\s()-]/g,'')+'?&body='+encodeURIComponent(m);}
function cfClose(){$('confirm').hidden=true;clearTimeout(cfT);cfAct=null;}
let cfAct=null;
function confirmAct(kind,phone,body){
 if(!phoneOK(phone)){log('Ignored an invalid phone number in the model reply.');return;}
 $('phone').value=phone;if(kind==='sms')$('smsBody').value=body.slice(0,300);
 put('cfText','Gemini wants to '+(kind==='call'?'call ':'text ')+phone+(kind==='sms'?': “'+body.slice(0,120)+'”':'')+'. Allow?');
 cfAct=kind==='call'?doCall:doSms;$('confirm').hidden=false;clearTimeout(cfT);cfT=setTimeout(cfClose,20000);}
function infer(raw){const p=(raw||'').trim(),lo=p.toLowerCase();
 const pm=lo.match(/^(?:please\s+)?(?:play\s+|(?:start|open)\s+(?:music|song|spotify)\s+)(?:(?:music|song|spotify)\s+)?(.+)$/);
 if(pm&&!/\bmaps?\b/.test(lo)){$('musicTrack').value=p.slice(lo.lastIndexOf(pm[1])).trim();setTimeout(doMusic,800);return true;}
 const nm=lo.match(/\b(?:navigate|drive|take me|route|directions|maps?)\s+(?:to\s+)?(.+)/);
 if(nm){$('navDest').value=p.slice(lo.lastIndexOf(nm[1])).trim();setTimeout(doNav,800);return true;}
 return false;}

/* ---------- Speech output ---------- */
function estMs(t){const w=(t||'').trim().split(/\s+/).filter(Boolean).length;return Math.max(1400,Math.min(9000,650+w*420));}
function settle(){if(busy)setState('thinking');else setState(wantListen?(awake?'listening':'standby'):'idle');resumeRec();}
function speakDone(){if(!speaking)return;speaking=false;settle();}
function speak(t){
 if(!t||!voiceOn){settle();return;}
 const tok=++spTok;speaking=true;setState('speaking');pauseRec();
 const b=br('speak');
 if(b){try{b.speak(t);}catch(e){}setTimeout(()=>{if(tok===spTok)speakDone();},estMs(t));return;}
 if(!('speechSynthesis' in window)){speakDone();return;}
 try{window.speechSynthesis.cancel();
  const u=new SpeechSynthesisUtterance(t),v=window.speechSynthesis.getVoices();
  const bv=v.find(a=>a.lang==='en-GB'&&/male/i.test(a.name))||v.find(a=>a.lang==='en-GB'&&a.name.includes('Daniel'))||v.find(a=>a.lang.startsWith('en-GB'))||null;
  if(bv)u.voice=bv;u.rate=.95;u.pitch=.75;
  u.onend=u.onerror=()=>{if(tok===spTok)speakDone();};u.onboundary=()=>Orb.pulse();
  window.speechSynthesis.speak(u);}
 catch(e){speakDone();}}

/* ---------- Speech input ---------- */
function micMsg(m){log(m);toast(m);}
function micBlocked(){wantListen=false;awake=false;$('mic').setAttribute('aria-pressed','false');
 const m=window.isSecureContext?'Microphone permission was denied. Allow it in your browser’s site settings, then tap the mic again.':'The browser blocked the microphone for this plain-HTTP page. Accept the permission prompt if it appears, use the text box, or open ALFRED in the Android app.';
 alertSet('mic',1,m);settle();}
function ensureRec(){if(rec)return;rec=new SR();rec.continuous=false;rec.interimResults=false;rec.lang='en-US';
 rec.onstart=()=>{recRun=true;};
 rec.onspeechstart=rec.onsoundstart=()=>{Orb.pulse();if(awake&&!busy&&!speaking){clearTimeout(awT);setState('listening');}};
 rec.onresult=e=>{recFail=0;const raw=((e.results[0]&&e.results[0][0]&&e.results[0][0].transcript)||'').trim();if(!raw)return;
  const lo=raw.toLowerCase(),i=lo.indexOf('alfred');log('Heard: '+raw);
  if(i>=0){const cmd=raw.substring(i+6).replace(/^[\s,.:;!?-]+/,'').trim();if(cmd){sleepAwake();ask(cmd);}else awaken();}
  else if(awake){sleepAwake();ask(raw);}};
 rec.onerror=e=>{const c=e.error;if(c==='no-speech'||c==='aborted')return;
  if(c==='not-allowed'||c==='service-not-allowed'){micBlocked();return;}
  recFail++;log('Mic error: '+c);
  if(c==='audio-capture')micMsg('No microphone found, or it is in use by another app.');
  else if(c==='network')micMsg('Speech recognition needs an internet connection.');};
 rec.onend=()=>{recRun=false;if(wantListen&&!speaking&&!busy){clearTimeout(rsT);rsT=setTimeout(startRec,Math.min(5000,250*Math.pow(2,Math.min(recFail,5))));}};}
function startRec(){if(!wantListen||recRun||speaking||busy||!rec)return;try{rec.start();}catch(e){}}
function pauseRec(){clearTimeout(rsT);try{if(rec)rec.abort();}catch(e){}}
function resumeRec(){if(wantListen){clearTimeout(rsT);rsT=setTimeout(startRec,300);}}
function awaken(){awake=true;setState('listening');log('Awake. Waiting for your command…');clearTimeout(awT);
 awT=setTimeout(()=>{if(awake){awake=false;log('Back to standby (silence).');if(!busy&&!speaking)settle();}},6000);}
function sleepAwake(){awake=false;clearTimeout(awT);}
function startListening(quiet){
 if(!SR){micMsg('Speech recognition isn’t supported in this browser. Try Chrome or Edge, or type your request.');return;}
 alertClear('mic');wantListen=true;recFail=0;ensureRec();$('mic').setAttribute('aria-pressed','true');
 if(!window.isSecureContext&&!quiet)log('Note: on plain http:// pages the browser may ask for microphone permission repeatedly.');
 if(!greeted&&!quiet){greeted=true;speak('ALFRED online. Standing by, the user.');}else{settle();}}
function stopListening(){wantListen=false;sleepAwake();pauseRec();$('mic').setAttribute('aria-pressed','false');settle();}
function awakenNow(){if(busy||speaking)return;if(!wantListen){greeted=true;startListening(true);}if(wantListen)awaken();}

/* ---------- Gemini ---------- */
const VIS=/\b(see|look|looking|view|watch|notice|holding|wearing|colou?r|face|person|camera|photo|picture|snapshot|eyes)\b|\bread (this|that|the|what)|what(?:’s|'s| is) (this|that|in)|who is (this|that|there)|in front of (me|you)/i;
async function gem(key,contents,sys){
 const ac=new AbortController(),to=setTimeout(()=>ac.abort(),25000);
 try{const r=await fetch(API+model+':generateContent',{method:'POST',headers:{'Content-Type':'application/json','x-goog-api-key':key},signal:ac.signal,
   body:JSON.stringify(Object.assign({contents:contents,systemInstruction:{parts:[{text:sys}]}},useSearch?{tools:[{google_search:{}}]}:{}))});
  let d=null;try{d=await r.json();}catch(e){}
  if(!r.ok||(d&&d.error)){const er=(d&&d.error)||{},m=String(er.message||('HTTP '+r.status)).split(key).join('***').slice(0,240);
   const hint=r.status===429?' Keys from the same Google project share one quota, so new keys do not reset it. Try the Flash-Lite model, turn off Google Search, or wait.':((r.status===401||r.status===403)?' (Check this is a Gemini API key from AI Studio; if it keeps failing, create a new key.)':'');
   return{ok:false,rotate:r.status===429||r.status===401||r.status===403||/API key/i.test(m)||er.status==='RESOURCE_EXHAUSTED',msg:m+hint};}
  const cd=d&&d.candidates&&d.candidates[0],ps=cd&&cd.content&&cd.content.parts;
  const t=ps?ps.map(p=>p.text||'').join('').trim():'';
  if(!t){const why=(cd&&cd.finishReason)||(d&&d.promptFeedback&&d.promptFeedback.blockReason)||'';return{ok:false,rotate:false,msg:'Empty or blocked response'+(why?' ('+why+')':'')+'.'};}
  return{ok:true,text:t};}
 catch(e){return{ok:false,rotate:false,msg:e.name==='AbortError'?'Timed out waiting for Gemini.':'Network error. Is this device connected to the internet?'};}
 finally{clearTimeout(to);}}
function fail(text,msg){busy=false;caption(text,'Request failed: '+msg);log('Gemini error: '+msg);toast(msg);settle();}
async function ask(text,forceVis){
 text=(text||'').trim().slice(0,500);if(!text)return;
 if(busy){toast('ALFRED is still working on the previous request.');return;}
 if(!keys.length){toast('Add a Gemini API key in Settings first.');go('settings');return;}
 busy=true;const id=++reqId;sleepAwake();pauseRec();setState('thinking');caption(text,'');
 try{
  const parts=[{text:text}];
  if(forceVis||(autoVis&&VIS.test(text))){const b=await grabFrame();if(id!==reqId)return;
   if(b){parts.push({inlineData:{mimeType:'image/jpeg',data:b}});log('Camera frame attached.');}
   else{log('Camera frame unavailable. Asking without an image.');toast('Camera frame unavailable. Asking without an image.');}}
  const sys='You are Alfred. '+new Date().toDateString()+'.\nRoom: '+(last.temperature!=null?last.temperature.toFixed(1)+' °C':'unknown')+', Range: '+(last.distance!=null?Math.round(last.distance)+' cm':'unknown')+'.\nKeep replies < 20 words.\nFor music append exactly [INTENT:PLAY,data:TRACK].\nFor maps/driving append exactly [INTENT:NAV,data:DESTINATION].\nFor calls append exactly [INTENT:CALL,data:PHONE].\nFor SMS append exactly [INTENT:SMS,data:PHONE,body:MESSAGE].';
  const contents=hist.concat([{role:'user',parts:parts}]);
  let out=null,err='';
  for(let n=0;n<keys.length;n++){
   const r=await gem(keys[ki],contents,sys);if(id!==reqId)return;
   if(r.ok){out=r.text;break;}
   err=r.msg;
   if(r.rotate&&keys.length>1){log('Key #'+(ki+1)+' was rejected or rate-limited. Switching key.');ki=(ki+1)%keys.length;continue;}
   break;}
  if(out===null){fail(text,err||'No response.');return;}
  const reply=out.replace(/[*#`]/g,'');let x,did=false;
  if((x=reply.match(/\[INTENT:NAV,data:(.*?)\]/))){$('navDest').value=x[1].trim().slice(0,120);setTimeout(doNav,1500);did=true;}
  if((x=reply.match(/\[INTENT:PLAY,data:(.*?)\]/))){$('musicTrack').value=x[1].trim().slice(0,120);setTimeout(doMusic,1500);did=true;}
  if((x=reply.match(/\[INTENT:CALL,data:(.*?)\]/))){confirmAct('call',x[1].trim());did=true;}
  if((x=reply.match(/\[INTENT:SMS,data:(.*?),body:(.*?)\]/))){confirmAct('sms',x[1].trim(),x[2].trim());did=true;}
  if(!did)infer(text);
  const spoken=reply.replace(/\[INTENT:[^\]]*\]/g,'').trim();
  hist.push({role:'user',parts:[{text:text}]},{role:'model',parts:[{text:reply}]});
  hist=hist.slice(-6);while(hist.length&&hist[0].role!=='user')hist.shift();
  caption(text,spoken||'Done.');log('ALFRED: '+spoken);
  busy=false;speak(spoken);
 }catch(e){if(id===reqId)fail(text,'Unexpected error.');}
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

/* ---------- Wiring ---------- */
function sendText(){const f=$('cmd'),v=f.value.trim();if(!v)return;if(busy){toast('ALFRED is still working on the previous request.');return;}f.value='';ask(v);}
['assistant','vision','sensors','network','settings'].forEach(n=>$('t-'+n).addEventListener('click',()=>go(n)));
$('send').addEventListener('click',sendText);
$('cmd').addEventListener('keydown',e=>{if(e.key==='Enter')sendText();});
$('mic').addEventListener('click',()=>{if(wantListen)stopListening();else startListening();});
$('orbBox').addEventListener('click',awakenNow);
$('orbBox').addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();awakenNow();}});
$('bMusic').addEventListener('click',doMusic);$('bNav').addEventListener('click',doNav);
$('bCall').addEventListener('click',doCall);$('bSms').addEventListener('click',doSms);
$('cfOk').addEventListener('click',()=>{const a=cfAct;cfClose();if(a)a();});$('cfNo').addEventListener('click',cfClose);
$('visBtn').addEventListener('click',()=>{go('assistant');ask('Describe what you see in front of the camera, briefly.',true);});
$('scanBtn').addEventListener('click',scan);$('connBtn').addEventListener('click',netConnect);$('forgetBtn').addEventListener('click',netForget);
$('saveKeys').addEventListener('click',saveKeys);$('clearKeys').addEventListener('click',flushKeys);
$('voiceChk').addEventListener('change',e=>{voiceOn=e.target.checked;ls.s('alfred_voice',voiceOn?'1':'0');if(!voiceOn){try{window.speechSynthesis.cancel();}catch(x){}}});
$('srchChk').addEventListener('change',e=>{useSearch=e.target.checked;ls.s('alfred_search',useSearch?'1':'0');});
$('modelSel').addEventListener('change',e=>{model=e.target.value;ls.s('alfred_model',model);log('Model: '+model);});
$('visChk').addEventListener('change',e=>{autoVis=e.target.checked;ls.s('alfred_autovis',autoVis?'1':'0');});
document.addEventListener('visibilitychange',()=>{if(!document.hidden){clearTimeout(pollT);poll();if(wantListen&&!speaking&&!busy)resumeRec();}});

voiceOn=ls.g('alfred_voice')!=='0';autoVis=ls.g('alfred_autovis')!=='0';$('voiceChk').checked=voiceOn;$('visChk').checked=autoVis;useSearch=ls.g('alfred_search')==='1';$('srchChk').checked=useSearch;{const m=ls.g('alfred_model');if(MODELS.indexOf(m)>=0)model=m;}$('modelSel').value=model;
const saved=ls.g('alfred_secure_pool');if(saved){const r=parseKeys(saved);if(r.k){keys=r.k;log('Loaded saved key(s) from this browser.');}}
keysUI();poll();
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
    apOn = WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS);
    Serial.printf("[WIFI] Setup AP \"%s\" %s at %s\n", SETUP_AP_SSID, apOn ? "up" : "FAILED",
                  WiFi.softAPIP().toString().c_str());
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
void setup() {
#if DISABLE_BROWNOUT
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif
  Serial.begin(115200);
  Serial.println("\n[SYSTEM] ALFRED v2 starting...");

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
