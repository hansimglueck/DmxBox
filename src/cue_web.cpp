#include "cue_web.h"
#include "cues.h"
#include "fixture_web.h"
#include <ArduinoJson.h>
#ifdef HAS_LORA
#include "sync.h"
#endif

static void announce(const CueTransport& t) {
#ifdef HAS_LORA
  syncTransport(t);
#endif
}

// seconds ("2.5") -> 0.1 s units
static uint16_t tenths(const String& s, uint16_t fallback) {
  if (s.isEmpty()) return fallback;
  float f = s.toFloat();
  if (f < 0) f = 0;
  if (f > 6000) f = 6000;
  return (uint16_t)lroundf(f * 10);
}

static String secs(uint16_t t) {
  String s = String(t / 10);
  if (t % 10) s += "." + String(t % 10);
  return s;
}

// ---------------- playback state ----------------

static void playJson(JsonDocument& doc) {
  PlayInfo p = cuesPlayInfo();
  doc["seq"] = p.seq;
  doc["seqName"] = p.seqName;
  doc["idx"] = p.idx;
  doc["n"] = p.count;
  doc["cue"] = p.cue;
  doc["cueName"] = p.cueName;
  doc["mode"] = p.mode == CUE_PLAY ? "play" : "hold";
  doc["fade"] = p.fade;
  doc["dur"] = p.dur;
  doc["ms"] = p.ms;
  doc["marks"] = cuesMarkCount();
  doc["showVer"] = cuesShowVersion();
  uint16_t remote = 0;
#ifdef HAS_LORA
  for (const RemoteBox& b : syncRemoteBoxes()) if (b.peer.online()) remote += b.markCount;
  CueStoreStatus ss = syncCueStoreStatus();
  if (ss.active) {
    JsonObject s = doc["store"].to<JsonObject>();
    s["total"] = ss.total;
    s["ok"] = ss.ok;
    s["pending"] = ss.pending;
    JsonArray c = s["counts"].to<JsonArray>();
    for (const auto& e : ss.counts) {
      JsonArray a = c.add<JsonArray>();
      a.add(e.first);
      a.add(e.second);
    }
    JsonArray f = s["failed"].to<JsonArray>();
    for (const String& n : ss.failed) f.add(n);
  }
#endif
  doc["rmarks"] = remote;
  JsonArray seqs = doc["seqs"].to<JsonArray>();
  for (const SeqMeta& s : cuesSequences()) {
    JsonArray a = seqs.add<JsonArray>();
    a.add(s.id);
    a.add(s.name);
  }
}

