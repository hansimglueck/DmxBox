#include "fader_ui.h"
#include "fixture_web.h"
#include <ArduinoJson.h>

static const char FADER_CSS[] PROGMEM = R"HTML(<style>
#ftabs{display:flex;gap:6px;margin:14px 0 4px}
#ftabs button{flex:1 1 0;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;background:var(--panel);
 border:1px solid var(--line);color:var(--text);font-size:14px;padding:7px 6px;border-radius:5px;cursor:pointer}
#ftabs small{overflow:hidden;text-overflow:ellipsis}
@media(max-width:600px){
 #ftabs{gap:4px}
 #ftabs button{writing-mode:vertical-rl;transform:rotate(180deg);height:92px;padding:6px 2px;font-size:13px;
  display:flex;align-items:center;justify-content:flex-start;gap:2px}
 #ftabs small{max-height:44px}
}
#ftabs button.on{border-color:var(--accent);color:var(--accent)}
#ftabs small{display:block;font-size:11px;color:var(--muted)}
.fgrid{display:flex;flex-wrap:wrap;gap:10px;margin:10px 0}
.fcard{width:140px;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:8px;text-align:center}
.fname{font-size:14px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-bottom:6px}
.fname a{color:var(--text)}
.fenc{position:relative;height:220px;border-radius:6px;background:#0d0f12;border:1px solid var(--line);cursor:grab;overflow:hidden;
 touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}
.fenc.drag{cursor:grabbing;border-color:var(--accent)}
.ftrk{position:absolute;top:12px;bottom:12px;left:50%;transform:translateX(-50%);width:4px;background:var(--line);border-radius:2px;pointer-events:none}
.ffill{position:absolute;bottom:12px;left:50%;transform:translateX(-50%);width:4px;background:var(--accent);border-radius:2px;pointer-events:none}
.find{position:absolute;left:50%;transform:translateX(-50%);width:44px;height:6px;background:var(--accent);border-radius:3px;bottom:12px;pointer-events:none}
.fval{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);font-size:26px;font-weight:600;color:var(--text);
 font-family:ui-monospace,Menlo,monospace;pointer-events:none}
.fpct{position:absolute;top:calc(50% + 20px);left:50%;transform:translateX(-50%);font-size:12px;color:var(--muted);pointer-events:none}
.fcard.pend .fval{color:var(--warn)}
.fcard.mk{border-color:var(--warn)}
.fcard.mk .faddr::after{content:' · changed';color:var(--warn)}
.fcard.pend .fname::after{content:'';display:inline-block;width:9px;height:9px;margin-left:6px;border:2px solid var(--line);
 border-top-color:var(--accent);border-radius:50%;animation:sp .8s linear infinite;vertical-align:-1px}
