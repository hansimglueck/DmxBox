#include "fixture_web.h"
#include "fixtures.h"
#include "fader_ui.h"
#include "system.h"
#include "cues.h"
#include "cue_web.h"
#ifdef HAS_LORA
#include "sync.h"
#include "sync_web.h"
#endif
#include <ArduinoJson.h>
#include <esp_rom_crc.h>

extern uint8_t data[];

// ---------------- shared page frame ----------------

static const char PAGE_CSS[] PROGMEM = R"CSS(
:root{--bg:#111317;--panel:#1a1d22;--line:#2c3038;--text:#d4d7dc;--muted:#878d96;--accent:#4a9eed;--danger:#d9534f;--warn:#d4a72c;--ok:#5cb85c}
*{box-sizing:border-box}
html{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;font-size:16px}
body{background:var(--bg);color:var(--text);margin:0}
header{display:flex;align-items:center;justify-content:space-between;padding:16px 18px;border-bottom:1px solid var(--line);background:var(--panel)}
header .brand{display:flex;align-items:center;gap:12px;color:var(--text);font-size:22px;font-weight:600;letter-spacing:.02em}
header .logo{display:flex;align-items:center;height:34px;color:#F2F4F6}
header .logo svg{height:34px;width:auto}
header .ctx{color:var(--muted);font-size:13px}
main{max-width:980px;margin:0 auto;padding:8px 16px 40px}
footer{border-top:1px solid var(--line);padding:14px 16px;text-align:center;font-size:14px}
footer a{color:var(--muted);margin:0 10px}
h1{font-size:21px;font-weight:600;margin:16px 0 4px}
h2{font-size:16px;font-weight:600;margin:20px 0 8px;color:var(--muted);text-transform:uppercase;letter-spacing:.05em}
a{color:var(--accent);text-decoration:none}
p{margin:8px 0}
.muted,.meta{color:var(--muted)}.meta{font-size:13px;line-height:1.5}
.mono{font-family:ui-monospace,Menlo,Consolas,monospace}
.panel{display:block;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:12px 14px;margin:10px 0;color:var(--text)}
a.panel:hover{border-color:var(--accent)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:10px 0}
.row form{display:contents}
.btn{display:inline-block;background:var(--panel);border:1px solid var(--line);color:var(--text);padding:9px 14px;font-size:15px;
 border-radius:5px;cursor:pointer;text-align:center;font-family:inherit}
.btn:hover{border-color:var(--accent)}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}
.btn.danger{border-color:var(--danger);color:var(--danger)}
.btn.grow{flex:1}
.btn.sel{border-color:var(--accent);color:var(--accent)}
.warn{color:var(--warn)}.err{color:var(--danger)}.ok{color:var(--ok)}
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px;margin:12px 0}
.tile{display:block;background:#1f2328;border:1px solid #353a43;border-radius:8px;color:var(--text);overflow:hidden;
 box-shadow:0 1px 3px rgba(0,0,0,.4)}