static void sendPlay(AsyncWebServerRequest* req, const String& err = "") {
  if (!err.isEmpty()) return req->send(400, "text/plain", err);
  JsonDocument doc;
  playJson(doc);
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

static void handlePlayApi(AsyncWebServerRequest* req) {
  if (req->method() == HTTP_GET) return sendPlay(req);
  String op = arg(req, "op");
  uint16_t a = arg(req, "arg").toInt();
  uint8_t code = op == "go"         ? OP_GO
                 : op == "back"     ? OP_BACK
                 : op == "skip"     ? OP_SKIP
                 : op == "skipback" ? OP_SKIP_BACK
                 : op == "play"     ? OP_PLAY
                 : op == "hold"     ? OP_HOLD
                 : op == "select"   ? OP_SELECT
                 : op == "goto"     ? OP_GOTO
                 : op == "discard"  ? OP_DISCARD
                                    : 0xFF;
  if (code == 0xFF) return sendPlay(req, "Unknown action");
  CueTransport t;
  String err = cuesTransport(code, a, t);
  if (err.isEmpty()) announce(t);
  sendPlay(req, err);
}

static void handleCueApi(AsyncWebServerRequest* req) {
  String op = arg(req, "op");
  if (op != "insert" && op != "update") return sendPlay(req, "Unknown action");
  CueStoreResult r = cuesStore(op == "insert", arg(req, "name"), tenths(arg(req, "fade"), 20), tenths(arg(req, "dur"), 50),
                               arg(req, "seqname"));
  if (!r.err.isEmpty() && !r.seq) return sendPlay(req, r.err);
#ifdef HAS_LORA
  syncCueStore(r);
#endif
  sendPlay(req, r.err);
}

static void handleSeqApi(AsyncWebServerRequest* req) {
  CueTransport t;
  String err = cuesNewSequence(arg(req, "name"), t);
  if (err.isEmpty()) announce(t);
  sendPlay(req, err);
}

// ---------------- home panel ----------------

static const char PLAY_HTML[] PROGMEM = R"HTML(
<div class='panel play' id='play'>
<div class='ktitle krow'><span>Playback</span><a href='/show'>Manage</a></div>
<div class='prow'><select id='pseq' aria-label='Sequence'></select><button class='btn' id='pplay' data-a='play'></button></div>
<div class='prow'><span id='pcue' class='pcue'>…</span><span id='ptime' class='meta mono'></span></div>
<div class='pbar'><i id='pbar'></i></div>
<div class='ptrans'>
<button class='btn' data-a='skipback' title='Skip back (no fade)'><svg viewBox='0 0 24 24'><path d='M6 5v14M19 5l-9 7 9 7z'/></svg></button>
<button class='btn' data-a='back' title='Back (with fade)'><svg viewBox='0 0 24 24'><path d='M15 5l-7 7 7 7'/></svg>Back</button>
<button class='btn' data-a='go' title='Go (with fade)'>Go<svg viewBox='0 0 24 24'><path d='M9 5l7 7-7 7'/></svg></button>
<button class='btn' data-a='skip' title='Skip forward (no fade)'><svg viewBox='0 0 24 24'><path d='M18 5v14M5 5l9 7-9 7z'/></svg></button>
</div>
<div id='pcount' class='meta pcount'></div>
<div class='pchg'>
<button class='btn' data-a='insert' title='Store the manual values as a new cue after the current one'>Save cue</button>
<button class='btn' data-a='update' id='pupd' title='Store the manual values into the current cue'>Update cue</button>
<button class='btn' data-a='discard' id='pdis' title='Drop the manual values and go back to the stored cue'>Clear</button></div>
<form id='pform' class='pform' style='display:none'>
<div id='pseqname'><label>New sequence</label><input name='seqname' maxlength='32' placeholder='Sequence name'></div>
<label>Cue name</label><input name='name' maxlength='32'>
<div class='prow'><span><label>Fade (s)</label><input name='fade' type='number' min='0' step='0.1'></span>
<span><label>Duration (s)</label><input name='dur' type='number' min='0.1' step='0.1'></span></div>
<p class='help'>The cue stores the manual values of all boxes and is inserted after the current cue.</p>
<div class='row'><button class='btn primary grow' type='submit'>Store cue</button><button class='btn' type='button' id='pcancel'>Cancel</button></div>
</form>
<div class='meta' id='pnote'></div>
</div>
)HTML";

