#include "fader_ui.h"
#include "fixture_web.h"
#include <ArduinoJson.h>

const char FADER_HEAD[] PROGMEM = R"HTML(<style>
#ftabs{display:flex;flex-wrap:wrap;justify-content:center;gap:6px;max-width:980px;margin:12px auto 4px}
#ftabs button{background:#2a2a2a;border:1px solid #555;color:#e0e0e0;font-size:16px;padding:8px 12px;border-radius:8px;cursor:pointer}
#ftabs button.on{background:#4CAF50;border-color:#4CAF50;color:#fff}
#ftabs small{display:block;font-size:11px;color:#ffd54f;min-height:0}
#ftabs button.on small{color:#fff}
.fgrid{display:flex;flex-wrap:wrap;justify-content:center;gap:10px;max-width:980px;margin:10px auto}
.fcard{width:140px;background:#2a2a2a;border:1px solid #555;border-radius:10px;padding:8px;box-sizing:border-box}
.fname{font-size:15px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-bottom:6px}
.fenc{position:relative;height:220px;border-radius:12px;background:linear-gradient(180deg,#0f172a 0%,#1e293b 100%);
 border:2px solid #334155;cursor:grab;overflow:hidden;touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}
.fenc.drag{cursor:grabbing;border-color:#3b82f6}
.ftrk{position:absolute;top:12px;bottom:12px;left:50%;transform:translateX(-50%);width:6px;background:#475569;border-radius:3px;pointer-events:none}
.ffill{position:absolute;bottom:12px;left:50%;transform:translateX(-50%);width:6px;background:#3b82f6;border-radius:3px;pointer-events:none}
.find{position:absolute;left:50%;transform:translateX(-50%);width:44px;height:8px;background:#3b82f6;border-radius:4px;
 bottom:12px;box-shadow:0 0 8px rgba(59,130,246,.5);pointer-events:none}
.fval{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);font-size:28px;font-weight:700;color:#f1f5f9;
 text-shadow:0 2px 8px rgba(0,0,0,.6);pointer-events:none}
.fpct{position:absolute;top:calc(50% + 22px);left:50%;transform:translateX(-50%);font-size:12px;color:#94a3b8;pointer-events:none}
.fcard.pend .fval{color:#ff9800}
.frng{font-size:13px;color:#2196F3;min-height:17px;margin-top:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fsel{width:100%;font-size:14px;padding:4px;margin-top:4px}
.fgran{display:inline-flex;background:#0f172a;border-radius:8px;padding:2px;margin-top:6px}
.fgran button{background:transparent;border:none;color:#94a3b8;font-size:13px;padding:5px 9px;border-radius:6px;cursor:pointer}
.fgran button.on{background:#3b82f6;color:#fff}
.faddr{font-size:12px;color:#888;margin-top:4px}
.ppanel{max-width:980px;margin:14px auto;padding:10px;background:#222;border:1px solid #444;border-radius:10px}
.ptitle{color:#aaa;font-size:15px;margin-bottom:6px}
.plist{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;margin-bottom:6px}
.pitem{display:inline-flex;flex-direction:column;align-items:center}
.pbtn{min-width:90px;background:#37474f;border:2px solid #546e7a;color:#fff;font-size:16px;padding:12px 10px;border-radius:8px;cursor:pointer}
.pbtn.on{background:#ff9800;border-color:#ffb74d}
.pbtn.ns{background:#2a2a2a;border-style:dashed;color:#888}
.pedit{display:none;gap:2px;margin-top:3px}
.ppanel.editing .pedit{display:inline-flex}
.pedit button{background:#333;border:1px solid #555;color:#ddd;font-size:14px;padding:3px 7px;border-radius:5px;cursor:pointer}
</style>)HTML";

static const char FADER_JS[] PROGMEM = R"JS(<script>
(function(){
const G=document.getElementById('faders'),T=document.getElementById('ftabs');
const P=[...document.querySelectorAll('.ppanel')],cards=[...G.querySelectorAll('.fcard')];
let vals=JSON.parse(G.dataset.v||'[]'),pend=[],refs=JSON.parse(G.dataset.r||'{}'),drag=null,holdUntil=0;
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
}
function showRefs(){
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
function apply(c,v,now){v=Math.max(0,Math.min(maxOf(c),Math.round(v)));setLocal(c,v);show(c);touched(c);if(now)send(c,v);return v}
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
    vals=j.v;pend=j.p||[];refs=j.r||{};cards.forEach(show);showRefs()}).catch(()=>{});
}
function op(o,g,p,name,reload){
  let u=G.dataset.pre+'&op='+o+'&g='+g+(p!==undefined?'&p='+p:'')+(name!==undefined?'&name='+encodeURIComponent(name):'');
  fetch(u,{method:'POST'}).then(r=>r.ok?r.text():r.text().then(t=>{throw t}))
    .then(()=>setTimeout(()=>reload?location.reload():poll(true),+G.dataset.delay+150))
    .catch(t=>{alert('Fehler: '+t);poll(true)});
}
document.addEventListener('click',e=>{
  const pn=e.target.closest('.ppanel');if(!pn)return;const g=pn.dataset.g;
  const ed=e.target.closest('[data-op]');
  if(ed){
    const o=ed.dataset.op,it=ed.closest('.pitem'),p=it?it.dataset.p:undefined;
    if(o==='edit')pn.classList.toggle('editing');
    else if(o==='create'){const n=prompt('Name des neuen Presets:');if(n!==null)op('create',g,undefined,n,true)}
    else if(o==='store'){if(confirm('Preset mit den aktuellen Werten überspeichern?'))op('store',g,p,undefined,true)}
    else if(o==='rename'){const n=prompt('Neuer Name:',it.querySelector('.pbtn').textContent);if(n)op('rename',g,p,n,true)}
    else if(o==='delete'){if(confirm('Preset löschen?'))op('delete',g,p,undefined,true)}
    return;
  }
  const b=e.target.closest('.pbtn');if(!b)return;
  if(b.classList.contains('ns')){
    if(confirm('„'+b.textContent+'“ ist für dieses Fixture noch nicht gespeichert. Mit den aktuellen Werten speichern?'))op('store',g,b.dataset.p,undefined,true);
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
  r->print("<div id='ftabs'><button data-g='all'>Alle</button>");
  for (uint8_t g : groups) r->printf("<button data-g='%u'>%s<small></small></button>", g, GROUP_NAMES[g]);
  r->print("</div>");
}

void faderGridBegin(AsyncResponseStream* r, const FaderGrid& g) {
  r->printf("<div id='faders' class='fgrid' data-set='%s' data-get='%s' data-pre='%s' data-poll='%u' data-delay='%u' "
            "data-th='80' data-v='%s' data-r='%s'>",
            esc(g.setUrl).c_str(), esc(g.getUrl).c_str(), esc(g.presetUrl).c_str(), g.pollMs, g.reloadDelayMs,
            esc(g.valuesJson).c_str(), esc(g.refsJson).c_str());
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
  if (ranges && !ranges->empty()) {
    r->print("<select class='fsel'><option value=''>Bereich…</option>");
    for (const ChannelRange& cr : *ranges) {
      r->printf("<option value='%u'>%s</option>", cr.from, esc(cr.name.isEmpty() ? String(cr.from) : cr.name).c_str());
    }
    r->print("</select>");
  }
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

void faderPresetPanel(AsyncResponseStream* r, uint8_t group, const std::vector<PresetInfo>& list, int activeId) {
  r->printf("<div class='ppanel' data-g='%u' style='display:none'><div class='ptitle'>Presets %s%s</div><div class='plist'>",
            group, GROUP_NAMES[group], groupIsSelective(group) ? " (pro Fixture)" : " (pro Fixture-Typ)");
  if (list.empty()) r->print("<span class='meta'>Noch keine Presets in dieser Gruppe.</span>");
  for (const PresetInfo& p : list) {
    r->printf("<span class='pitem' data-p='%u'><button class='pbtn%s%s' data-p='%u'>%s</button>"
              "<span class='pedit'><button data-op='store' title='Überspeichern'>💾</button>"
              "<button data-op='rename' title='Umbenennen'>✎</button><button data-op='delete' title='Löschen'>🗑</button></span></span>",
              p.id, p.stored ? "" : " ns", (int)p.id == activeId ? " on" : "", p.id, esc(p.name).c_str());
  }
  r->print("</div><button class='button' data-op='create'>+ Preset</button>"
           "<button class='button b-grey' data-op='edit'>Bearbeiten</button></div>");
}

void faderScript(AsyncResponseStream* r) { r->print(FPSTR(FADER_JS)); }