.tile:hover{border-color:var(--accent)}
div.tile{cursor:pointer}
.tile .name{display:block;font-weight:600;font-size:16px;padding:7px 10px 0;word-break:break-word;color:var(--text)}
.tile .meta{padding:0 10px 6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tile .meta.warn{padding-top:0;margin-top:-6px}
.chips{display:flex;gap:4px;padding:6px 8px 8px;border-top:1px solid #353a43;background:#191c21}
.chip{display:flex;flex-direction:column;flex:1 1 0;min-width:0;border:1px solid var(--line);border-radius:5px;padding:4px 6px;
 font-size:12px;color:var(--muted);background:#0d0f12;line-height:1.3}
.chip .cl{display:flex;align-items:center;gap:5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chip .cv{font-family:ui-monospace,Menlo,monospace;font-size:11px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chip.none{border-style:dashed;border-color:#262a31;background:transparent}
.chip.hid,.chips.hid{display:none}
.chip.on{border-color:var(--accent);color:var(--text)}
.chip.on .cv{color:var(--accent)}
.chip:hover{border-color:var(--accent)}
.sw{display:inline-block;width:11px;height:11px;border-radius:2px;border:1px solid #555}
.lv{display:block;height:4px;background:var(--line);border-radius:2px;margin-top:4px;overflow:hidden}
.lv i{display:block;height:100%;background:linear-gradient(90deg,#4a4a4a,#fff)}
.sbar{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}
.sbar a{flex:1 1 auto;display:flex;align-items:center;gap:6px;border:1px solid var(--line);border-radius:6px;padding:7px 12px;
 font-size:14px;color:var(--text)}
.sbar a:hover{border-color:var(--accent)}
.sbar svg{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;flex:none}
#schedp{flex:0 1 auto;color:var(--muted)}
.gfil{display:flex;gap:3px}
.gfil button{width:26px;padding:3px 0;font-size:12px;font-family:ui-monospace,Menlo,monospace;background:transparent;color:var(--muted);
 border:1px solid var(--line);border-radius:4px;cursor:pointer}
.gfil button.on{border-color:var(--accent);color:var(--accent)}
.tile.new{background:transparent;box-shadow:none;min-height:84px}
.tile.off{opacity:.45}
.tile.new{display:flex;align-items:center;justify-content:center;border-style:dashed;color:var(--muted);padding:12px}
.tiles.home{margin-top:8px;grid-template-columns:repeat(auto-fill,minmax(min(300px,100%),1fr))}
.chip .gs{display:none}.chips.narrow .gn{display:none}.chips.narrow .gs{display:inline}
@media(max-width:600px){.tiles{grid-template-columns:1fr}}
form.f label{display:block;margin:12px 0 4px;color:var(--muted);font-size:13px}
input,select,textarea{font-size:15px;padding:8px;background:#0d0f12;color:var(--text);border:1px solid var(--line);border-radius:4px;width:100%;font-family:inherit}
input[type=checkbox]{width:auto}
textarea{font-family:ui-monospace,Menlo,monospace;font-size:13px}
.help{font-size:13px;color:var(--muted);margin:6px 0}
table{border-collapse:collapse;width:100%;font-size:14px}
td,th{border-bottom:1px solid var(--line);padding:6px 4px;text-align:left;vertical-align:top}
th{color:var(--muted);font-weight:500}
.status{display:flex;gap:20px;align-items:center;flex-wrap:wrap;color:var(--text)}
.status span{display:inline-flex;align-items:center;gap:6px}
.status svg{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round}
.status .dim{color:var(--muted)}
.titlebar{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}
.titlebar .row{margin:12px 0 0}
body.boxoff #ftabs,body.boxoff .fgrid,body.boxoff .ppanel{opacity:.35;filter:grayscale(1);pointer-events:none}
.boxoffmsg{display:none}body.boxoff .boxoffmsg{display:block}
.big{font-size:40px;font-weight:600;font-family:ui-monospace,Menlo,monospace;margin:6px 0}
#lost{display:none;position:fixed;top:0;left:0;right:0;z-index:99;background:var(--danger);color:#fff;text-align:center;padding:10px;font-weight:600}
body.lost #lost{display:block}
body.lost header,body.lost main,body.lost footer{opacity:.3;filter:grayscale(1);pointer-events:none}
.spin{width:34px;height:34px;border:3px solid var(--line);border-top-color:var(--accent);border-radius:50%;animation:sp .8s linear infinite}
.spin.sm{width:12px;height:12px;border-width:2px;display:inline-block;vertical-align:-1px}
@keyframes sp{to{transform:rotate(360deg)}}
.btn svg{width:16px;height:16px;fill:none;stroke:currentColor;stroke-width:2.2;stroke-linecap:round;stroke-linejoin:round;vertical-align:-3px}
.btn:disabled{opacity:.35;cursor:default}
.btn.sm{padding:5px 8px;font-size:13px}
.play{padding:12px 14px;border-left:3px solid var(--accent)}
.prow{display:flex;gap:10px;align-items:center;justify-content:space-between;margin:2px 0 6px}
.prow select{flex:1}
#pseq{border:none;background:transparent;font-size:22px;font-weight:600;padding:2px 0;color:var(--text);cursor:pointer;min-width:0}
#pplay{display:flex;align-items:center;gap:4px;padding:8px 14px}
.mplay{background:var(--panel);border:1px solid var(--line);border-left:3px solid var(--accent);border-radius:6px;padding:6px 8px;margin:10px 0 4px;
 position:sticky;top:6px;z-index:20;box-shadow:0 4px 12px rgba(0,0,0,.45)}
.crumbs{display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin:12px 0 0;font-size:14px;color:var(--muted)}
.crumbs a{color:var(--muted)}.crumbs a:hover{color:var(--accent)}
.crumbs .sep{opacity:.6}.crumbs .cur{color:var(--text)}
.crumbs .back{display:flex;align-items:center;justify-content:center;width:30px;height:30px;border:1px solid var(--line);border-radius:5px;
 margin-right:4px;color:var(--text)}
.crumbs .back svg{width:16px;height:16px;fill:none;stroke:currentColor;stroke-width:2.2;stroke-linecap:round;stroke-linejoin:round}
.mline{display:flex;justify-content:space-between;gap:8px;font-size:14px;margin:0 2px 6px}
.mcue{white-space:pre;overflow:hidden;text-overflow:ellipsis}
.mcount{font-size:13px;white-space:nowrap}
.mbtns{display:flex;gap:4px}
.mbtns .btn{flex:1;padding:7px 0;display:flex;justify-content:center;min-width:0}
.msep{width:8px;flex:none}
.bx.own{color:var(--accent);font-weight:600}
.krow{display:flex;justify-content:space-between;align-items:baseline}
.krow a{text-transform:none;letter-spacing:0;font-size:14px}
.ktitle{font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin-bottom:4px}
h2.ktitle{margin:22px 0 0;font-weight:500}
h2.krow{align-items:center}
.prow>span{flex:1}
.pcue{font-size:15px;white-space:pre;color:var(--muted)}
.pbar{height:4px;background:var(--line);border-radius:2px;overflow:hidden;margin:4px 0 10px}
.pbar i{display:block;height:100%;width:0;background:var(--accent)}
.ptrans{display:grid;grid-template-columns:auto 1fr 1fr auto;gap:8px}
.ptrans .btn{display:flex;align-items:center;justify-content:center;gap:4px;padding:12px 10px}
.pcount{margin-top:12px;font-size:14px}
.pchg{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:6px}
.pchg .btn{flex:1}
.pform{margin-top:10px;border-top:1px solid var(--line);padding-top:6px}
.pform label{display:block;margin:8px 0 4px;color:var(--muted);font-size:13px}
#pnote{margin-top:8px}#pnote:empty{display:none}
table.cues input{padding:5px;font-size:14px}
table.cues td{vertical-align:middle}
table.cues input[type=number]{width:64px}
table.cues .acts{white-space:nowrap}
tr.here td{background:#1f2a36}
#busy{display:none;position:fixed;top:0;left:0;right:0;bottom:0;z-index:98;background:rgba(17,19,23,.6);align-items:center;
 justify-content:center;flex-direction:column;gap:14px;color:var(--text);font-size:15px}
)CSS";

// Header logo: L4Bx (light-on-dark variant from the "L4Bx Logo" design canvas); strokes follow the text colour.
static const char LOGO_HTML[] PROGMEM =
    "<svg viewBox='0 0 500 180' fill='none' role='img' aria-label='L4Bx'>"
    "<g transform='translate(17 16)' stroke='currentColor' stroke-linejoin='miter' stroke-miterlimit='10'>"
    "<g transform='translate(0 120) skewX(-6) translate(0 -120)'><path d='M40 40 V120 H160' stroke-width='20'/></g>"
    "<g transform='translate(0 120) skewX(-14) translate(0 -120)'><path d='M190 128 V6 L110 90 H222' stroke-width='10'/></g>"
    "<g transform='translate(0 120) skewX(-8) translate(0 -120)'><path d='M244 30 V130 M237 30 H272 A24 24 0 0 1 272 78 H237 "
    "M237 78 H278 A26 26 0 0 1 278 130 H237' stroke-width='14'/></g>"
    "<path d='M332 56 L404 138' stroke='#F2217F' stroke-width='11'/>"
    "<path d='M408 6 L428 6 L322 152 Z' fill='#F2217F' stroke='none'/></g></svg>";

// Polls /api/ping with a timeout: a plain fetch() hangs forever when the phone loses WiFi.
static const char MONITOR_JS[] PROGMEM = R"JS(<script>
(function(){let fails=0;const b=document.body;
function lost(v){b.classList.toggle('lost',v)}
function ping(){const c=new AbortController(),t=setTimeout(()=>c.abort(),2500);
 fetch('/api/ping',{signal:c.signal,cache:'no-store'}).then(r=>{clearTimeout(t);if(!r.ok)throw 0;fails=0;lost(false)})
 .catch(()=>{clearTimeout(t);if(++fails>=2)lost(true)})}
setInterval(ping,3000);addEventListener('offline',()=>{fails=2;lost(true)});addEventListener('online',ping);
// spinner overlay while a change travels over the LoRa link
window.busy=function(on,text){let e=document.getElementById('busy');
 if(!e){e=document.createElement('div');e.id='busy';e.innerHTML='<div class="spin"></div><div></div>';document.body.appendChild(e)}
 e.lastChild.textContent=text||'';e.style.display=on?'flex':'none'};
// resolves true once no command to that box is waiting for confirmation, false on timeout
window.waitLink=function(box,ms){const t0=Date.now();return new Promise(res=>{(function chk(){
 fetch('/api/link/pending?box='+box,{cache:'no-store'}).then(r=>r.json()).then(j=>{if(!j.n)res(true);else if(Date.now()-t0>ms)res(false);else setTimeout(chk,400)})
 .catch(()=>{if(Date.now()-t0>ms)res(false);else setTimeout(chk,600)})})()})};
})();
</script>)JS";

struct Asset {
  const char* path;
  String ver;
};
static std::vector<Asset> assets;

void registerAsset(AsyncWebServer& server, const char* path, const char* type, const char* text) {
  const char* d = text;
  size_t len = strlen(text);
  auto strip = [&](const char* open, const char* close) {
    size_t o = strlen(open), c = strlen(close);
    if (len >= o && !strncmp(d, open, o)) {
      d += o;
      len -= o;
    }
    while (len && (d[len - 1] == '\n' || d[len - 1] == ' ')) len--;
    if (len >= c && !strncmp(d + len - c, close, c)) len -= c;
  };
  strip("<script>", "</script>");
  strip("<style>", "</style>");
  char ver[9];
  snprintf(ver, sizeof(ver), "%08x", (unsigned)esp_rom_crc32_le(0, (const uint8_t*)d, len));
  assets.push_back({path, ver});
  String t = type;
  server.on(path, HTTP_GET, [d, len, t](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, t, (const uint8_t*)d, len);
    r->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    req->send(r);
  });
}

String assetUrl(const char* path) {
  for (const Asset& a : assets) {
    if (!strcmp(a.path, path)) return String(path) + "?v=" + a.ver;
  }
  return path;
}

String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

bool hasArg(AsyncWebServerRequest* r, const char* name) {
  return r->hasParam(name, true) || r->hasParam(name);
}

String arg(AsyncWebServerRequest* r, const char* name) {
  if (r->hasParam(name, true)) return r->getParam(name, true)->value();
  if (r->hasParam(name)) return r->getParam(name)->value();
  return "";
}

AsyncResponseStream* pageBegin(AsyncWebServerRequest* req, const String& title, const char* extraHead) {
  AsyncResponseStream* r = req->beginResponseStream("text/html");
  r->print("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width, initial-scale=1'>"
           "<link rel='icon' href='data:,'><base target='_self'><title>");
  r->print(esc(title));
  r->printf(" · dmxbox</title><link rel='stylesheet' href='%s'>", assetUrl("/s/app.css").c_str());
  if (extraHead) r->print(extraHead);
  r->printf("</head><body><div id='lost'>Connection lost – retrying…</div>"
            "<header><a class='brand' href='/'><span class='logo'>%s</span><span>dmxbox</span></a>"
            "<a class='ctx' href='/connectivity#box' title='Rename this box'>%s</a></header><main>",
            LOGO_HTML,
            esc(localBoxName()).c_str());
  return r;
}

void pageEnd(AsyncWebServerRequest* req, AsyncResponseStream* r) {
  r->print("</main><footer><a href='/show'>Show</a><a href='/defs'>Definitions</a><a href='/connectivity'>Connectivity</a>"
           "<a href='/timesetup'>Schedule</a></footer>");
  r->printf("<script src='%s'></script></body></html>", assetUrl("/s/monitor.js").c_str());
  req->send(r);
}

void crumbs(AsyncResponseStream* r, const std::vector<std::pair<String, String>>& path) {
  String parent = "/";
  for (size_t i = 0; i + 1 < path.size(); i++) if (!path[i].second.isEmpty()) parent = path[i].second;
  r->printf("<nav class='crumbs'><a class='back' href='%s' aria-label='Back'><svg viewBox='0 0 24 24'><path d='M15 5l-7 7 7 7'/>"
            "</svg></a><a href='/'>Home</a>", esc(parent).c_str());
  for (size_t i = 0; i < path.size(); i++) {
    r->print("<span class='sep'>›</span>");
    if (i + 1 == path.size()) r->printf("<span class='cur'>%s</span>", esc(path[i].first).c_str());
    else if (path[i].second.isEmpty()) r->printf("<span>%s</span>", esc(path[i].first).c_str());
    else r->printf("<a href='%s'>%s</a>", esc(path[i].second).c_str(), esc(path[i].first).c_str());
  }
  r->print("</nav>");
}

void sendMessage(AsyncWebServerRequest* req, int code, const String& title, const String& msg) {
  AsyncResponseStream* r = pageBegin(req, title);
  r->setCode(code);
  r->printf("<h1 class='%s'>%s</h1>", code >= 400 ? "err" : "", esc(title).c_str());
  r->printf("<div class='panel'>%s</div>", esc(msg).c_str());
  r->print("<div class='row'><a class='btn' href='javascript:history.back()'>Back</a><a class='btn' href='/'>Home</a></div>");
  pageEnd(req, r);
}

void sendProgress(AsyncWebServerRequest* req, const String& title, const String& msg, const String& next, int seconds,
                  uint16_t waitBox) {
  AsyncResponseStream* r = pageBegin(req, title);
  r->printf("<h1>%s</h1><div class='panel row'><span class='spin'></span><span>%s</span></div>", esc(title).c_str(),
            esc(msg).c_str());
  // next is always an internal URL without quotes
  r->printf("<script>(function(){const go=()=>location.replace('%s');", next.c_str());
  if (waitBox) {
    // waitLink is defined by the page footer script, hence the load event
    r->printf("addEventListener('load',()=>waitLink(%u,20000).then(()=>setTimeout(go,%d)));", waitBox, seconds * 1000);
  } else {
    r->printf("setTimeout(go,%d);", seconds * 1000);
  }
  r->print("})();</script>");
  pageEnd(req, r);
}

// ---------------- labels ----------------

static String rangeLabel(const ChannelRange& r) {
  String s = r.name.isEmpty() ? String("") : r.name + " ";
  s += r.from == r.to ? "(" + String(r.from) + ")" : "(" + String(r.from) + "–" + String(r.to) + ")";
  return s;
}

String fixtureTypeLabel(const Fixture& f) {
  if (f.defId == 0) return "Generic " + String(f.channelCount) + " ch";
  const FixtureDef* d = findDef(f.defId);
  return d ? d->name : String("?");
}

String dmxRangeLabel(uint16_t address, uint16_t count) {
  return count <= 1 ? "DMX " + String(address) : "DMX " + String(address) + "–" + String(address + count - 1);
}

static String dmxRangeLabel(const Fixture& f) { return dmxRangeLabel(f.address, fixtureChannelCount(f)); }

// ---------------- home ----------------

static const char HOME_JS[] PROGMEM = R"JS(<script>
(function(){
const IC={
wifi:'<svg viewBox="0 0 24 24"><path d="M2 8.5a15 15 0 0 1 20 0M5.5 12a10 10 0 0 1 13 0M9 15.5a5 5 0 0 1 6 0"/><circle cx="12" cy="19" r="1"/></svg>',
lora:'<svg viewBox="0 0 24 24"><circle cx="12" cy="11" r="2"/><path d="M12 13v8M7.8 6.8a6 6 0 0 0 0 8.4M16.2 6.8a6 6 0 0 1 0 8.4M4.9 3.9a10 10 0 0 0 0 14.2M19.1 3.9a10 10 0 0 1 0 14.2"/></svg>',
clock:'<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>'};
function h(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function render(s){
  const w=s.wifi,l=s.lora;let out='<span>'+IC.wifi+(w.mode==='STA'?'STA '+w.rssi+' dBm':'AP '+w.clients)+'</span>';
  if(l)out+='<span class="'+(l.enabled&&l.boxes>1?'':'dim')+'">'+IC.lora+(!l.enabled?'off':l.boxes>1?l.boxes+' boxes':'alone')+'</span>';
  if(s.time)out+='<span class="dim">'+IC.clock+h(s.time)+'</span>';
  document.getElementById('conn').innerHTML=out;
  document.getElementById('sched').textContent=s.sched.charAt(0).toUpperCase()+s.sched.slice(1);
  const n=document.getElementById('note');let t='';
  let spin=false;
  if(s.errors&&s.errors.length)t+=(t?' · ':'')+s.errors.map(e=>e.box+': '+e.msg).join(' · ');
  n.innerHTML=(spin?'<span class="spin sm"></span> ':'')+h(t);n.style.display=t?'':'none';
  if(s.boxes)document.querySelectorAll('.tile[data-box]').forEach(e=>{const on=s.boxes[e.dataset.box];if(on!==undefined)e.classList.toggle('off',!on)});
  clearTimeout(window._st);window._st=setTimeout(poll,spin?1000:5000);
}
document.addEventListener('click',e=>{const t=e.target.closest('.tile[data-href]');if(t&&!e.target.closest('a'))location=t.dataset.href});
// group filter (remembered per browser) and aligned columns: a fixture without a group gets an empty
// placeholder, so that the same group sits at the same place on every tile, like a fixture sheet
let hidden=[];try{hidden=JSON.parse(localStorage.getItem('hideGroups')||'[]')}catch(e){}
const GF=document.getElementById('gfil');
function sheet(){
  const chips=[...document.querySelectorAll('.tiles .chip:not(.none)')];
  const present=[...new Set(chips.map(c=>+c.dataset.g))].sort((a,b)=>a-b);
  GF.querySelectorAll('button').forEach(b=>{const g=+b.dataset.g;b.classList.toggle('on',!hidden.includes(g));b.style.display=present.includes(g)?'':'none'});
  const shown=present.filter(g=>!hidden.includes(g));
  document.querySelectorAll('.tiles .chips').forEach(box=>{
    box.querySelectorAll('.chip.none').forEach(c=>c.remove());
    const have={};box.querySelectorAll('.chip').forEach(c=>{have[c.dataset.g]=c;c.classList.toggle('hid',hidden.includes(+c.dataset.g))});
    let any=false;
    shown.forEach(g=>{if(have[g]){any=true;box.appendChild(have[g])}else{const p=document.createElement('span');p.className='chip none';p.dataset.g=g;box.appendChild(p)}});
    box.classList.toggle('hid',!any);
    box.classList.toggle('narrow',box.clientWidth/Math.max(1,shown.length)<75);  // only the letter
  });
}
addEventListener('resize',sheet);
GF.addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;const g=+b.dataset.g;
  hidden=hidden.includes(g)?hidden.filter(x=>x!==g):hidden.concat(g);
  try{localStorage.setItem('hideGroups',JSON.stringify(hidden))}catch(e){}
  sheet()});
sheet();
function poll(){fetch('/api/status',{cache:'no-store'}).then(r=>r.json()).then(render).catch(()=>{clearTimeout(window._st);window._st=setTimeout(poll,5000)})}
poll();
// keep tile values current: re-render the tile grid from the server every 5 s
setInterval(()=>{if(document.hidden)return;fetch('/',{cache:'no-store'}).then(r=>r.text()).then(t=>{
  const n=new DOMParser().parseFromString(t,'text/html').querySelector('.tiles');
  if(n){document.querySelector('.tiles').innerHTML=n.innerHTML;sheet()}}).catch(()=>{})},5000);
})();
</script>)JS";

static String lower(String s) {
  s.toLowerCase();
  return s;
}

// Swatch for a colour name as used in wheel ranges ("Red", "Light blue", "Weiß" ...); "" if unknown.
static String colorFromName(const String& name) {
  static const char* const map[][2] = {
      {"light blue", "#81d4fa"}, {"dark blue", "#1a237e"}, {"congo", "#3949ab"}, {"lavender", "#b39ddb"},
      {"magenta", "#d81b60"},    {"pink", "#f06292"},      {"purple", "#8e24aa"},  {"violet", "#7e57c2"},
      {"orange", "#fb8c00"},     {"amber", "#ffb300"},     {"yellow", "#fdd835"},  {"lime", "#c0ca33"},
      {"green", "#43a047"},      {"cyan", "#00bcd4"},      {"blue", "#1e88e5"},    {"red", "#e53935"},
      {"white", "#ffffff"},      {"open", "#ffffff"},      {"cto", "#ffcc80"},     {"ctb", "#cfe8ff"},
      {"uv", "#6a1b9a"},         {"rot", "#e53935"},       {"grün", "#43a047"},    {"blau", "#1e88e5"},
      {"weiß", "#ffffff"},       {"gelb", "#fdd835"},      {"lila", "#8e24aa"}};
  String n = lower(name);
  for (const auto& m : map) {
    if (n.indexOf(m[0]) >= 0) return m[1];
  }
  return "";
}

// Colour of a colour group: RGB(W/A) or CMY mixing channels, otherwise the current wheel range name.
static String groupSwatch(const std::vector<TileChannel>& chans) {
  int R = -1, G = -1, B = -1, W = 0, A = 0, C = -1, M = -1, Y = -1;
  for (const TileChannel& c : chans) {
    String n = lower(c.name);
    n.trim();
    if (n.indexOf("fine") >= 0) continue;
    if (n == "r" || n.startsWith("red") || n == "rot") R = c.value;
    else if (n == "g" || n.startsWith("green") || n.startsWith("grün") || n.startsWith("gruen")) G = c.value;
    else if (n == "b" || n.startsWith("blue") || n.startsWith("blau")) B = c.value;
    else if (n == "w" || n.startsWith("white") || n.startsWith("weiß") || n.startsWith("weiss")) W = c.value;
    else if (n == "a" || n.startsWith("amber")) A = c.value;
    else if (n == "c" || n.startsWith("cyan") || n.startsWith("cyna")) C = c.value;
    else if (n == "m" || n.startsWith("magenta")) M = c.value;
    else if (n == "y" || n.startsWith("yellow") || n.startsWith("gelb")) Y = c.value;
  }
  char buf[24];
  if (R >= 0 || G >= 0 || B >= 0) {
    int r = min(255, max(R, 0) + W + A), g = min(255, max(G, 0) + W + A / 2), b = min(255, max(B, 0) + W);
    snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", r, g, b);
    return buf;
  }
  if (C >= 0 || M >= 0 || Y >= 0) {
    snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", 255 - max(C, 0), 255 - max(M, 0), 255 - max(Y, 0));
    return buf;
  }
  for (const TileChannel& c : chans) {
    if (!c.ranges) continue;
    for (const ChannelRange& cr : *c.ranges) {
      if (c.value >= cr.from && c.value <= cr.to) {
        String sw = colorFromName(cr.name);
        if (!sw.isEmpty()) return sw;
      }
    }
  }
  return "";
}

void fixtureTile(AsyncResponseStream* r, const String& href, const String& name, const String& dmx, const String& type,
                 const String& box, const String& warn, const std::vector<TileGroup>& groups, uint16_t boxId, bool off) {
  // the box comes first; this box is highlighted, so its fixtures stand out in the list
  r->printf("<div class='tile%s' data-href='%s' data-box='%u'><a class='name' href='%s'>%s</a>"
            "<div class='meta'><span class='bx%s'>%s</span> · <span class='mono'>%s</span> · %s</div>",
            off ? " off" : "", esc(href).c_str(), boxId, esc(href).c_str(), esc(name).c_str(), boxId ? "" : " own",
            esc(box).c_str(), dmx.c_str(), esc(type).c_str());
  if (!warn.isEmpty()) r->printf("<div class='meta warn'>%s</div>", esc(warn).c_str());
  if (!groups.empty()) {
    r->print("<div class='chips'>");
    for (const TileGroup& g : groups) {
      bool nonDefault = false;
      String vals;
      for (const TileChannel& c : g.channels) {
        nonDefault |= c.value != 0;
        vals += (vals.isEmpty() ? "" : "/") + String(c.value);
      }
      String sw = g.group == GROUP_COLOR ? groupSwatch(g.channels) : String("");
      if (sw.isEmpty() && g.group == GROUP_COLOR && !g.preset.isEmpty()) sw = colorFromName(g.preset);
      r->printf("<a class='chip%s' data-g='%u' href='%s#g%u'><span class='cl'>", (nonDefault || !g.preset.isEmpty()) ? " on" : "",
                g.group, esc(href).c_str(), g.group);
      if (!sw.isEmpty()) r->printf("<i class='sw' style='background:%s'></i>", sw.c_str());
      r->printf("<span class='gn'>%s</span><span class='gs'>%c</span></span><span class='cv'>%s</span>", GROUP_NAMES[g.group],
                GROUP_NAMES[g.group][0], esc(g.preset.isEmpty() ? vals : g.preset).c_str());
      if (g.group == GROUP_DIMMER && !g.channels.empty()) {
        r->printf("<span class='lv'><i style='width:%d%%'></i></span>", g.channels[0].value * 100 / 255);
      }
      r->print("</a>");
    }
    r->print("</div>");
  }
  r->print("</div>");
}

static void localTile(AsyncResponseStream* r, const Fixture& f) {
  std::vector<TileGroup> groups;
  std::vector<uint8_t> chGroups = fixtureGroups(f);
  std::vector<FixtureControl> controls = fixtureControls(f);
  for (uint8_t g : groupsPresent(chGroups)) {
    TileGroup tg{g, "", {}};
    int active = presetActive(f.id, g);
    if (active >= 0) {
      for (const PresetInfo& pi : presetsFor(f, g)) if ((int)pi.id == active) tg.preset = pi.name;
    }
    for (const FixtureControl& c : controls) {
      if (c.ch >= chGroups.size() || chGroups[c.ch] != g) continue;
      uint16_t dmx = f.address + c.ch;
      const ChannelDef* cdef = fixtureChannelDef(f, c.ch);
      tg.channels.push_back({fixtureChannelName(f, c.ch), dmx <= DMX_UNIVERSE_SIZE ? data[dmx] : (uint8_t)0,
                             cdef ? &cdef->ranges : nullptr});
    }
    groups.push_back(tg);
  }
  fixtureTile(r, "/fixture?id=" + String(f.id), f.name, dmxRangeLabel(f), fixtureTypeLabel(f), localBoxName(),
              fixtureOverlaps(f) ? "overlaps another fixture" : "", groups, 0, false);
}

void sendFixturesPage(AsyncWebServerRequest* request) {
  AsyncResponseStream* r = pageBegin(request, "Fixtures");
  // status strip: connectivity and timer are information, the playback below is the control
  r->print("<div class='sbar'><a id='connp' href='/connectivity' title='Connectivity'>"
           "<span class='status' id='conn'><span class='dim'>…</span></span></a>"
           "<a id='schedp' href='/timesetup' title='Timer'><svg viewBox='0 0 24 24'><rect x='3' y='5' width='18' height='16' rx='2'/>"
           "<path d='M3 10h18M8 3v4M16 3v4'/></svg><span id='sched'>…</span></a></div>");
  renderPlaybackPanel(r);
  r->print("<div class='panel meta' id='note' style='display:none'></div>");
  // group filter: letters in group order (Control is the second C)
  r->print("<h2 class='ktitle krow'><span>Fixtures</span><span class='gfil' id='gfil'>");
  for (uint8_t g = 0; g < GROUP_COUNT; g++) r->printf("<button type='button' data-g='%u' title='%s'>%c</button>", g, GROUP_NAMES[g], GROUP_NAMES[g][0]);
  r->print("</span></h2><div class='tiles home'>");
  for (const Fixture& f : fixtureList) localTile(r, f);
#ifdef HAS_LORA
  renderRemoteTiles(r);
#endif
  r->print("<a class='tile new' href='/fixture/edit'>+ New Fixture</a></div>");
  r->printf("<script src='%s'></script>", assetUrl("/s/home.js").c_str());
  pageEnd(request, r);
}

// ---------------- fixture edit / save / delete ----------------

static uint16_t nextFreeAddress(const std::vector<std::pair<uint16_t, uint16_t>>& used) {
  uint16_t next = 1;
  for (const auto& u : used) {
    uint16_t end = u.first + u.second;
    if (end > next) next = end;
  }
  return next > DMX_UNIVERSE_SIZE ? DMX_UNIVERSE_SIZE : next;
}

static void defOption(AsyncResponseStream* r, const String& value, const String& label, bool selected) {
  r->printf("<option value='%s'%s>%s</option>", value.c_str(), selected ? " selected" : "", esc(label).c_str());
}

static String localDefLabel(const FixtureDef& d) { return d.name + " (" + String(d.channels.size()) + " ch)"; }

// Form shared by local and remote fixtures. boxId 0 = this box.
static void fixtureForm(AsyncResponseStream* r, uint16_t boxId, bool isNew, uint16_t id, const String& name,
                        uint16_t address, const String& defValue, uint16_t count) {
  r->print("<form class='f' action='/fixture/save' method='post'>");
  if (!isNew) r->printf("<input type='hidden' name='id' value='%u'><input type='hidden' name='box' value='%u'>", id, boxId);
#ifdef HAS_LORA
  std::vector<RemoteBox> boxes = syncRemoteBoxes();
  if (isNew) {
    r->print("<label>Box</label><select name='box' id='box'>");
    r->printf("<option value='0'>%s (this box)</option>", esc(localBoxName()).c_str());
    for (const RemoteBox& b : boxes) {
      if (b.peer.online() && b.described) {
        r->printf("<option value='%u'%s>%s</option>", b.peer.id, b.peer.id == boxId ? " selected" : "", esc(b.peer.name).c_str());
      }
    }
    r->print("</select>");
  } else if (boxId) {
    for (const RemoteBox& b : boxes) {
      if (b.peer.id == boxId) r->printf("<label>Box</label><div class='panel'>%s</div>", esc(b.peer.name).c_str());
    }
  }
#endif
  r->printf("<label>Name</label><input name='name' maxlength='%d' value='%s' placeholder='e.g. Spot left'>",
            FIXTURE_NAME_MAX, esc(name).c_str());
  r->printf("<label>Start address (1–512)</label><input name='address' id='addr' type='number' min='1' max='512' value='%u'>",
            address);
  r->print("<label>Type</label><select name='def' id='def'></select>");
  r->printf("<div id='cnt'><label>Channel count</label><input name='count' type='number' min='1' max='512' value='%u'></div>",
            count);
  r->print("<div class='row'><button class='btn primary grow' type='submit'>Save</button></div></form>");

  // definition choices per box: own definitions, plus local ones that would be copied over
  JsonDocument doc;
  JsonObject boxesJson = doc.to<JsonObject>();
  {
    JsonObject b = boxesJson["0"].to<JsonObject>();
    std::vector<std::pair<uint16_t, uint16_t>> used;
    for (const Fixture& f : fixtureList) used.push_back({f.address, fixtureChannelCount(f)});
    b["next"] = nextFreeAddress(used);
    JsonArray defs = b["defs"].to<JsonArray>();
    for (const FixtureDef& d : fixtureDefs) {
      JsonArray o = defs.add<JsonArray>();
      o.add("L:" + String(d.id));
      o.add(localDefLabel(d));
    }
  }
#ifdef HAS_LORA
  for (const RemoteBox& rb : boxes) {
    if (!rb.described) continue;
    JsonObject b = boxesJson[String(rb.peer.id)].to<JsonObject>();
    std::vector<std::pair<uint16_t, uint16_t>> used;
    for (const RemoteFixture& f : rb.fixtures) used.push_back({f.address, f.count});
    b["next"] = nextFreeAddress(used);
    JsonArray defs = b["defs"].to<JsonArray>();
    for (const RemoteDef& d : rb.defs) {
      JsonArray o = defs.add<JsonArray>();
      o.add("R:" + String(d.id));
      o.add(d.name + " (" + String(d.channels) + " ch)");
    }
    for (const FixtureDef& d : fixtureDefs) {
      uint32_t crc = defCrc(d);
      bool there = false;
      for (const RemoteDef& rd : rb.defs) there |= rd.crc == crc;
      if (there) continue;
      JsonArray o = defs.add<JsonArray>();
      o.add("L:" + String(d.id));
      o.add(localDefLabel(d) + " – copy from this box");
    }
  }
#endif
  String boxesStr;
  serializeJson(doc, boxesStr);
  r->printf("<script>const BOXES=%s,NEW=%s,CUR='%s',CURBOX='%u';", boxesStr.c_str(), isNew ? "true" : "false",
            esc(defValue).c_str(), boxId);
  r->print(R"JS(
const def=document.getElementById('def'),box=document.getElementById('box'),cnt=document.getElementById('cnt');
function fill(){const b=BOXES[box?box.value:CURBOX]||{defs:[],next:1};
 def.innerHTML='<option value="0">Generic (free channel count)</option>';
 b.defs.forEach(d=>{const o=document.createElement('option');o.value=d[0];o.textContent=d[1];if(d[0]===CUR)o.selected=true;def.appendChild(o)});
 const m=document.createElement('option');m.value='__defs';m.textContent='Manage definitions…';def.appendChild(m);
 if(NEW)document.getElementById('addr').value=b.next;tg()}
function tg(){cnt.style.display=def.value==='0'?'':'none'}
def.onchange=()=>{if(def.value==='__defs'){location='/defs';return}tg()};if(box)box.onchange=fill;fill();
</script>)JS");
}

static void handleFixtureEdit(AsyncWebServerRequest* req) {
  uint16_t boxId = arg(req, "box").toInt();
  bool isNew = !hasArg(req, "id");
#ifdef HAS_LORA
  if (boxId && boxId != syncBoxId()) {
    RemoteBox b;
    if (!syncRemoteBox(boxId, b)) return sendMessage(req, 404, "Not found", "Box not known.");
    if (isNew) {
      AsyncResponseStream* r = pageBegin(req, "New Fixture");
      crumbs(r, {{"New fixture", ""}});
      r->print("<h1>New Fixture</h1>");
      fixtureForm(r, boxId, true, 0, "", 1, "0", 1);
      return pageEnd(req, r);
    }
    uint16_t id = arg(req, "id").toInt();
    for (const RemoteFixture& f : b.fixtures) {
      if (f.id != id) continue;
      AsyncResponseStream* r = pageBegin(req, "Edit Fixture");
      crumbs(r, {{b.peer.name, ""}, {f.name, "/remote/fixture?box=" + String(boxId) + "&id=" + String(f.id)}, {"Edit", ""}});
      r->printf("<h1>Edit %s</h1>", esc(f.name).c_str());
      fixtureForm(r, boxId, false, f.id, f.name, f.address, f.defId ? "R:" + String(f.defId) : String("0"), f.count);
      r->printf("<div class='row'><a class='btn danger' href='/fixture/delete?box=%u&id=%u'>Delete fixture</a>"
                "<a class='btn' href='/remote/fixture?box=%u&id=%u'>Cancel</a></div>", boxId, f.id, boxId, f.id);
      return pageEnd(req, r);
    }
    return sendMessage(req, 404, "Not found", "Fixture not found on " + b.peer.name + ".");
  }
#endif
  if (isNew) {
    AsyncResponseStream* r = pageBegin(req, "New Fixture");
    crumbs(r, {{"New fixture", ""}});
    r->print("<h1>New Fixture</h1>");
    fixtureForm(r, 0, true, 0, "", 1, "0", 1);
    return pageEnd(req, r);
  }
  Fixture* f = findFixture(arg(req, "id").toInt());
  if (!f) return sendMessage(req, 404, "Not found", "Fixture does not exist.");
  AsyncResponseStream* r = pageBegin(req, "Edit Fixture");
  crumbs(r, {{f->name, "/fixture?id=" + String(f->id)}, {"Edit", ""}});
  r->printf("<h1>Edit %s</h1>", esc(f->name).c_str());
  fixtureForm(r, 0, false, f->id, f->name, f->address, f->defId ? "L:" + String(f->defId) : String("0"), f->channelCount);
  r->printf("<div class='row'><a class='btn danger' href='/fixture/delete?id=%u'>Delete fixture</a>"
            "<a class='btn' href='/fixture?id=%u'>Cancel</a></div>", f->id, f->id);
  pageEnd(req, r);
}

// "0" = generic, "L:<id>" = definition of this box, "R:<id>" = definition of the target box,
// a plain number is treated as a local definition id.
static void parseDefValue(const String& v, bool& remote, uint16_t& id) {
  remote = v.startsWith("R:");
  id = (v.startsWith("L:") || remote) ? v.substring(2).toInt() : v.toInt();
}

static void handleFixtureSave(AsyncWebServerRequest* req) {
  bool remoteDef;
  uint16_t defId;
  parseDefValue(arg(req, "def"), remoteDef, defId);
  uint16_t boxId = arg(req, "box").toInt();
#ifdef HAS_LORA
  if (boxId && boxId != syncBoxId()) {
    RemoteBox b;
    if (!syncRemoteBox(boxId, b)) return sendMessage(req, 404, "Not found", "Box not known.");
    if (!b.peer.online()) return sendMessage(req, 409, "Box offline", b.peer.name + " is not reachable.");
    uint16_t remoteId = 0;
    int pushLocal = -1;
    if (defId && remoteDef) {
      remoteId = defId;
    } else if (defId) {
      FixtureDef* d = findDef(defId);
      if (!d) return sendMessage(req, 400, "Not saved", "Definition not found");
      uint32_t crc = defCrc(*d);
      for (const RemoteDef& rd : b.defs) if (rd.crc == crc) remoteId = rd.id;
      if (!remoteId) pushLocal = defId;
    }
    syncRemoteFixtureSave(boxId, arg(req, "id").toInt(), arg(req, "name"), arg(req, "address").toInt(), remoteId,
                          pushLocal, arg(req, "count").toInt());
    return sendProgress(req, "Sending", "Sending the fixture" + String(pushLocal >= 0 ? " and its definition" : "") +
                                            " to " + b.peer.name + "…", "/", 1, boxId);
  }
#endif
  Fixture f;
  if (hasArg(req, "id") && arg(req, "id").toInt() > 0) {
    Fixture* existing = findFixture(arg(req, "id").toInt());
    if (!existing) return sendMessage(req, 404, "Not found", "Fixture does not exist.");
    f = *existing;
  }
  if (hasArg(req, "name")) f.name = arg(req, "name");
  if (hasArg(req, "address")) f.address = constrain(arg(req, "address").toInt(), 0, 9999);
  if (hasArg(req, "def")) f.defId = remoteDef ? 0 : defId;
  if (hasArg(req, "count")) f.channelCount = constrain(arg(req, "count").toInt(), 0, 9999);
  String err = saveFixture(f);
  if (!err.isEmpty()) return sendMessage(req, 400, "Not saved", err);
  req->redirect("/");
}

static void handleFixtureDelete(AsyncWebServerRequest* req) {
  uint16_t boxId = arg(req, "box").toInt();
  uint16_t id = arg(req, "id").toInt();
  bool confirmed = arg(req, "confirm") == "1";
#ifdef HAS_LORA
  if (boxId && boxId != syncBoxId()) {
    RemoteBox b;
    if (!syncRemoteBox(boxId, b)) return sendMessage(req, 404, "Not found", "Box not known.");
    const RemoteFixture* f = nullptr;
    for (const RemoteFixture& rf : b.fixtures) if (rf.id == id) f = &rf;
    if (!f) return sendMessage(req, 404, "Not found", "Fixture not found.");
    if (confirmed) {
      if (!b.peer.online()) return sendMessage(req, 409, "Box offline", b.peer.name + " is not reachable.");
      syncRemoteFixtureDelete(boxId, id);
      return sendProgress(req, "Deleting", "Deleting " + f->name + " on " + b.peer.name + "…", "/", 1, boxId);
    }
    AsyncResponseStream* r = pageBegin(req, "Delete Fixture");
    r->printf("<h1>Delete %s?</h1><div class='panel meta'>%s · %s<br>DMX values of its channels stay as they are.</div>",
              esc(f->name).c_str(), dmxRangeLabel(f->address, f->count).c_str(), esc(b.peer.name).c_str());
    r->printf("<div class='row'><a class='btn danger' href='/fixture/delete?box=%u&id=%u&confirm=1'>Delete</a>"
              "<a class='btn' href='/remote/fixture?box=%u&id=%u'>Cancel</a></div>", boxId, id, boxId, id);
    return pageEnd(req, r);
  }
#endif
  Fixture* f = findFixture(id);
  if (!f) return sendMessage(req, 404, "Not found", "Fixture does not exist.");
  if (confirmed) {
    String err = deleteFixture(id);
    if (!err.isEmpty()) return sendMessage(req, 500, "Error", err);
    presetsFixtureDeleted(id);
    return req->redirect("/");
  }
  AsyncResponseStream* r = pageBegin(req, "Delete Fixture");
  r->printf("<h1>Delete %s?</h1><div class='panel meta'>%s<br>DMX values of its channels stay as they are.</div>",
            esc(f->name).c_str(), dmxRangeLabel(*f).c_str());
  r->printf("<div class='row'><a class='btn danger' href='/fixture/delete?id=%u&confirm=1'>Delete</a>"
            "<a class='btn' href='/fixture?id=%u'>Cancel</a></div>", f->id, f->id);
  pageEnd(req, r);
}

// ---------------- fixture channels ----------------

static String fixtureValuesJson(const Fixture& f) {
  String s = "[";
  uint16_t n = fixtureChannelCount(f);
  for (uint16_t i = 0; i < n; i++) {
    uint16_t dmx = f.address + i;
    if (i) s += ",";
    s += String(dmx <= DMX_UNIVERSE_SIZE ? data[dmx] : 0);
  }
  return s + "]";
}

static String fixtureRefsJson(const Fixture& f) {
  String s = "{";
  bool first = true;
  for (const PresetRef& r : presetRefs()) {
    if (r.fixtureId != f.id) continue;
    if (!first) s += ",";
    s += "\"" + String(r.group) + "\":" + String(r.presetId);
    first = false;
  }
  return s + "}";
}

// Channels changed by hand since the last cue change: [channel indices].
static String fixtureMarksJson(const Fixture& f) {
  String s = "[";
  uint16_t n = fixtureChannelCount(f);
  bool first = true;
  for (uint16_t i = 0; i < n; i++) {
    if (!cuesMarked(f.address + i)) continue;
    if (!first) s += ",";
    s += String(i);
    first = false;
  }
  return s + "]";
}

static void handleFixturePage(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "id").toInt());
  if (!fp) return req->redirect("/");
  const Fixture& f = *fp;
  AsyncResponseStream* r = pageBegin(req, f.name, faderHead().c_str());
  crumbs(r, {{f.name, ""}});
  renderMiniPlayback(r);
  r->printf("<div class='titlebar'><h1>%s</h1><span class='row'>", esc(f.name).c_str());
  if (f.defId == 0) r->printf("<a class='btn' href='/fixturenames?id=%u'>Channel names</a>", f.id);
  r->printf("<a class='btn' href='/fixture/edit?id=%u'>Edit</a></span></div>", f.id);
  String id = String(f.id);
  std::vector<uint8_t> groups = fixtureGroups(f);
  std::vector<uint8_t> present = groupsPresent(groups);
  faderTabs(r, present);
  FaderGrid grid;
  grid.setUrl = "/api/channel/set?fix=" + id;
  grid.clearUrl = "/api/channel/clear?fix=" + id;
  grid.getUrl = "/api/channel/values?fix=" + id;
  grid.presetUrl = "/api/preset?fix=" + id;
  grid.valuesJson = fixtureValuesJson(f);
  grid.refsJson = fixtureRefsJson(f);
  grid.marksJson = fixtureMarksJson(f);
  grid.version = String(fixturesVersion());
  faderGridBegin(r, grid);
  for (const FixtureControl& c : fixtureControls(f)) {
    const ChannelDef* cdef = fixtureChannelDef(f, c.ch);
    faderCard(r, fixtureChannelName(f, c.ch), "/channel?fix=" + id + "&ch=" + String(c.ch), c.ch, c.fine,
              c.ch < groups.size() ? groups[c.ch] : GROUP_CONTROL, f.address + c.ch,
              c.fine >= 0 ? f.address + c.fine : -1, cdef ? &cdef->ranges : nullptr);
  }
  faderGridEnd(r);
  for (uint8_t g : present) {
    std::vector<RangeChannel> rcs;
    for (const FixtureControl& c : fixtureControls(f)) {
      const ChannelDef* cdef = fixtureChannelDef(f, c.ch);
      if (cdef && !cdef->ranges.empty() && c.ch < groups.size() && groups[c.ch] == g) {
        rcs.push_back({c.ch, c.fine, fixtureChannelName(f, c.ch), &cdef->ranges});
      }
    }
    faderPresetPanel(r, g, presetsFor(f, g), presetActive(f.id, g), rcs);
  }
  r->printf("<p class='meta finfo'><span class='bx own'>%s</span> · <span class='mono'>%s</span> · %s</p>",
            esc(localBoxName()).c_str(), dmxRangeLabel(f).c_str(), esc(fixtureTypeLabel(f)).c_str());
  faderScript(r);
  pageEnd(req, r);
}

static void handleChannelValuesApi(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "fix").toInt());
  if (!fp) return req->send(404, "application/json", "{}");
  req->send(200, "application/json",
            "{\"v\":" + fixtureValuesJson(*fp) + ",\"r\":" + fixtureRefsJson(*fp) +
                ",\"m\":" + fixtureMarksJson(*fp) + ",\"ver\":" + String(fixturesVersion()) + "}");
}

static void handlePresetApi(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "fix").toInt());
  if (!fp) return req->send(404, "text/plain", "Fixture not found");
  String op = arg(req, "op");
  uint16_t pid = arg(req, "p").toInt();
  String err;
  if (op == "recall") err = presetRecall(fp->id, pid);
  else if (op == "store") err = presetStore(fp->id, pid);
  else if (op == "create") err = presetCreate(fp->id, arg(req, "g").toInt(), arg(req, "name"));
  else if (op == "rename") err = presetRename(pid, arg(req, "name"));
  else if (op == "delete") err = presetDelete(pid);
  else err = "Unknown action";
  if (!err.isEmpty()) return req->send(400, "text/plain", err);
  req->send(200, "text/plain", "ok");
}