static const char PLAY_JS[] PROGMEM = R"JS(<script>
(function(){
const $=id=>document.getElementById(id),F=$('pform');let st=null,at=0,pollT=0;
function h(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
const sec=t=>(t/10).toFixed(1).replace(/\.0$/,'');
const ICON={play:'<svg viewBox="0 0 24 24"><path d="M7 5l12 7-12 7z"/></svg>Play',hold:'<svg viewBox="0 0 24 24"><path d="M7 5v14M17 5v14"/></svg>Hold'};
function changes(){return st?st.marks+st.rmarks:0}
function render(j){
  st=j;at=Date.now();
  const sel=$('pseq'),key=JSON.stringify(j.seqs)+':'+j.seq;
  if(sel.dataset.k!==key&&document.activeElement!==sel){sel.dataset.k=key;
    sel.innerHTML=(j.seq?'':'<option value="">No sequence</option>')+j.seqs.map(s=>'<option value="'+s[0]+'"'+(s[0]==j.seq?' selected':'')+'>'+h(s[1])+'</option>').join('')+'<option value="new">+ New sequence…</option>'}
  $('pcue').textContent=!j.seq?'No sequence – store a cue to start one':j.idx>=0?'Cue '+(j.idx+1)+'/'+j.n+'  '+j.cueName:j.n?'No cue':'Empty sequence (all channels 0)';
  const pp=$('pplay');pp.innerHTML=j.mode==='play'?ICON.hold:ICON.play;pp.dataset.a=j.mode==='play'?'hold':'play';
  pp.disabled=j.n<2&&j.mode!=='play';pp.title=j.n<2?'Playing needs at least two cues':'';
  document.querySelectorAll('#play [data-a=skip],#play [data-a=skipback]').forEach(b=>b.disabled=j.n<2);
  document.querySelectorAll('#play [data-a=go],#play [data-a=back]').forEach(b=>b.disabled=!j.n);
  const c=changes();
  $('pupd').disabled=!c||j.idx<0;$('pdis').disabled=!c;
  const pc=$('pcount');pc.textContent=c?c+' manual value'+(c==1?'':'s'):'No manual values';pc.className='pcount '+(c?'warn':'meta');
  let note='';
  if(j.store){const s=j.store;note=(s.pending?'<span class="spin sm"></span> Storing… ':'Stored: ')+s.counts.map(e=>e[1]+' values on '+h(e[0])).join(', ');
    if(s.failed.length)note+=' <span class="err">– no answer from '+h(s.failed.join(', '))+'</span>'}
  $('pnote').innerHTML=note;
  tick();
  clearTimeout(pollT);pollT=setTimeout(poll,j.store&&j.store.pending?500:1000);
}
function tick(){
  if(!st)return;const ms=st.ms+(st.mode==='play'?Date.now()-at:0);
  const p=st.idx>=0&&st.dur&&st.mode==='play'?Math.min(1,ms/(st.dur*100)):0;
  $('pbar').style.width=(p*100)+'%';
  $('ptime').textContent=st.idx>=0?'fade '+sec(st.fade)+' s · '+sec(st.dur)+' s':'';
}
setInterval(tick,200);
function poll(){if(document.hidden){clearTimeout(pollT);pollT=setTimeout(poll,2000);return}
  fetch('/api/play',{cache:'no-store'}).then(r=>r.json()).then(render).catch(()=>{clearTimeout(pollT);pollT=setTimeout(poll,3000)})}
function post(url,text){
  return fetch(url,{method:'POST'}).then(r=>r.ok?r.json():r.text().then(t=>{throw t})).then(render).catch(t=>{alert(t);poll()});
}
const LEAVE=['go','back','skip','skipback','play','select','goto'];
function ok(op){const c=changes();return !(LEAVE.includes(op)&&c&&!confirm('Discard '+c+' manual value'+(c==1?'':'s')+'?'))}
function act(op,arg){if(!ok(op))return false;post('/api/play?op='+op+(arg!==undefined?'&arg='+arg:''));return true}
function showForm(){
  F.style.display='';$('pseqname').style.display=st&&st.seq?'none':'';
  F.name.value='Cue '+((st&&st.idx>=0?st.idx:-1)+2);
  F.fade.value=st&&st.idx>=0?sec(st.fade):'2';F.dur.value=st&&st.idx>=0?sec(st.dur):'5';
  (st&&st.seq?F.name:F.seqname).focus();
}
$('play').addEventListener('click',e=>{
  const b=e.target.closest('[data-a]');if(!b)return;const a=b.dataset.a;
  if(a==='insert')return showForm();
  if(a==='update')return post('/api/cue?op=update');
  act(a);
});
$('pcancel').onclick=()=>{F.style.display='none'};
F.addEventListener('submit',e=>{e.preventDefault();
  const q=new URLSearchParams({op:'insert',name:F.name.value,fade:F.fade.value,dur:F.dur.value,seqname:F.seqname.value});
  F.style.display='none';post('/api/cue?'+q)});
$('pseq').addEventListener('change',e=>{const v=e.target.value;
  if(v==='new'){const c=changes();
    const n=(!c||confirm('Discard '+c+' manual value'+(c==1?'':'s')+'?'))?prompt('Name of the new sequence (starts with all channels at 0):'):null;
    if(n)post('/api/seq?op=new&name='+encodeURIComponent(n));
    e.target.dataset.k='';e.target.blur();if(!n&&st)render(st);return}
  if(!act('select',v)){e.target.dataset.k='';render(st)}
  e.target.blur()});
poll();
})();
</script>)JS";

void renderPlaybackPanel(AsyncResponseStream* r) {
  r->print(FPSTR(PLAY_HTML));
  r->printf("<script src='%s'></script>", assetUrl("/s/play.js").c_str());
}