.frng{font-size:12px;color:var(--muted);min-height:16px;margin-top:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fsel{font-size:13px;padding:4px;margin-top:4px}
.fgran{display:inline-flex;border:1px solid var(--line);border-radius:5px;margin-top:6px}
.fgran button{background:transparent;border:none;color:var(--muted);font-size:12px;padding:4px 8px;cursor:pointer;font-family:ui-monospace,Menlo,monospace}
.fgran button.on{background:var(--accent);color:#fff}
.faddr{font-size:11px;color:var(--muted);margin-top:4px;font-family:ui-monospace,Menlo,monospace}
@media(max-width:600px){
 .fgrid{gap:6px}
 .fcard{width:calc((100% - 18px) / 4);padding:5px 3px}
 .fname{font-size:12px;margin-bottom:4px}
 .fenc{height:190px}
 .find{width:70%}
 .fval{font-size:17px}
 .fpct{font-size:10px;top:calc(50% + 14px)}
 .frng{font-size:10px}
 .fsel{font-size:11px;padding:2px}
 .fgran{display:flex}
 .fgran button{flex:1;font-size:10px;padding:4px 0}
 .faddr{font-size:9px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
}
.finfo{margin-top:18px}
.ppanel{margin:14px 0;padding:10px 12px;background:var(--panel);border:1px solid var(--line);border-radius:6px}
.ptitle{color:var(--muted);font-size:13px;margin-bottom:8px;text-transform:uppercase;letter-spacing:.05em}
.plist{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:8px}
.pitem{display:inline-flex;flex-direction:column;align-items:stretch}
.pbtn{min-width:90px;background:#0d0f12;border:1px solid var(--line);color:var(--text);font-size:15px;padding:11px 10px;border-radius:5px;cursor:pointer}
.pbtn.on{border-color:var(--accent);color:var(--accent);box-shadow:inset 0 0 0 1px var(--accent)}
.pbtn.ns{border-style:dashed;color:var(--muted)}
.pbtn.rng{min-width:0;font-size:14px;padding:9px 10px}
.rtitle{margin-top:14px}
.pedit{display:none;gap:2px;margin-top:3px}
.ppanel.editing .pedit{display:flex}
.pedit button{flex:1;background:transparent;border:1px solid var(--line);color:var(--muted);font-size:11px;padding:3px 4px;border-radius:4px;cursor:pointer}
</style>)HTML";

static const char FADER_JS[] PROGMEM = R"JS(<script>
(function(){
const G=document.getElementById('faders'),T=document.getElementById('ftabs');
const P=[...document.querySelectorAll('.ppanel')],cards=[...G.querySelectorAll('.fcard')];
let vals=JSON.parse(G.dataset.v||'[]'),pend=[],refs=JSON.parse(G.dataset.r||'{}'),drag=null,holdUntil=0;
let mk=new Set(JSON.parse(G.dataset.m||'[]'));
// range selects are built here from the card's ranges (keeps the page small)
cards.forEach(c=>{const r=JSON.parse(c.dataset.r||'[]');if(!r.length)return;const s=document.createElement('select');s.className='fsel';
  const esc=t=>String(t).replace(/[&<>"]/g,x=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[x]));
  s.innerHTML='<option value="">Range…</option>'+r.map(x=>'<option value="'+x[0]+'">'+esc(x[2]||x[0])+'</option>').join('');
  c.querySelector('.frng').after(s)});
const is16=c=>+c.dataset.f>=0;
const maxOf=c=>is16(c)?65535:255;
const valOf=c=>is16(c)?((vals[+c.dataset.c]||0)<<8)|(vals[+c.dataset.f]||0):(vals[+c.dataset.c]||0);
function setLocal(c,v){if(is16(c)){vals[+c.dataset.c]=v>>8;vals[+c.dataset.f]=v&255}else vals[+c.dataset.c]=v}
function show(c){
  const v=valOf(c),p=v/maxOf(c);
  c.querySelector('.find').style.bottom='calc(12px + (100% - 32px) * '+p+')';
  c.querySelector('.ffill').style.height='calc((100% - 24px) * '+p+')';
  c.querySelector('.fval').textContent=v;
  c.querySelector('.fpct').textContent=Math.round(p*100)+' %';
  const coarse=vals[+c.dataset.c]||0,r=JSON.parse(c.dataset.r||'[]');
  const hit=r.find(x=>coarse>=x[0]&&coarse<=x[1]);
  c.querySelector('.frng').textContent=hit?hit[2]:'';
  const sel=c.querySelector('.fsel');if(sel&&document.activeElement!==sel)sel.value=hit?String(hit[0]):'';
  c.classList.toggle('pend',!!(pend[+c.dataset.c]||(is16(c)&&pend[+c.dataset.f])));
  c.classList.toggle('mk',mk.has(+c.dataset.c)||(is16(c)&&mk.has(+c.dataset.f)));
}
// range presets: highlight the range the channel is in
function showRanges(){
  document.querySelectorAll('.pbtn.rng').forEach(b=>{const v=vals[+b.dataset.c]||0;b.classList.toggle('on',v>=+b.dataset.lo&&v<=+b.dataset.hi)});
}
function showRefs(){
  showRanges();
  P.forEach(p=>{const a=refs[p.dataset.g];p.querySelectorAll('.pbtn').forEach(b=>b.classList.toggle('on',a!==undefined&&+b.dataset.p===a))});
  if(T)T.querySelectorAll('button').forEach(b=>{const s=b.querySelector('small');if(!s)return;const a=refs[b.dataset.g];
    const pb=a!==undefined&&document.querySelector('.ppanel[data-g="'+b.dataset.g+'"] .pbtn[data-p="'+a+'"]');s.textContent=pb?pb.textContent:''});
}
function selectTab(g){
  if(T)T.querySelectorAll('button').forEach(b=>b.classList.toggle('on',b.dataset.g==g));
  cards.forEach(c=>c.style.display=(g==='all'||c.dataset.g==g)?'':'none');
  P.forEach(p=>p.style.display=p.dataset.g==g?'':'none');
  history.replaceState(null,'',location.pathname+location.search+(g==='all'?'':'#g'+g));
}
if(T)T.addEventListener('click',e=>{const b=e.target.closest('button');if(b)selectTab(b.dataset.g)});
// one request in flight at a time, so values can never arrive out of order; newest value per channel wins
let inflight=false;const queued=new Map();
function send(c,v){
  if(inflight){queued.set(c.dataset.c,[c,v]);return}
  inflight=true;
  fetch(G.dataset.set+'&ch='+c.dataset.c+'&v='+v,{method:'POST'}).catch(()=>{}).finally(()=>{
    inflight=false;const n=queued.entries().next();
    if(!n.done){queued.delete(n.value[0]);send(n.value[1][0],n.value[1][1])}
  });
}
function touched(c){if(refs[c.dataset.g]!==undefined){delete refs[c.dataset.g];showRefs()}}
function apply(c,v,now){v=Math.max(0,Math.min(maxOf(c),Math.round(v)));setLocal(c,v);mk.add(+c.dataset.c);show(c);touched(c);showRanges();if(now)send(c,v);return v}
const gran=c=>+(c.querySelector('.fgran .on')||{dataset:{g:1}}).dataset.g;
G.addEventListener('pointerdown',e=>{
  const enc=e.target.closest('.fenc');if(!enc)return;e.preventDefault();
  enc.setPointerCapture(e.pointerId);enc.classList.add('drag');
  const c=enc.closest('.fcard');drag={c,enc,y:e.clientY,v0:valOf(c),h:enc.clientHeight,last:0,pv:null};
  if(navigator.vibrate)navigator.vibrate(10);
});
G.addEventListener('pointermove',e=>{
  if(!drag)return;e.preventDefault();
  const c=drag.c,g=gran(c),px=Math.max(1,drag.h/(maxOf(c)/g));
  const v=apply(c,drag.v0+Math.round((drag.y-e.clientY)/px)*g,false);
  const t=Date.now();if(t-drag.last>+G.dataset.th){drag.last=t;send(c,v);drag.pv=null}else drag.pv=v;
});
function end(){if(!drag)return;if(drag.pv!==null)send(drag.c,drag.pv);drag.enc.classList.remove('drag');drag=null;holdUntil=Date.now()+1200}
G.addEventListener('pointerup',end);G.addEventListener('pointercancel',end);
G.addEventListener('wheel',e=>{const enc=e.target.closest('.fenc');if(!enc)return;e.preventDefault();
  const c=enc.closest('.fcard');apply(c,valOf(c)+(e.deltaY<0?1:-1)*gran(c),true);holdUntil=Date.now()+1200},{passive:false});
G.addEventListener('click',e=>{const b=e.target.closest('.fgran button');if(!b)return;
  b.parentNode.querySelectorAll('button').forEach(x=>x.classList.toggle('on',x===b))});
G.addEventListener('change',e=>{const s=e.target.closest('.fsel');if(!s||s.value==='')return;
  const c=s.closest('.fcard');apply(c,is16(c)?(+s.value)<<8:+s.value,true);holdUntil=Date.now()+1200;s.blur()});
function poll(force){
  if(!force&&(drag||Date.now()<holdUntil))return;
  fetch(G.dataset.get).then(r=>r.json()).then(j=>{if(drag||(!force&&Date.now()<holdUntil))return;
    if(j.ver!==undefined&&G.dataset.ver&&String(j.ver)!==G.dataset.ver){location.reload();return}
    if(j.on!==undefined)document.body.classList.toggle('boxoff',!j.on);
    vals=j.v;pend=j.p||[];refs=j.r||{};mk=new Set(j.m||[]);cards.forEach(show);showRefs()}).catch(()=>{});
}
let waitVerUntil=0;
// after a remote recall: keep the spinner until the box reports the preset as active
function waitRef(g,p,until){fetch(G.dataset.get).then(r=>r.json()).then(j=>{
  if((j.r||{})[g]===p||Date.now()>until){vals=j.v;pend=j.p||[];refs=j.r||{};mk=new Set(j.m||[]);cards.forEach(show);showRefs();busy(false)}
  else setTimeout(()=>waitRef(g,p,until),300)}).catch(()=>busy(false))}
// Remote boxes: keep a spinner up until the box confirmed over the link; list changes then wait
// for the new fixture list (a version change reloads the page).
function op(o,g,p,name,reload){
  const box=+G.dataset.box;
  let u=G.dataset.pre+'&op='+o+'&g='+g+(p!==undefined?'&p='+p:'')+(name!==undefined?'&name='+encodeURIComponent(name):'');
  if(box)busy(true,o==='recall'?'Recalling preset on '+G.dataset.boxname+'…':'Sending to '+G.dataset.boxname+'…');
  fetch(u,{method:'POST'}).then(r=>r.ok?r.text():r.text().then(t=>{throw t}))
    .then(()=>box?waitLink(box,12000):true)
    .then(ok=>{
      if(!box){reload?location.reload():poll(true);return}
      if(!ok){busy(false);alert('No answer from '+G.dataset.boxname);poll(true);return}
      fetch(G.dataset.get).then(r=>r.json()).then(j=>{
        if(j.err){busy(false);alert(G.dataset.boxname+': '+j.err);poll(true);return}
        if(o==='recall'){refs[g]=+p;showRefs();holdUntil=Date.now()+3000;waitRef(g,+p,Date.now()+3000);return}
        if(!reload){poll(true);setTimeout(()=>busy(false),200);return}
        waitVerUntil=Date.now()+8000;
      });
    })
    .catch(t=>{busy(false);alert('Error: '+t);poll(true)});
}
setInterval(()=>{if(!waitVerUntil)return;if(Date.now()>waitVerUntil)location.reload();else poll(true)},600);
document.addEventListener('click',e=>{
  const pn=e.target.closest('.ppanel');if(!pn)return;const g=pn.dataset.g;
  const ed=e.target.closest('[data-op]');
  if(ed){
    const o=ed.dataset.op,it=ed.closest('.pitem'),p=it?it.dataset.p:undefined;
    if(o==='edit')pn.classList.toggle('editing');
    else if(o==='create'){const n=prompt('Name of the new preset:');if(n!==null)op('create',g,undefined,n,true)}
    else if(o==='store'){if(confirm('Overwrite the preset with the current values?'))op('store',g,p,undefined,true)}
    else if(o==='rename'){const n=prompt('New name:',it.querySelector('.pbtn').textContent);if(n)op('rename',g,p,n,true)}
    else if(o==='delete'){if(confirm('Delete preset?'))op('delete',g,p,undefined,true)}
    return;
  }
  const b=e.target.closest('.pbtn');if(!b)return;
  if(b.classList.contains('rng')){
    const c=cards.find(x=>x.dataset.c===b.dataset.c);if(!c)return;
    const mid=Math.round((+b.dataset.lo + +b.dataset.hi)/2);
    apply(c,is16(c)?mid<<8:mid,true);holdUntil=Date.now()+1200;showRanges();return;
  }
  if(b.classList.contains('ns')){
    if(confirm('"'+b.textContent+'" is not stored for this fixture yet. Store the current values?'))op('store',g,b.dataset.p,undefined,true);
    return;
  }
  refs[g]=+b.dataset.p;showRefs();holdUntil=0;op('recall',g,b.dataset.p,undefined,false);
});
cards.forEach(show);showRefs();
selectTab(location.hash.startsWith('#g')?location.hash.slice(2):'all');
setInterval(()=>poll(false),+G.dataset.poll);
})();
</script>)JS";

std::vector<uint8_t> groupsPresent(const std::vector<uint8_t>& channelGroups) {
  std::vector<uint8_t> out;
  for (uint8_t g = 0; g < GROUP_COUNT; g++) {
    for (uint8_t cg : channelGroups) {
      if (cg == g) {
        out.push_back(g);
        break;
      }
    }
  }
  return out;
}

void faderTabs(AsyncResponseStream* r, const std::vector<uint8_t>& groups) {
  r->print("<div id='ftabs'><button data-g='all'>All</button>");
  for (uint8_t g : groups) r->printf("<button data-g='%u'>%s<small></small></button>", g, GROUP_NAMES[g]);
  r->print("</div>");
}

void faderGridBegin(AsyncResponseStream* r, const FaderGrid& g) {
  r->printf("<div id='faders' class='fgrid' data-set='%s' data-get='%s' data-pre='%s' data-poll='%u' data-delay='%u' "
            "data-th='80' data-v='%s' data-r='%s' data-m='%s' data-ver='%s' data-box='%u' data-boxname='%s'>",
            esc(g.setUrl).c_str(), esc(g.getUrl).c_str(), esc(g.presetUrl).c_str(), g.pollMs, g.reloadDelayMs,
            esc(g.valuesJson).c_str(), esc(g.refsJson).c_str(), esc(g.marksJson).c_str(), esc(g.version).c_str(), g.box,
            esc(g.boxName).c_str());
}

void faderCard(AsyncResponseStream* r, const String& name, const String& detailUrl, uint16_t ch, int16_t fine,
               uint8_t group, uint16_t address, int16_t fineAddress, const std::vector<ChannelRange>* ranges) {
  String rangesJson = "[]";
  if (ranges && !ranges->empty()) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const ChannelRange& cr : *ranges) {
      JsonArray e = arr.add<JsonArray>();
      e.add(cr.from);
      e.add(cr.to);
      e.add(cr.name);
    }
    rangesJson = "";
    serializeJson(doc, rangesJson);
  }
  r->printf("<div class='fcard' data-c='%u' data-f='%d' data-g='%u' data-r='%s'>", ch, fine, group,
            esc(rangesJson).c_str());
  if (detailUrl.isEmpty()) r->printf("<div class='fname'>%s</div>", esc(name).c_str());
  else r->printf("<div class='fname'><a href='%s'>%s</a></div>", esc(detailUrl).c_str(), esc(name).c_str());
  r->print("<div class='fenc'><div class='ftrk'></div><div class='ffill'></div><div class='find'></div>"
           "<div class='fval'>0</div><div class='fpct'></div></div><div class='frng'></div>");
  if (fine >= 0) {
    r->print("<div class='fgran'><button data-g='1'>1</button><button data-g='10'>10</button>"
             "<button data-g='100' class='on'>100</button></div>");
    r->printf("<div class='faddr'>DMX %u + %d · 16 Bit</div></div>", address, fineAddress);
  } else {
    r->print("<div class='fgran'><button data-g='1' class='on'>1</button><button data-g='5'>5</button>"
             "<button data-g='10'>10</button></div>");
    r->printf("<div class='faddr'>DMX %u</div></div>", address);
  }
}

void faderGridEnd(AsyncResponseStream* r) { r->print("</div>"); }

void faderPresetPanel(AsyncResponseStream* r, uint8_t group, const std::vector<PresetInfo>& list, int activeId,
                      const std::vector<RangeChannel>& rangeChannels) {
  r->printf("<div class='ppanel' data-g='%u' style='display:none'><div class='ptitle'>Presets %s%s</div><div class='plist'>",
            group, GROUP_NAMES[group], groupIsSelective(group) ? " – per fixture" : " – per fixture type");
  if (list.empty()) r->print(rangeChannels.empty() ? "<span class='meta'>No presets in this group yet.</span>"
                                                   : "<span class='meta'>No own presets yet.</span>");
  for (const PresetInfo& p : list) {
    r->printf("<span class='pitem' data-p='%u'><button class='pbtn%s%s' data-p='%u'>%s</button>"
              "<span class='pedit'><button data-op='store'>Store</button>"
              "<button data-op='rename'>Rename</button><button data-op='delete'>Delete</button></span></span>",
              p.id, p.stored ? "" : " ns", (int)p.id == activeId ? " on" : "", p.id, esc(p.name).c_str());
  }
  r->print("</div><div class='row'><button class='btn' data-op='create'>+ Preset</button>"
           "<button class='btn' data-op='edit'>Edit presets</button></div>");
  for (const RangeChannel& rc : rangeChannels) {
    if (!rc.ranges || rc.ranges->empty()) continue;
    r->printf("<div class='ptitle rtitle'>%s – from the definition</div><div class='plist'>", esc(rc.name).c_str());
    for (const ChannelRange& cr : *rc.ranges) {
      String label = cr.name.isEmpty() ? String(cr.from) + (cr.to != cr.from ? "–" + String(cr.to) : String("")) : cr.name;
      r->printf("<button class='pbtn rng' data-c='%u' data-lo='%u' data-hi='%u'>%s</button>", rc.ch, cr.from, cr.to,
                esc(label).c_str());
    }
    r->print("</div>");
  }
  r->print("</div>");
}

void faderScript(AsyncResponseStream* r) {
  r->printf("<script src='%s'></script>", assetUrl("/s/fader.js").c_str());
}

String faderHead() { return "<link rel='stylesheet' href='" + assetUrl("/s/fader.css") + "'>"; }

void registerFaderAssets(AsyncWebServer& server) {
  registerAsset(server, "/s/fader.css", "text/css", FADER_CSS);
  registerAsset(server, "/s/fader.js", "application/javascript", FADER_JS);
}