static void handleChannelSetApi(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "fix").toInt());
  int ch = arg(req, "ch").toInt();
  if (!fp || ch < 0 || ch >= fixtureChannelCount(*fp) || !hasArg(req, "v")) return req->send(400, "text/plain", "bad request");
  std::vector<int16_t> fine = fixtureFinePairs(*fp);
  long v = arg(req, "v").toInt();
  uint16_t dmx = fp->address + ch;
  if (ch < (int)fine.size() && fine[ch] >= 0) {
    v = constrain(v, 0, 65535);
    uint16_t fineDmx = fp->address + fine[ch];
    portENTER_CRITICAL(&dmxDataMux);
    if (dmx <= DMX_UNIVERSE_SIZE) data[dmx] = v >> 8;
    if (fineDmx <= DMX_UNIVERSE_SIZE) data[fineDmx] = v & 0xFF;
    portEXIT_CRITICAL(&dmxDataMux);
  } else if (dmx <= DMX_UNIVERSE_SIZE) {
    data[dmx] = constrain(v, 0, 255);
  }
  uint16_t last = (ch < (int)fine.size() && fine[ch] >= 0) ? max(dmx, (uint16_t)(fp->address + fine[ch])) : dmx;
  uint16_t first = (ch < (int)fine.size() && fine[ch] >= 0) ? min(dmx, (uint16_t)(fp->address + fine[ch])) : dmx;
  presetChannelsChanged(first, last);
  cuesManualChange(first, last);
  req->send(200, "text/plain", "ok");
}