// ---------------- mini playback (fixture pages) ----------------

static const char MINI_HTML[] PROGMEM = R"HTML(<div class='mplay' id='mplay'>
<div class='mline'><span class='mcue'>…</span><span class='mcount warn'></span></div>
<div class='mbtns'>
<button class='btn' data-a='skipback' title='Skip back (no fade)'><svg viewBox='0 0 24 24'><path d='M6 5v14M19 5l-9 7 9 7z'/></svg></button>
<button class='btn' data-a='back' title='Back'><svg viewBox='0 0 24 24'><path d='M15 5l-7 7 7 7'/></svg></button>
<button class='btn' data-a='go' title='Go'><svg viewBox='0 0 24 24'><path d='M9 5l7 7-7 7'/></svg></button>
<button class='btn' data-a='skip' title='Skip forward (no fade)'><svg viewBox='0 0 24 24'><path d='M18 5v14M5 5l9 7-9 7z'/></svg></button>
<button class='btn' data-a='pp' title='Play / Hold'></button>
<span class='msep'></span>
<button class='btn' data-a='insert' title='Save cue (new cue after the current one)'><svg viewBox='0 0 24 24'><path d='M12 5v14M5 12h14'/></svg></button>
<button class='btn' data-a='update' title='Update cue'><svg viewBox='0 0 24 24'><path d='M5 12l5 5 9-10'/></svg></button>
<button class='btn' data-a='discard' title='Clear manual values'><svg viewBox='0 0 24 24'><path d='M6 6l12 12M18 6L6 18'/></svg></button>
</div></div>)HTML";

static const char MINI_JS[] PROGMEM = R"JS(<script>
(function(){
const M=document.getElementById('mplay');if(!M)return;let st=null,t=0;
const q=s=>M.querySelector(s);
const IC={play:'<svg viewBox="0 0 24 24"><path d="M7 5l12 7-12 7z"/></svg>',hold:'<svg viewBox="0 0 24 24"><path d="M7 5v14M17 5v14"/></svg>'};
const manual=()=>st?st.marks+st.rmarks:0;
function render(j){
  st=j;const c=manual();
  q('.mcue').textContent=!j.seq?'No sequence':j.idx>=0?(j.idx+1)+'/'+j.n+'  '+j.cueName:(j.n?'–/'+j.n:'Empty sequence');
  q('.mcount').textContent=c?c+' manual':'';
  const pp=q('[data-a=pp]');pp.innerHTML=j.mode==='play'?IC.hold:IC.play;pp.dataset.op=j.mode==='play'?'hold':'play';
  pp.disabled=j.n<2&&j.mode!=='play';
  M.querySelectorAll('[data-a=skip],[data-a=skipback]').forEach(b=>b.disabled=j.n<2);
  M.querySelectorAll('[data-a=go],[data-a=back]').forEach(b=>b.disabled=!j.n);
  q('[data-a=update]').disabled=!c||j.idx<0;q('[data-a=discard]').disabled=!c;
  clearTimeout(t);t=setTimeout(poll,1000);
}
function poll(){clearTimeout(t);if(document.hidden){t=setTimeout(poll,2000);return}
  fetch('/api/play',{cache:'no-store'}).then(r=>r.json()).then(render).catch(()=>{t=setTimeout(poll,3000)})}
function post(u){return fetch(u,{method:'POST'}).then(r=>r.ok?r.json():r.text().then(x=>{throw x})).then(render).catch(x=>{alert(x);poll()})}
const LEAVE=['go','back','skip','skipback','play'];
M.addEventListener('click',e=>{
  const b=e.target.closest('button[data-a]');if(!b)return;let a=b.dataset.a;if(a==='pp')a=b.dataset.op;
  const c=manual();
  if(LEAVE.includes(a)&&c&&!confirm('Discard '+c+' manual value'+(c==1?'':'s')+'?'))return;
  if(a==='insert'){
    let seqname='';
    if(!st||!st.seq){seqname=prompt('No sequence yet. Name of the new sequence:');if(!seqname)return}
    const n=prompt('Name of the new cue:','Cue '+((st&&st.idx>=0?st.idx:-1)+2));if(n===null)return;
    const p=new URLSearchParams({op:'insert',name:n,seqname,fade:st&&st.idx>=0?st.fade/10:2,dur:st&&st.idx>=0?st.dur/10:5});
    return post('/api/cue?'+p);
  }
  if(a==='update')return post('/api/cue?op=update');
  post('/api/play?op='+a);
});
poll();
})();
</script>)JS";

