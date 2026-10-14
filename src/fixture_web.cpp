#include "fixture_web.h"
#include "fixtures.h"
#include <ArduinoJson.h>

extern uint8_t data[];

static const char PAGE_CSS[] PROGMEM = R"CSS(
body{background:#1a1a1a;color:#e0e0e0;margin:0;padding:0 10px 30px}
html{font-family:Helvetica,Arial,sans-serif;text-align:center;font-size:20px}
h1,h2{color:#4CAF50}h2{margin:6px 0}
a{color:#4CAF50;text-decoration:none}
p,.meta{color:#aaa}.meta{font-size:16px}
.card{max-width:600px;margin:15px auto;padding:12px 15px;border:2px solid #4CAF50;background:#2a2a2a;border-radius:8px}
.warn{color:#ff9800}.err{color:#f44336}
.button{background:#4CAF50;border:none;color:#fff;padding:11px 15px;font-size:18px;margin:6px;cursor:pointer;border-radius:4px;display:inline-block}
.button:hover{opacity:.85}.b-blue{background:#2196F3}.b-orange{background:#ff9800}.b-red{background:#f44336}.b-grey{background:#555}
.b-sel{outline:3px solid #fff}
input,select,textarea{font-size:18px;padding:8px;background:#333;color:#e0e0e0;border:1px solid #555;border-radius:4px;box-sizing:border-box}
form.f{max-width:600px;margin:0 auto;text-align:left}
form.f label{display:block;margin:12px 0 4px;color:#aaa;font-size:16px}
form.f input,form.f select,form.f textarea{width:100%}
textarea{font-family:monospace;font-size:15px}
.channel{max-width:600px;margin:8px auto;padding:10px;border:1px solid #555;background:#2a2a2a;border-radius:4px;display:flex;justify-content:space-between;align-items:center;gap:10px}
.channel a{flex:1;text-align:left}
.ch-name{color:#aaa;font-size:16px;min-width:90px;text-align:left}
.ch-val{font-weight:bold;min-width:40px;text-align:right}
.ch-range{color:#2196F3;font-size:14px}
.help{font-size:14px;color:#888;text-align:left;max-width:600px;margin:6px auto}
)CSS";

static String esc(const String& s) {
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

static bool hasArg(AsyncWebServerRequest* r, const char* name) {
  return r->hasParam(name, true) || r->hasParam(name);
}

static String arg(AsyncWebServerRequest* r, const char* name) {
  if (r->hasParam(name, true)) return r->getParam(name, true)->value();
  if (r->hasParam(name)) return r->getParam(name)->value();
  return "";
}

static AsyncResponseStream* pageBegin(AsyncWebServerRequest* req, const String& title, const char* extraHead = nullptr) {
  AsyncResponseStream* r = req->beginResponseStream("text/html");
  r->print("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width, initial-scale=1'>"
           "<link rel='icon' href='data:,'><base target='_self'><title>");
  r->print(esc(title));
  r->print("</title><style>");
  r->print(FPSTR(PAGE_CSS));
  r->print("</style>");
  if (extraHead) r->print(extraHead);
  r->print("</head><body>");
  return r;
}

static void pageEnd(AsyncWebServerRequest* req, AsyncResponseStream* r) {
  r->print("</body></html>");
  req->send(r);
}

static void sendMessage(AsyncWebServerRequest* req, int code, const String& title, const String& msg) {
  AsyncResponseStream* r = pageBegin(req, title);
  r->setCode(code);
  r->printf("<h1 class='%s'>%s</h1>", code >= 400 ? "err" : "", esc(title).c_str());
  r->printf("<div class='card'>%s</div>", esc(msg).c_str());
  r->print("<a href='javascript:history.back()'><button class='button b-grey'>← Zurück</button></a>");
  r->print("<a href='/'><button class='button'>Fixtures</button></a>");
  pageEnd(req, r);
}

static String rangeLabel(const ChannelRange& r) {
  String s = r.name.isEmpty() ? String("") : r.name + " ";
  s += r.from == r.to ? "(" + String(r.from) + ")" : "(" + String(r.from) + "–" + String(r.to) + ")";
  return s;
}

static const ChannelRange* rangeForValue(const ChannelDef* c, uint8_t v) {
  if (!c) return nullptr;
  for (const ChannelRange& r : c->ranges) if (v >= r.from && v <= r.to) return &r;
  return nullptr;
}

static String fixtureTypeLabel(const Fixture& f) {
  if (f.defId == 0) return "Generisch, " + String(f.channelCount) + " Kanäle";
  const FixtureDef* d = findDef(f.defId);
  return d ? d->name + ", " + String(d->channels.size()) + " Kanäle" : String("?");
}

static String dmxRangeLabel(const Fixture& f) {
  uint16_t n = fixtureChannelCount(f);
  return n <= 1 ? "DMX " + String(f.address) : "DMX " + String(f.address) + "–" + String(f.address + n - 1);
}

// ---------------- fixture list ----------------

static const char STATUS_SCRIPT[] PROGMEM = R"JS(
<style>
#ci{position:fixed;top:10px;right:10px;padding:8px 15px;background:#2a2a2a;border-radius:20px;border:2px solid #4CAF50;font-size:14px;z-index:1000;display:flex;align-items:center;gap:8px}
.sd{width:10px;height:10px;border-radius:50%;background:#4CAF50}.sd.off{background:#f44336;animation:pl 1s infinite}
@keyframes pl{0%,100%{opacity:1}50%{opacity:.4}}
.info{max-width:600px;margin:15px auto;background:#2a2a2a;padding:15px;border-radius:8px;border:1px solid #555}
</style>
<script>
let fails=0;
function setConn(ok,msg){const i=document.getElementById('ci');i.querySelector('.sd').classList.toggle('off',!ok);i.querySelector('span').textContent=msg;i.style.borderColor=ok?'#4CAF50':'#f44336';}
function upd(){fetch('/status').then(r=>{if(!r.ok)throw 0;return r.json()}).then(d=>{document.getElementById('status-box').innerHTML=d.html;fails=0;setConn(true,'Connected')}).catch(()=>{fails++;setConn(false,fails<=5?'Reconnecting... ('+fails+'/5)':'Disconnected - Reload page')})}
setInterval(upd,5000);window.addEventListener('load',upd);
</script>
)JS";

void sendFixturesPage(AsyncWebServerRequest* request) {
  AsyncResponseStream* r = pageBegin(request, "DMX Fixtures", STATUS_SCRIPT);
  r->print("<div id='ci'><div class='sd'></div><span>Connected</span></div>");
  r->print("<h1>DMX Fixtures</h1>");
  r->print("<div id='status-box' class='info'>Loading status...</div>");
  r->print("<div>");
  r->print("<a href='/saveall'><button class='button b-orange'>💾 Save DMX values</button></a>");
  r->print("<a href='/resetconfirm'><button class='button b-red'>🔄 Reset DMX values</button></a>");
  r->print("<a href='/wifisetup'><button class='button b-blue'>📶 WiFi Setup</button></a>");
  r->print("<a href='/timesetup'><button class='button b-blue'>⏰ Time Setup</button></a>");
  r->print("</div><div>");
  r->print("<a href='/fixture/edit'><button class='button'>+ Fixture</button></a>");
  r->print("<a href='/defs'><button class='button b-grey'>📋 Definitionen</button></a>");
  r->print("</div>");

  if (fixtureList.empty()) {
    r->print("<p>Noch keine Fixtures angelegt.</p>");
  }
  for (const Fixture& f : fixtureList) {
    r->print("<div class='card'>");
    r->printf("<h2><a href='/fixture?id=%u'>%s</a></h2>", f.id, esc(f.name).c_str());
    r->printf("<div class='meta'>%s · %s</div>", dmxRangeLabel(f).c_str(), esc(fixtureTypeLabel(f)).c_str());
    if (fixtureOverlaps(f)) r->print("<div class='warn'>⚠ überlappt mit einem anderen Fixture</div>");
    r->printf("<a href='/fixture?id=%u'><button class='button'>Kanäle</button></a>", f.id);
    r->printf("<a href='/fixture/edit?id=%u'><button class='button b-blue'>Bearbeiten</button></a>", f.id);
    r->print("</div>");
  }
  pageEnd(request, r);
}

// ---------------- fixture edit / save / delete ----------------

static void handleFixtureEdit(AsyncWebServerRequest* req) {
  Fixture f;
  bool isNew = true;
  if (hasArg(req, "id")) {
    Fixture* existing = findFixture(arg(req, "id").toInt());
    if (!existing) return sendMessage(req, 404, "Nicht gefunden", "Fixture existiert nicht.");
    f = *existing;
    isNew = false;
  } else {
    uint16_t next = 1;
    for (const Fixture& g : fixtureList) {
      uint16_t end = g.address + fixtureChannelCount(g);
      if (end > next) next = end;
    }
    f.address = next > DMX_UNIVERSE_SIZE ? DMX_UNIVERSE_SIZE : next;
  }

  AsyncResponseStream* r = pageBegin(req, isNew ? "Neues Fixture" : "Fixture bearbeiten");
  r->printf("<h1>%s</h1>", isNew ? "Neues Fixture" : "Fixture bearbeiten");
  r->print("<form class='f' action='/fixture/save' method='post'>");
  if (!isNew) r->printf("<input type='hidden' name='id' value='%u'>", f.id);
  r->printf("<label>Name</label><input name='name' maxlength='%d' value='%s' placeholder='z.B. Spot links'>",
            FIXTURE_NAME_MAX, esc(f.name).c_str());
  r->printf("<label>Startadresse (1–512)</label><input name='address' type='number' min='1' max='512' value='%u'>", f.address);
  r->print("<label>Typ</label><select name='def' id='def' onchange='tg()'>");
  r->printf("<option value='0'%s>Generisch (Kanalanzahl frei)</option>", f.defId == 0 ? " selected" : "");
  for (const FixtureDef& d : fixtureDefs) {
    r->printf("<option value='%u'%s>%s (%u Kanäle)</option>", d.id, d.id == f.defId ? " selected" : "",
              esc(d.name).c_str(), (unsigned)d.channels.size());
  }
  r->print("</select>");
  r->printf("<div id='cnt'><label>Kanalanzahl</label><input name='count' type='number' min='1' max='512' value='%u'></div>",
            f.channelCount);
  r->print("<div style='text-align:center'><button class='button' type='submit'>Speichern</button></div></form>");
  r->print("<script>function tg(){document.getElementById('cnt').style.display=document.getElementById('def').value=='0'?'':'none'}tg()</script>");
  if (!isNew) {
    r->printf("<a href='/fixture/delete?id=%u'><button class='button b-red'>Löschen</button></a>", f.id);
  }
  r->print("<a href='/'><button class='button b-grey'>Abbrechen</button></a>");
  pageEnd(req, r);
}

static void handleFixtureSave(AsyncWebServerRequest* req) {
  Fixture f;
  if (hasArg(req, "id") && arg(req, "id").toInt() > 0) {
    Fixture* existing = findFixture(arg(req, "id").toInt());
    if (!existing) return sendMessage(req, 404, "Nicht gefunden", "Fixture existiert nicht.");
    f = *existing;
  }
  if (hasArg(req, "name")) f.name = arg(req, "name");
  if (hasArg(req, "address")) f.address = constrain(arg(req, "address").toInt(), 0, 9999);
  if (hasArg(req, "def")) f.defId = arg(req, "def").toInt();
  if (hasArg(req, "count")) f.channelCount = constrain(arg(req, "count").toInt(), 0, 9999);
  String err = saveFixture(f);
  if (!err.isEmpty()) return sendMessage(req, 400, "Nicht gespeichert", err);
  req->redirect("/");
}

static void handleFixtureDelete(AsyncWebServerRequest* req) {
  Fixture* f = findFixture(arg(req, "id").toInt());
  if (!f) return sendMessage(req, 404, "Nicht gefunden", "Fixture existiert nicht.");
  if (arg(req, "confirm") == "1") {
    String err = deleteFixture(f->id);
    if (!err.isEmpty()) return sendMessage(req, 500, "Fehler", err);
    return req->redirect("/");
  }
  AsyncResponseStream* r = pageBegin(req, "Fixture löschen");
  r->printf("<h1 class='err'>Fixture löschen?</h1><div class='card'><h2>%s</h2><div class='meta'>%s</div>"
            "<p>Die DMX-Werte der Kanäle bleiben erhalten.</p></div>",
            esc(f->name).c_str(), dmxRangeLabel(*f).c_str());
  r->printf("<a href='/fixture/delete?id=%u&confirm=1'><button class='button b-red'>Ja, löschen</button></a>", f->id);
  r->print("<a href='/'><button class='button b-grey'>Abbrechen</button></a>");
  pageEnd(req, r);
}

// ---------------- fixture channels ----------------

static void handleFixturePage(AsyncWebServerRequest* req) {
  Fixture* fp = findFixture(arg(req, "id").toInt());
  if (!fp) return req->redirect("/");
  const Fixture& f = *fp;
  AsyncResponseStream* r = pageBegin(req, f.name);
  r->printf("<h1>%s</h1>", esc(f.name).c_str());
  r->printf("<p>%s · %s</p>", dmxRangeLabel(f).c_str(), esc(fixtureTypeLabel(f)).c_str());
  r->print("<p><a href='/'>← Zurück</a></p>");
  r->printf("<a href='/fixture/edit?id=%u'><button class='button b-blue'>Bearbeiten</button></a>", f.id);
  if (f.defId == 0) {
    r->printf("<a href='/fixturenames?id=%u'><button class='button b-blue'>Kanalnamen</button></a>", f.id);
  }
  uint16_t n = fixtureChannelCount(f);
  for (uint16_t i = 0; i < n; i++) {
    uint16_t dmx = f.address + i;
    if (dmx < 1 || dmx > DMX_UNIVERSE_SIZE) continue;
    const ChannelRange* cr = rangeForValue(fixtureChannelDef(f, i), data[dmx]);
    r->print("<div class='channel'>");
    r->printf("<span class='ch-name'>%s</span>", esc(fixtureChannelName(f, i)).c_str());
    r->printf("<a href='/channel?fix=%u&ch=%u'>DMX %u</a>", f.id, i, dmx);
    if (cr && !cr->name.isEmpty()) r->printf("<span class='ch-range'>%s</span>", esc(cr->name).c_str());
    r->printf("<span class='ch-val'>%u</span></div>", data[dmx]);
  }
  pageEnd(req, r);
}

static void renderChannel(AsyncWebServerRequest* req, const Fixture& f, int ch) {
  uint16_t dmx = f.address + ch;
  const ChannelDef* cdef = fixtureChannelDef(f, ch);
  String chName = fixtureChannelName(f, ch);
  AsyncResponseStream* r = pageBegin(req, chName);
  if (f.defId == 0) {
    r->printf("<h1 style='cursor:pointer' onclick=\"document.getElementById('rf').style.display='block';"
              "document.getElementById('rn').select();\">%s</h1>", esc(chName).c_str());
    r->printf("<form id='rf' action='/channel' method='get' style='display:none;margin:10px'>"
              "<input type='hidden' name='fix' value='%u'><input type='hidden' name='ch' value='%d'>"
              "<input id='rn' type='text' name='name' value='%s' maxlength='%d'>"
              "<button class='button b-blue' type='submit'>OK</button></form>",
              f.id, ch, esc(chName).c_str(), FIXTURE_NAME_MAX);
  } else {
    r->printf("<h1>%s</h1>", esc(chName).c_str());
  }
  r->printf("<p>%s · DMX %u</p>", esc(f.name).c_str(), dmx);
  r->printf("<h2 style='cursor:pointer' onclick=\"document.getElementById('vf').style.display='block';"
            "document.getElementById('vn').select();\">%u</h2>", data[dmx]);
  r->printf("<form id='vf' action='/channel' method='get' style='display:none;margin:10px'>"
            "<input type='hidden' name='fix' value='%u'><input type='hidden' name='ch' value='%d'>"
            "<input id='vn' type='number' name='val' value='%u' min='0' max='255' style='width:100px'>"
            "<button class='button b-blue' type='submit'>OK</button></form>",
            f.id, ch, data[dmx]);
  r->printf("<p><a href='/fixture?id=%u'>← Zurück</a></p>", f.id);

  if (cdef && !cdef->ranges.empty()) {
    r->print("<div style='max-width:600px;margin:10px auto'>");
    for (const ChannelRange& cr : cdef->ranges) {
      bool sel = data[dmx] >= cr.from && data[dmx] <= cr.to;
      r->printf("<a href='/channel?fix=%u&ch=%d&val=%u'><button class='button b-blue%s'>%s</button></a>",
                f.id, ch, cr.from, sel ? " b-sel" : "", esc(rangeLabel(cr)).c_str());
    }
    r->print("</div>");
  }

  static const int deltas[] = {-50, -10, -5, -1, 50, 10, 5, 1};
  r->print("<div style='margin:20px'>");
  r->printf("<a href='/channel?fix=%u&ch=%d&val=0'><button class='button'>0</button></a>", f.id, ch);
  r->printf("<a href='/channel?fix=%u&ch=%d&val=255'><button class='button'>255</button></a><br>", f.id, ch);
  for (int i = 0; i < 8; i++) {
    r->printf("<a href='/channel?fix=%u&ch=%d&delta=%d'><button class='button'>%+d</button></a>%s",
              f.id, ch, deltas[i], deltas[i], i == 3 ? "<br>" : "");
  }
  r->print("</div>");
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
  } else if (hasArg(req, "delta")) {
    data[dmx] = constrain((int)data[dmx] + arg(req, "delta").toInt(), 0, 255);
  }
  if (hasArg(req, "name") && fp->defId == 0) {
    Fixture copy = *fp;
    if (copy.channelNames.size() < (size_t)ch + 1) copy.channelNames.resize(ch + 1);
    copy.channelNames[ch] = arg(req, "name");
    copy.channelNames[ch].trim();
    String err = saveFixture(copy);
    if (!err.isEmpty()) return sendMessage(req, 400, "Nicht gespeichert", err);
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
  AsyncResponseStream* r = pageBegin(req, f.name + " – Kanalnamen", NAME_PRESETS);
  r->printf("<h1>%s – Kanalnamen</h1>", esc(f.name).c_str());
  r->printf("<p><a href='/fixture?id=%u'>← Zurück</a></p>", f.id);
  r->print("<div><span class='meta'>Vorlage: </span>");
  const char* presetNames[] = {"SharpyPlus", "Beam330", "Beam230", "Beam230neu", "Beam230v21"};
  for (const char* p : presetNames) {
    r->printf("<button class='button b-grey' onclick='applyPreset(\"%s\")'>%s</button>", p, p);
  }
  r->print("</div>");
  r->printf("<form class='f' action='/savechannelnames' method='post'><input type='hidden' name='id' value='%u'>", f.id);
  for (uint16_t i = 0; i < f.channelCount; i++) {
    String cur = i < f.channelNames.size() ? f.channelNames[i] : String("");
    r->printf("<label>Kanal %u (DMX %u)</label><input data-ch='%u' name='n%u' maxlength='%d' value='%s' placeholder='Ch %u'>",
              i + 1, f.address + i, i, i, FIXTURE_NAME_MAX, esc(cur).c_str(), i + 1);
  }
  r->print("<div style='text-align:center'><button class='button' type='submit'>Speichern</button></div></form>");
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
  if (!err.isEmpty()) return sendMessage(req, 400, "Nicht gespeichert", err);
  req->redirect("/fixture?id=" + String(f.id));
}

// ---------------- definitions ----------------

static int defUsage(uint16_t defId) {
  int n = 0;
  for (const Fixture& f : fixtureList) if (f.defId == defId) n++;
  return n;
}

static void handleDefs(AsyncWebServerRequest* req) {
  AsyncResponseStream* r = pageBegin(req, "Definitionen");
  r->print("<h1>Fixture-Definitionen</h1><p><a href='/'>← Fixtures</a></p>");
  r->print("<a href='/def/edit'><button class='button'>+ Definition</button></a>");
  r->print("<a href='/defs/import'><button class='button b-blue'>Import</button></a>");
  if (fixtureDefs.empty()) r->print("<p>Noch keine Definitionen.</p>");
  for (const FixtureDef& d : fixtureDefs) {
    int used = defUsage(d.id);
    r->print("<div class='card'>");
    r->printf("<h2>%s</h2>", esc(d.name).c_str());
    r->printf("<div class='meta'>%s%s%u Kanäle · verwendet von %d Fixture%s</div>",
              esc(d.manufacturer).c_str(), d.manufacturer.isEmpty() ? "" : " · ",
              (unsigned)d.channels.size(), used, used == 1 ? "" : "s");
    r->printf("<a href='/def/edit?id=%u'><button class='button b-blue'>Bearbeiten</button></a>", d.id);
    r->printf("<a href='/def/export?id=%u'><button class='button b-grey'>Export</button></a>", d.id);
    r->printf("<a href='/def/delete?id=%u'><button class='button b-red'>Löschen</button></a>", d.id);
    r->print("</div>");
  }
  pageEnd(req, r);
}

static void handleDefEdit(AsyncWebServerRequest* req) {
  FixtureDef d;
  bool isNew = true;
  if (hasArg(req, "id")) {
    FixtureDef* existing = findDef(arg(req, "id").toInt());
    if (!existing) return sendMessage(req, 404, "Nicht gefunden", "Definition existiert nicht.");
    d = *existing;
    isNew = false;
  }
  String text = defChannelsToText(d);
  int rows = max(8, (int)d.channels.size() + 3);
  if (rows > 30) rows = 30;
  AsyncResponseStream* r = pageBegin(req, isNew ? "Neue Definition" : "Definition bearbeiten");
  r->printf("<h1>%s</h1>", isNew ? "Neue Definition" : "Definition bearbeiten");
  r->print("<form class='f' action='/def/save' method='post'>");
  if (!isNew) r->printf("<input type='hidden' name='id' value='%u'>", d.id);
  r->printf("<label>Name</label><input name='name' maxlength='%d' value='%s'>", FIXTURE_NAME_MAX, esc(d.name).c_str());
  r->printf("<label>Hersteller</label><input name='manufacturer' maxlength='%d' value='%s'>",
            FIXTURE_NAME_MAX, esc(d.manufacturer).c_str());
  r->printf("<label>Kanäle (eine Zeile pro Kanal)</label><textarea name='channels' rows='%d' "
            "placeholder='Pan&#10;Tilt&#10;Dimmer | dimmer&#10;Farbe | color | 0-9 Weiß; 10-19 Rot; 20-29 Blau'>%s</textarea>",
            rows, esc(text).c_str());
  r->print("<div class='help'>Format: <code>Name | Typ | Bereiche</code>. Typ und Bereiche sind optional. "
           "Bereiche durch <code>;</code> getrennt, z.B. <code>0-9 Offen; 10-19 Gobo 1; 255 Reset</code>.</div>");
  r->print("<div style='text-align:center'><button class='button' type='submit'>Speichern</button></div></form>");
  if (!isNew) r->printf("<a href='/def/delete?id=%u'><button class='button b-red'>Löschen</button></a>", d.id);
  r->print("<a href='/defs'><button class='button b-grey'>Abbrechen</button></a>");
  pageEnd(req, r);
}

static void handleDefSave(AsyncWebServerRequest* req) {
  FixtureDef d;
  if (hasArg(req, "id") && arg(req, "id").toInt() > 0) {
    FixtureDef* existing = findDef(arg(req, "id").toInt());
    if (!existing) return sendMessage(req, 404, "Nicht gefunden", "Definition existiert nicht.");
    d = *existing;
  }
  d.name = arg(req, "name");
  d.manufacturer = arg(req, "manufacturer");
  String err = defChannelsFromText(arg(req, "channels"), d);
  if (err.isEmpty()) err = saveDef(d);
  if (!err.isEmpty()) return sendMessage(req, 400, "Nicht gespeichert", err);
  req->redirect("/defs");
}

static void handleDefDelete(AsyncWebServerRequest* req) {
  FixtureDef* d = findDef(arg(req, "id").toInt());
  if (!d) return sendMessage(req, 404, "Nicht gefunden", "Definition existiert nicht.");
  if (arg(req, "confirm") == "1") {
    String err = deleteDef(d->id);
    if (!err.isEmpty()) return sendMessage(req, 409, "Nicht gelöscht", err);
    return req->redirect("/defs");
  }
  int used = defUsage(d->id);
  AsyncResponseStream* r = pageBegin(req, "Definition löschen");
  r->printf("<h1 class='err'>Definition löschen?</h1><div class='card'><h2>%s</h2>", esc(d->name).c_str());
  if (used) r->printf("<p class='warn'>Wird noch von %d Fixture(s) verwendet und kann erst danach gelöscht werden.</p>", used);
  r->print("</div>");
  if (!used) r->printf("<a href='/def/delete?id=%u&confirm=1'><button class='button b-red'>Ja, löschen</button></a>", d->id);
  r->print("<a href='/defs'><button class='button b-grey'>Abbrechen</button></a>");
  pageEnd(req, r);
}

static void handleDefExport(AsyncWebServerRequest* req) {
  FixtureDef* d = findDef(arg(req, "id").toInt());
  if (!d) return sendMessage(req, 404, "Nicht gefunden", "Definition existiert nicht.");
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
  const modes=(o.modes||[]).map(m=>({name:m.shortName||m.name||'',channels:(m.channels||[]).map((key,i)=>{
    const c={index:i+1};
    if(typeof key!=='string'){c.name='(nicht unterstützt)';return c;}
    c.name=key;
    if(fineOf[key]){c.type='fine';return c;}
    const ch=av[key];
    if(!ch)return c;
    const caps=ch.capabilities||(ch.capability?[ch.capability]:[]);
    if(caps.length&&caps[0].type)c.type=caps[0].type.toLowerCase();
    const div=ch.dmxValueResolution==='16bit'?256:ch.dmxValueResolution==='24bit'?65536:1;
    if(ch.capabilities)c.ranges=ch.capabilities.filter(x=>Array.isArray(x.dmxRange)).map(x=>({from:Math.floor(x.dmxRange[0]/div),to:Math.floor(x.dmxRange[1]/div),name:oflCapName(x)}));
    return c;})}));
  const mf=(o.manufacturer&&o.manufacturer.name)||o.manufacturerKey||'';
  return {name:o.name||'Import',manufacturer:mf,modes};
}
function toDmxBox(text){
  const o=JSON.parse(text);
  return o&&o.availableChannels?oflConvert(o):o;
}
function loadFile(inp){
  const f=inp.files[0];if(!f)return;
  const rd=new FileReader();rd.onload=()=>{document.getElementById('json').value=rd.result};rd.readAsText(f);
}
function doImport(ev){
  ev.preventDefault();
  const out=document.getElementById('out');
  let payload;
  try{payload=JSON.stringify(toDmxBox(document.getElementById('json').value));}
  catch(e){out.className='err';out.textContent='Ungültiges JSON: '+e.message;return false;}
  fetch('/api/defs/import',{method:'POST',body:new URLSearchParams({json:payload,ajax:'1'})})
    .then(r=>r.json()).then(d=>{
      out.className=d.ok?'':'err';
      out.innerHTML=d.ok?(d.created+' Definition(en) importiert. <a href="/defs">→ Definitionen</a>'+(d.error?'<br><span class="warn">'+d.error+'</span>':'')):('Fehler: '+d.error);
    }).catch(e=>{out.className='err';out.textContent='Fehler: '+e});
  return false;
}
</script>)JS";

static void handleImportPage(AsyncWebServerRequest* req) {
  AsyncResponseStream* r = pageBegin(req, "Definition importieren", IMPORT_SCRIPT);
  r->print("<h1>Definition importieren</h1><p><a href='/defs'>← Definitionen</a></p>");
  r->print("<form class='f' action='/api/defs/import' method='post' onsubmit='return doImport(event)'>");
  r->print("<label>JSON-Datei</label><input type='file' accept='.json,application/json' onchange='loadFile(this)'>");
  r->print("<label>oder JSON einfügen</label><textarea id='json' name='json' rows='14'></textarea>");
  r->print("<div class='help'>Unterstützt: DmxBox-Export, labX-Fixture-Dateien und Open Fixture Library (JSON). "
           "Mehrere Modi werden als einzelne Definitionen angelegt.</div>");
  r->print("<div style='text-align:center'><button class='button' type='submit'>Importieren</button></div></form>");
  r->print("<div id='out' class='card' style='min-height:1em'></div>");
  pageEnd(req, r);
}

static void handleImportApi(AsyncWebServerRequest* req) {
  bool ajax = arg(req, "ajax") == "1";
  String err;
  int created = 0;
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, arg(req, "json"));
  if (de) {
    err = String("Ungültiges JSON: ") + de.c_str();
  } else {
    created = importDefs(doc.as<JsonVariantConst>(), err);
  }
  Serial.printf("Import: %d created%s%s\n", created, err.isEmpty() ? "" : ", error: ", err.c_str());
  if (!ajax) {
    if (created == 0) return sendMessage(req, 400, "Import fehlgeschlagen", err);
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
  // registered last: "/fixture" would otherwise also match "/fixture/..." prefixes
  server.on("/fixture", HTTP_GET, handleFixturePage);
}