static void handleChannelClearApi(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "fix").toInt());
  int ch = arg(req, "ch").toInt();
  if (!fp || ch < 0 || ch >= fixtureChannelCount(*fp)) return req->send(400, "text/plain", "bad request");
  std::vector<int16_t> fine = fixtureFinePairs(*fp);
  uint16_t dmx = fp->address + ch;
  int16_t f = ch < (int)fine.size() ? fine[ch] : -1;
  uint16_t first = f >= 0 ? min(dmx, (uint16_t)(fp->address + f)) : dmx;
  uint16_t last = f >= 0 ? max(dmx, (uint16_t)(fp->address + f)) : dmx;
  cuesClearChannels(first, last);
  req->send(200, "text/plain", "ok");
}

static void renderChannel(AsyncWebServerRequest* req, const Fixture& f, int ch) {
  uint16_t dmx = f.address + ch;
  const ChannelDef* cdef = fixtureChannelDef(f, ch);
  String chName = fixtureChannelName(f, ch);
  AsyncResponseStream* r = pageBegin(req, chName);
  crumbs(r, {{f.name, "/fixture?id=" + String(f.id)}, {chName, ""}});
  r->printf("<p class='meta mono'>DMX %u</p>", dmx);
  r->printf("<h1>%s</h1>", esc(chName).c_str());
  r->printf("<div class='big'>%u</div>", data[dmx]);
  r->printf("<form class='row' action='/channel' method='get'><input type='hidden' name='fix' value='%u'>"
            "<input type='hidden' name='ch' value='%d'><input name='val' type='number' min='0' max='255' value='%u' style='width:110px'>"
            "<button class='btn' type='submit'>Set</button></form>",
            f.id, ch, data[dmx]);
  if (cdef && !cdef->ranges.empty()) {
    r->print("<h2>Ranges</h2><div class='row'>");
    for (const ChannelRange& cr : cdef->ranges) {
      bool sel = data[dmx] >= cr.from && data[dmx] <= cr.to;
      r->printf("<a class='btn%s' href='/channel?fix=%u&ch=%d&val=%u'>%s</a>", sel ? " sel" : "", f.id, ch, cr.from,
                esc(rangeLabel(cr)).c_str());
    }
    r->print("</div>");
  }
  static const int deltas[] = {-50, -10, -5, -1, 1, 5, 10, 50};
  r->print("<h2>Adjust</h2><div class='row'>");
  r->printf("<a class='btn' href='/channel?fix=%u&ch=%d&val=0'>0</a>", f.id, ch);
  for (int d : deltas) r->printf("<a class='btn mono' href='/channel?fix=%u&ch=%d&delta=%d'>%+d</a>", f.id, ch, d, d);
  r->printf("<a class='btn' href='/channel?fix=%u&ch=%d&val=255'>255</a></div>", f.id, ch);
  if (f.defId == 0) {
    r->printf("<h2>Name</h2><form class='row' action='/channel' method='get'><input type='hidden' name='fix' value='%u'>"
              "<input type='hidden' name='ch' value='%d'><input name='name' maxlength='%d' value='%s' style='flex:1'>"
              "<button class='btn' type='submit'>Rename</button></form>",
              f.id, ch, FIXTURE_NAME_MAX, esc(chName).c_str());
  }
  pageEnd(req, r);
}