void renderMiniPlayback(AsyncResponseStream* r) {
  r->print(FPSTR(MINI_HTML));
  r->printf("<script src='%s'></script>", assetUrl("/s/miniplay.js").c_str());
}

// ---------------- show page ----------------

static void handleShowPage(AsyncWebServerRequest* req) {
  std::vector<SeqMeta> seqs = cuesSequences();
  PlayInfo p = cuesPlayInfo();
  uint16_t sel = hasArg(req, "seq") ? arg(req, "seq").toInt() : p.seq;
  const SeqMeta* cur = nullptr;
  for (const SeqMeta& s : seqs) if (s.id == sel) cur = &s;
  if (!cur && !seqs.empty()) cur = &seqs[0];
  uint16_t startup = cuesStartup();

  AsyncResponseStream* r = pageBegin(req, "Show");
  r->print("<h1>Show</h1><p class='help'>Sequences and their cues are shared by all linked boxes. Each box stores its "
           "own channels of a cue.</p><h2>Sequences</h2><div class='panel'><table>");
  if (seqs.empty()) r->print("<tr><td class='muted'>No sequence yet. Store a cue on the home page to start one.</td></tr>");
  for (const SeqMeta& s : seqs) {
    r->printf("<tr><td><a href='/show?seq=%u'>%s</a>%s</td><td class='meta'>%u cue%s%s</td></tr>", s.id,
              esc(s.name).c_str(), s.id == p.seq ? " <span class='ok meta'>active</span>" : "", (unsigned)s.cues.size(),
              s.cues.size() == 1 ? "" : "s", s.id == startup ? " · startup" : "");
  }
  r->print("</table></div>");
  r->print("<form class='row' method='post' action='/show/do'><input type='hidden' name='op' value='startup'>"
           "<label class='meta' for='st'>At startup</label><select id='st' name='seq' style='flex:1'>");
  r->printf("<option value='0'%s>Last active sequence</option>", startup == 0 ? " selected" : "");
  for (const SeqMeta& s : seqs) {
    r->printf("<option value='%u'%s>%s</option>", s.id, s.id == startup ? " selected" : "", esc(s.name).c_str());
  }
  r->print("</select><button class='btn' type='submit'>Set</button></form>");
  r->print("<form class='row' method='post' action='/show/do'><input type='hidden' name='op' value='newseq'>"
           "<input name='name' maxlength='32' placeholder='New sequence name' required style='flex:1'>"
           "<button class='btn' type='submit' onclick=\"return confirm('The new sequence starts with all channels at 0. Unsaved changes are discarded.')\">"
           "New sequence</button></form>");

  if (cur) {
    r->printf("<h2>%s</h2>", esc(cur->name).c_str());
    r->printf("<form class='row' method='post' action='/show/do'>"
              "<input type='hidden' name='seq' value='%u'><input name='name' maxlength='32' value='%s' style='flex:1' "
              "aria-label='Sequence name'><button class='btn' type='submit' name='op' value='rename'>Rename</button>",
              cur->id, esc(cur->name).c_str());
    if (cur->id != p.seq) {
      r->printf("<button class='btn danger' type='submit' name='op' value='delseq' "
                "onclick=\"return confirm('Delete this sequence and its cues on all boxes?')\">Delete</button>");
    }
    r->print("</form>");
    if (cur->cues.empty()) r->print("<p class='muted'>No cues yet.</p>");
    else {
      r->print("<div class='panel'><table class='cues'><tr><th>#</th><th>Name</th><th>Fade s</th><th>Dur s</th>"
               "<th title='Channels this box stores in the cue'>Ch</th><th></th></tr>");
      for (size_t i = 0; i < cur->cues.size(); i++) {
        const CueMeta& c = cur->cues[i];
        bool here = cur->id == p.seq && c.id == p.cue;
        r->printf("<tr%s><td class='mono'>%u</td>"
                  "<td><form id='c%u' method='post' action='/show/do'><input type='hidden' name='seq' value='%u'>"
                  "<input type='hidden' name='cue' value='%u'></form>"
                  "<input form='c%u' name='name' maxlength='32' value='%s' aria-label='Cue name'></td>"
                  "<td><input form='c%u' name='fade' type='number' min='0' step='0.1' value='%s' aria-label='Fade'></td>"
                  "<td><input form='c%u' name='dur' type='number' min='0.1' step='0.1' value='%s' aria-label='Duration'></td>"
                  "<td class='mono meta'>%u</td><td class='acts'>",
                  here ? " class='here'" : "", (unsigned)i + 1, c.id, cur->id, c.id, c.id, esc(c.name).c_str(), c.id,
                  secs(c.fade).c_str(), c.id, secs(c.dur).c_str(), cuesPartSize(cur->id, c.id));
        r->printf("<button class='btn sm' form='c%u' name='op' value='cue' type='submit'>Save</button>"
                  "<button class='btn sm' form='c%u' name='op' value='up' type='submit' title='Move up'>&uarr;</button>"
                  "<button class='btn sm' form='c%u' name='op' value='down' type='submit' title='Move down'>&darr;</button>",
                  c.id, c.id, c.id);
        if (cur->id == p.seq) {
          r->printf("<button class='btn sm' form='c%u' name='op' value='goto' type='submit' title='Go to this cue (hold)'>Go to</button>",
                    c.id);
        }
        r->printf("<button class='btn sm danger' form='c%u' name='op' value='delcue' type='submit' "
                  "onclick=\"return confirm('Delete this cue on all boxes?')\">Delete</button></td></tr>",
                  c.id);
      }
      r->print("</table></div>");
    }
    if (cur->id != p.seq) {
      r->printf("<form method='post' action='/show/do' class='row'><input type='hidden' name='op' value='select'>"
                "<input type='hidden' name='seq' value='%u'><button class='btn primary grow' type='submit'>Play this sequence</button></form>",
                cur->id);
    }
  }
  pageEnd(req, r);
}