static void handleChannel(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "fix").toInt());
  int ch = arg(req, "ch").toInt();
  if (!fp || ch < 0 || ch >= fixtureChannelCount(*fp)) return req->redirect("/");
  uint16_t dmx = fp->address + ch;
  if (dmx < 1 || dmx > DMX_UNIVERSE_SIZE) return req->redirect("/");

  if (hasArg(req, "val")) {
    data[dmx] = constrain(arg(req, "val").toInt(), 0, 255);
    presetChannelsChanged(dmx, dmx);
    cuesManualChange(dmx, dmx);
  } else if (hasArg(req, "delta")) {
    data[dmx] = constrain((int)data[dmx] + arg(req, "delta").toInt(), 0, 255);
    presetChannelsChanged(dmx, dmx);
    cuesManualChange(dmx, dmx);
  }
  if (hasArg(req, "name") && fp->defId == 0) {
    Fixture copy = *fp;
    if (copy.channelNames.size() < (size_t)ch + 1) copy.channelNames.resize(ch + 1);
    copy.channelNames[ch] = arg(req, "name");
    copy.channelNames[ch].trim();
    String err = saveFixture(copy);
    if (!err.isEmpty()) return sendMessage(req, 400, "Not saved", err);
    fp = findFixture(copy.id);  // saveFixture re-sorts the list
  }
  renderChannel(req, *fp, ch);
}

static const char NAME_PRESETS[] PROGMEM = R"JS(<script>
const presets={
'SharpyPlus':['Cyna','Magenta','Yellow','Colour1','Colour2','Colour3','Strobe','Dimmer','Dimmer Fine','Gobo','Animation','Ani Rot','Gobo2','Gobo2 Rot','Rot Fine','4Prism'],
'Beam330':['Color','Strobe','Dimmer','Gobo','Gobo Rot','Prism','Prism Rot','Zoom1','Zoom2','Pan','Pan Fine','Tilt','Tilt Fine','Focus','Frost','Lamp Ctrl'],
'Beam230':['Pan','Tilt','Pan Fine','Tilt Fine','Dimmer','Strobe','Frost','Focus','Gobo','Gobo Shake','Color','??','Prism','Prism Rot','??','??'],
'Beam230neu':['Color','Strobe','Dimmer','Gobo','Prism','Prism Rot','PrismRot2','Bunt','Focus','Pan','Pan Fine','Tilt','Tilt Fine','PT Time','Reset','Lamp Ctrl'],
'Beam230v21':['Pan','Tilt','Pan Fine','Tilt Fine','XY Speed','Zoom','Strobe','Dimmer','Color','Gobo','Gobo2','Gobo Rot','Focus','Prism','Prism Rot','Reset']};
function applyPreset(n){const p=presets[n];document.querySelectorAll('input[data-ch]').forEach(el=>{const i=+el.dataset.ch;if(i<p.length)el.value=p[i];});}
</script>)JS";