static void handleShowDo(AsyncWebServerRequest* req) {
  String op = arg(req, "op");
  uint16_t seq = arg(req, "seq").toInt(), cue = arg(req, "cue").toInt();
  String err;
  CueTransport t;
  if (op == "startup") err = cuesSetStartup(seq);
  else if (op == "newseq") {
    err = cuesNewSequence(arg(req, "name"), t);
    if (err.isEmpty()) {
      announce(t);
      seq = t.seq;
    }
  } else if (op == "rename") err = cuesRenameSequence(seq, arg(req, "name"));
  else if (op == "delseq") {
    err = cuesDeleteSequence(seq);
    if (err.isEmpty()) seq = 0;
  } else if (op == "cue") {
    err = cuesEditCue(seq, cue, arg(req, "name"), tenths(arg(req, "fade"), 0), tenths(arg(req, "dur"), 10));
  } else if (op == "up" || op == "down") {
    err = cuesMoveCue(seq, cue, op == "up" ? -1 : 1);
  } else if (op == "delcue") {
    err = cuesDeleteCue(seq, cue);
  } else if (op == "goto") {
    err = cuesTransport(OP_GOTO, cue, t);
    if (err.isEmpty()) announce(t);
  } else if (op == "select") {
    err = cuesTransport(OP_SELECT, seq, t);
    if (err.isEmpty()) announce(t);
  } else {
    err = "Unknown action";
  }
  if (!err.isEmpty()) return sendMessage(req, 400, "Not done", err);
  req->redirect(seq ? "/show?seq=" + String(seq) : String("/show"));
}

void registerCueRoutes(AsyncWebServer& server) {
  registerAsset(server, "/s/play.js", "application/javascript", PLAY_JS);
  registerAsset(server, "/s/miniplay.js", "application/javascript", MINI_JS);
  server.on("/api/play", HTTP_ANY, handlePlayApi);
  server.on("/api/cue", HTTP_ANY, handleCueApi);
  server.on("/api/seq", HTTP_ANY, handleSeqApi);
  server.on("/show/do", HTTP_ANY, handleShowDo);
  server.on("/show", HTTP_GET, handleShowPage);
}