static void handleFixtureNames(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "id").toInt());
  if (!fp || fp->defId != 0) return req->redirect("/");
  const Fixture& f = *fp;
  AsyncResponseStream* r = pageBegin(req, f.name + " – Channel names", NAME_PRESETS);
  crumbs(r, {{f.name, "/fixture?id=" + String(f.id)}, {"Channel names", ""}});
  r->print("<h1>Channel names</h1>");
  r->print("<div class='row'><span class='meta'>Template:</span>");
  const char* presetNames[] = {"SharpyPlus", "Beam330", "Beam230", "Beam230neu", "Beam230v21"};
  for (const char* p : presetNames) r->printf("<button class='btn' onclick='applyPreset(\"%s\")'>%s</button>", p, p);
  r->print("</div>");
  r->printf("<form class='f' action='/savechannelnames' method='post'><input type='hidden' name='id' value='%u'>", f.id);
  for (uint16_t i = 0; i < f.channelCount; i++) {
    String cur = i < f.channelNames.size() ? f.channelNames[i] : String("");
    r->printf("<label>Channel %u (DMX %u)</label><input data-ch='%u' name='n%u' maxlength='%d' value='%s' placeholder='Ch %u'>",
              i + 1, f.address + i, i, i, FIXTURE_NAME_MAX, esc(cur).c_str(), i + 1);
  }
  r->print("<div class='row'><button class='btn primary grow' type='submit'>Save</button></div></form>");
  pageEnd(req, r);
}

static void handleSaveChannelNames(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "id").toInt());
  if (!fp || fp->defId != 0) return req->redirect("/");
  Fixture f = *fp;
  f.channelNames.resize(f.channelCount);
  for (uint16_t i = 0; i < f.channelCount; i++) {
    String key = "n" + String(i);
    if (hasArg(req, key.c_str())) {
      String v = arg(req, key.c_str());
      v.trim();
      f.channelNames[i] = v;
    }
  }
  String err = saveFixture(f);
  if (!err.isEmpty()) return sendMessage(req, 400, "Not saved", err);
  req->redirect("/fixture?id=" + String(f.id));
}

// ---------------- definitions ----------------

static int defUsage(uint16_t defId) {
  int n = 0;
  for (const Fixture& f : fixtureList) if (f.defId == defId) n++;
  return n;
}

static void handleDefs(AsyncWebServerRequest* req) {
  AsyncResponseStream* r = pageBegin(req, "Definitions");
  crumbs(r, {{"Definitions", ""}});
  r->print("<h1>Fixture definitions</h1>");
  r->print("<div class='row'><a class='btn' href='/def/edit'>+ New definition</a><a class='btn' href='/defs/import'>Import</a></div>");
  if (fixtureDefs.empty()) r->print("<p class='muted'>No definitions yet.</p>");
  r->print("<div class='tiles'>");
  for (const FixtureDef& d : fixtureDefs) {
    int used = defUsage(d.id);
    r->printf("<a class='tile' href='/def/edit?id=%u'><div class='name'>%s</div><div class='meta'>%s%s%u ch · used by %d</div></a>",
              d.id, esc(d.name).c_str(), esc(d.manufacturer).c_str(), d.manufacturer.isEmpty() ? "" : " · ",
              (unsigned)d.channels.size(), used);
  }
  r->print("</div>");
  pageEnd(req, r);
}

static void handleDefEdit(AsyncWebServerRequest* req) {
  FixtureDef d;
  bool isNew = true;
  if (hasArg(req, "id")) {
    FixtureDef* existing = findDef(arg(req, "id").toInt());
    if (!existing) return sendMessage(req, 404, "Not found", "Definition does not exist.");
    d = *existing;
    isNew = false;
  }
  String text = defChannelsToText(d);
  int rows = max(8, (int)d.channels.size() + 3);
  if (rows > 30) rows = 30;
  AsyncResponseStream* r = pageBegin(req, isNew ? "New definition" : "Edit definition");
  crumbs(r, {{"Definitions", "/defs"}, {isNew ? String("New definition") : d.name, ""}});
  r->printf("<h1>%s</h1>", isNew ? "New definition" : esc(d.name).c_str());
  r->print("<form class='f' action='/def/save' method='post'>");
  if (!isNew) r->printf("<input type='hidden' name='id' value='%u'>", d.id);
  r->printf("<label>Name</label><input name='name' maxlength='%d' value='%s'>", FIXTURE_NAME_MAX, esc(d.name).c_str());
  r->printf("<label>Manufacturer</label><input name='manufacturer' maxlength='%d' value='%s'>",
            FIXTURE_NAME_MAX, esc(d.manufacturer).c_str());
  r->printf("<label>Channels (one line per channel)</label><textarea name='channels' rows='%d' "
            "placeholder='Pan&#10;Pan fine&#10;Dimmer&#10;Color | color | 0-9 White; 10-19 Red; 20-29 Blue'>%s</textarea>",
            rows, esc(text).c_str());
  r->print("<div class='help'>Format: <span class='mono'>Name | Type | Ranges</span>, type and ranges are optional. "
           "Ranges separated by <span class='mono'>;</span>, e.g. <span class='mono'>0-9 Open; 10-19 Gobo 1; 255 Reset</span>. "
           "Type <span class='mono'>fine:Pan</span> makes a 16-bit pair (\"Pan fine\" is detected automatically). "
           "A group name (Dimmer, Position, Gobo, Color, Beam, Focus, Control) as type, or <span class='mono'>Color/wheel</span>, "
           "sets the group explicitly.</div>");
  r->print("<div class='row'><button class='btn primary grow' type='submit'>Save</button></div></form>");
  if (!isNew) {
    r->printf("<div class='row'><a class='btn' href='/def/export?id=%u'>Export</a>"
              "<a class='btn danger' href='/def/delete?id=%u'>Delete</a></div>", d.id, d.id);
  }
  pageEnd(req, r);
}

static void handleDefSave(AsyncWebServerRequest* req) {
  FixtureDef d;
  if (hasArg(req, "id") && arg(req, "id").toInt() > 0) {
    FixtureDef* existing = findDef(arg(req, "id").toInt());
    if (!existing) return sendMessage(req, 404, "Not found", "Definition does not exist.");
    d = *existing;
  }
  d.name = arg(req, "name");
  d.manufacturer = arg(req, "manufacturer");
  String err = defChannelsFromText(arg(req, "channels"), d);
  if (err.isEmpty()) err = saveDef(d);
  if (!err.isEmpty()) return sendMessage(req, 400, "Not saved", err);
  req->redirect("/defs");
}

static void handleDefDelete(AsyncWebServerRequest* req) {
  FixtureDef* d = findDef(arg(req, "id").toInt());
  if (!d) return sendMessage(req, 404, "Not found", "Definition does not exist.");
  String remoteUsers;
#ifdef HAS_LORA
  remoteUsers = syncDefUsedBy(d->uid);
#endif
  if (!remoteUsers.isEmpty()) {
    return sendMessage(req, 409, "Not deleted", "Still used on other boxes: " + remoteUsers);
  }
  if (arg(req, "confirm") == "1") {
    String err = deleteDef(d->id);
    if (!err.isEmpty()) return sendMessage(req, 409, "Not deleted", err);
    return req->redirect("/defs");
  }
  int used = defUsage(d->id);
  AsyncResponseStream* r = pageBegin(req, "Delete definition");
  r->printf("<h1>Delete %s?</h1>", esc(d->name).c_str());
#ifdef HAS_LORA
  r->print("<p class='meta'>The definition is deleted on all linked boxes.</p>");
#endif
  if (used) r->printf("<div class='panel warn'>Still used by %d fixture(s); delete or change them first.</div>", used);
  r->print("<div class='row'>");
  if (!used) r->printf("<a class='btn danger' href='/def/delete?id=%u&confirm=1'>Delete</a>", d->id);
  r->printf("<a class='btn' href='/def/edit?id=%u'>Cancel</a></div>", d->id);
  pageEnd(req, r);
}

static void handleDefExport(AsyncWebServerRequest* req) {
  FixtureDef* d = findDef(arg(req, "id").toInt());
  if (!d) return sendMessage(req, 404, "Not found", "Definition does not exist.");
  JsonDocument doc;
  defToJson(*d, doc.to<JsonObject>());
  String out;
  serializeJsonPretty(doc, out);
  String fname = d->name;
  for (size_t i = 0; i < fname.length(); i++) {
    if (!isAlphaNumeric(fname[i]) && fname[i] != '-') fname.setCharAt(i, '_');
  }
  AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", out);
  resp->addHeader("Content-Disposition", "attachment; filename=\"" + fname + ".json\"");
  req->send(resp);
}

static const char IMPORT_SCRIPT[] PROGMEM = R"JS(<script>
function oflCapName(c){
  if(c.comment)return c.comment;
  let n=c.type||'';
  if(c.slotNumber!==undefined)n+=' '+c.slotNumber;
  if(c.effectName)n+=' '+c.effectName;
  if(c.speed)n+=' '+c.speed;
  return n.trim();
}
function oflConvert(o){
  const av=o.availableChannels||{};
  const fineOf={};
  for(const k in av)(av[k].fineChannelAliases||[]).forEach(a=>fineOf[a]=k);
  const modes=(o.modes||[]).map(m=>{const keys=m.channels||[];const chans=keys.map((key,i)=>{
    const c={index:i+1};
    if(typeof key!=='string'){c.name='(unsupported)';return c;}
    c.name=key;
    if(fineOf[key]){c.type='fine';return c;}
    const ch=av[key];
    if(!ch)return c;
    const caps=ch.capabilities||(ch.capability?[ch.capability]:[]);
    if(caps.length&&caps[0].type)c.type=caps[0].type.toLowerCase();
    const div=ch.dmxValueResolution==='16bit'?256:ch.dmxValueResolution==='24bit'?65536:1;
    if(ch.capabilities)c.ranges=ch.capabilities.filter(x=>Array.isArray(x.dmxRange)).map(x=>({from:Math.floor(x.dmxRange[0]/div),to:Math.floor(x.dmxRange[1]/div),name:oflCapName(x)}));
    return c;});
    keys.forEach((key,i)=>{if(typeof key==='string'&&fineOf[key]){const ci=keys.indexOf(fineOf[key]);if(ci>=0)chans[ci].fine=i+1;}});
    return {name:m.shortName||m.name||'',channels:chans};});
  const mf=(o.manufacturer&&o.manufacturer.name)||o.manufacturerKey||'';
  return {name:o.name||'Import',manufacturer:mf,modes};
}
// grandMA2 fixture type XML (one or several documents pasted together)
const MA_NAMES={PAN:'Pan',TILT:'Tilt',DIM:'Dimmer',SHUTTER:'Shutter',STROBE:'Strobe',COLOR1:'Color',COLOR2:'Color 2',
 GOBO1:'Gobo',GOBO2:'Gobo 2',GOBO1_POS:'Gobo Rot',GOBO2_POS:'Gobo 2 Rot',PRISMA1:'Prism',PRISMA1POS:'Prism Rot',
 FOCUS:'Focus',ZOOM:'Zoom',FROST:'Frost',IRIS:'Iris',POSITIONMSPEED:'PT Speed',EFFECT:'Effect',RESET:'Reset',
 COLORRGB1:'Red',COLORRGB2:'Green',COLORRGB3:'Blue',COLORRGB4:'White',COLORRGB5:'Amber',COLORRGB6:'UV',
 COLORMIXER:'Color Mix',CTO:'CTO',CTB:'CTB',CONTROL:'Control'};
const MA_GROUPS={POSITION:'position',DIMMER:'dimmer',GOBO:'gobo',COLOR:'color',BEAM:'beam',FOCUS:'focus',CONTROL:'control'};
function maAttrs(tag){const a={};tag.replace(/([\w:]+)="([^"]*)"/g,(m,k,v)=>{a[k]=v.replace(/&amp;/g,'&').replace(/&quot;/g,'"').replace(/&lt;/g,'<').replace(/&gt;/g,'>')});return a}
function maName(attr){const u=attr.toUpperCase();if(MA_NAMES[u])return MA_NAMES[u];
  return attr.charAt(0).toUpperCase()+attr.slice(1).toLowerCase()}
function maFixture(x){
  const ft=maAttrs((x.match(/<FixtureType\b[^>]*>/)||[''])[0]);
  const tagText=t=>{const m=x.match(new RegExp('<'+t+'>([^<]*)</'+t+'>'));return m?m[1].trim():''};
  const chans=[],used={};
  const re=/<ChannelType\b([^>]*)>([\s\S]*?)<\/ChannelType>/g;let m;
  while((m=re.exec(x))){
    const a=maAttrs(m[1]),coarse=+a.coarse,fine=+a.fine||0;if(!coarse)continue;
    let name=maName(a.attribute||'Ch'+coarse);
    while(used[name])name+='*';used[name]=1;
    const c={index:coarse,name,type:name.replace(/\*+$/,'').toLowerCase()};
    const g=MA_GROUPS[(a.preset||'').toUpperCase()];if(g)c.group=g;
    const div=fine?256:1,ranges=[];
    m[2].replace(/<ChannelSet\b([^>]*)\/?>/g,(t,attrs)=>{const s=maAttrs(attrs);if(s.from_dmx===undefined)return;
      ranges.push({from:Math.min(255,Math.floor(+s.from_dmx/div)),to:Math.min(255,Math.floor(+s.to_dmx/div)),name:s.name||''})});
    if(ranges.length)c.ranges=ranges;
    if(fine){c.fine=fine;chans[fine-1]={index:fine,name:name+' fine',type:'fine'}}
    chans[coarse-1]=c;
  }
  for(let i=0;i<chans.length;i++)if(!chans[i])chans[i]={index:i+1,name:'Ch '+(i+1)};
  const name=((ft.name||'Import')+(ft.mode?' '+ft.mode:'')).slice(0,32);
  return {name,manufacturer:tagText('manufacturer'),modes:[{name:ft.mode||(chans.length+'ch'),channels:chans}]};
}
function maConvert(text){
  const docs=text.split(/(?=<\?xml)/).filter(d=>/<FixtureType\b/.test(d));
  const out=[];docs.forEach(d=>d.split(/(?=<FixtureType\b)/).slice(1).forEach(f=>out.push(maFixture(f))));
  if(!out.length)throw new Error('no grandMA2 FixtureType found');
  return out.length==1?out[0]:out;
}
function toDmxBox(text){
  if(text.trim().startsWith('<'))return maConvert(text);
  const o=JSON.parse(text);
  return o&&o.availableChannels?oflConvert(o):o;
}
function loadFile(inp){
  const f=inp.files[0];if(!f)return;
  const ta=document.getElementById('json');ta.value='';
  [...inp.files].forEach(f=>{const rd=new FileReader();rd.onload=()=>{ta.value+=(ta.value?'\n':'')+rd.result};rd.readAsText(f)});
}
function doImport(ev){
  ev.preventDefault();
  const out=document.getElementById('out');
  let payload;
  try{payload=JSON.stringify(toDmxBox(document.getElementById('json').value));}
  catch(e){out.className='panel err';out.textContent='Not readable: '+e.message;return false;}
  fetch('/api/defs/import',{method:'POST',body:new URLSearchParams({json:payload,ajax:'1'})})
    .then(r=>r.json()).then(d=>{
      out.className='panel'+(d.ok?'':' err');
      out.innerHTML=d.ok?(d.created+' definition(s) imported. <a href="/defs">Definitions</a>'+(d.error?'<br><span class="warn">'+d.error+'</span>':'')):('Error: '+d.error);
    }).catch(e=>{out.className='panel err';out.textContent='Error: '+e});
  return false;
}
</script>)JS";

static void handleImportPage(AsyncWebServerRequest* req) {
  AsyncResponseStream* r = pageBegin(req, "Import definition", IMPORT_SCRIPT);
  crumbs(r, {{"Definitions", "/defs"}, {"Import", ""}});
  r->print("<h1>Import definition</h1>");
  r->print("<form class='f' action='/api/defs/import' method='post' onsubmit='return doImport(event)'>");
  r->print("<label>Files</label><input type='file' multiple accept='.json,.xml,application/json,text/xml' onchange='loadFile(this)'>");
  r->print("<label>or paste</label><textarea id='json' name='json' rows='14'></textarea>");
  r->print("<div class='help'>Supported: DmxBox export, labX fixture files, Open Fixture Library (JSON) and grandMA2 "
           "fixture types (XML, several files at once). Several modes become separate definitions.</div>");
  r->print("<div class='row'><button class='btn primary grow' type='submit'>Import</button></div></form>");
  r->print("<div id='out' style='display:block'></div>");
  pageEnd(req, r);
}

static void handleImportApi(AsyncWebServerRequest* req) {
  bool ajax = arg(req, "ajax") == "1";
  String err;
  int created = 0;
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, arg(req, "json"));
  if (de) {
    err = String("Invalid JSON: ") + de.c_str();
  } else {
    created = importDefs(doc.as<JsonVariantConst>(), err);
  }
  Serial.printf("Import: %d created%s%s\n", created, err.isEmpty() ? "" : ", error: ", err.c_str());
  if (!ajax) {
    if (created == 0) return sendMessage(req, 400, "Import failed", err);
    return req->redirect("/defs");
  }
  JsonDocument resp;
  resp["ok"] = created > 0;
  resp["created"] = created;
  if (!err.isEmpty()) resp["error"] = err;
  String out;
  serializeJson(resp, out);
  req->send(created > 0 ? 200 : 400, "application/json", out);
}

// ---------------- registration ----------------

void registerFixtureRoutes(AsyncWebServer& server) {
  registerAsset(server, "/s/app.css", "text/css", PAGE_CSS);
  registerAsset(server, "/s/monitor.js", "application/javascript", MONITOR_JS);
  registerAsset(server, "/s/home.js", "application/javascript", HOME_JS);
  registerFaderAssets(server);
  server.on("/", HTTP_GET, sendFixturesPage);
  server.on("/fixture/edit", HTTP_GET, handleFixtureEdit);
  server.on("/fixture/save", HTTP_ANY, handleFixtureSave);
  server.on("/fixture/delete", HTTP_GET, handleFixtureDelete);
  server.on("/fixturenames", HTTP_GET, handleFixtureNames);
  server.on("/savechannelnames", HTTP_ANY, handleSaveChannelNames);
  server.on("/channel", HTTP_GET, handleChannel);
  server.on("/defs/import", HTTP_GET, handleImportPage);
  server.on("/defs", HTTP_GET, handleDefs);
  server.on("/def/edit", HTTP_GET, handleDefEdit);
  server.on("/def/save", HTTP_ANY, handleDefSave);
  server.on("/def/delete", HTTP_GET, handleDefDelete);
  server.on("/def/export", HTTP_GET, handleDefExport);
  server.on("/api/defs/import", HTTP_POST, handleImportApi);
  server.on("/api/channel/values", HTTP_GET, handleChannelValuesApi);
  server.on("/api/channel/set", HTTP_ANY, handleChannelSetApi);
  server.on("/api/channel/clear", HTTP_POST, handleChannelClearApi);
  server.on("/api/preset", HTTP_ANY, handlePresetApi);
  // registered last: "/fixture" would otherwise also match "/fixture/..." prefixes
  server.on("/fixture", HTTP_GET, handleFixturePage);
}
