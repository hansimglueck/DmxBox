#ifdef HAS_LORA
#include "sync_web.h"
#include "sync.h"
#include "lora_link.h"
#include "fixture_web.h"
#include <algorithm>
#include "fader_ui.h"

static String ago(uint32_t ms) {
  uint32_t s = (millis() - ms) / 1000;
  if (s < 120) return String(s) + " s";
  if (s < 7200) return String(s / 60) + " min";
  return String(s / 3600) + " h";
}

static void handleLinkPage(AsyncWebServerRequest* req) {
  LinkSettings s = syncSettings();
  AsyncResponseStream* r = pageBegin(req, "Verbund", "<meta http-equiv='refresh' content='10'>");
  r->print("<h1>Verbund (LoRa)</h1><p><a href='/'>← Fixtures</a></p>");

  r->print("<div class='card'>");
  r->printf("<h2>%s</h2>", esc(s.name).c_str());
  r->printf("<div class='meta'>Box-ID %04x · %s</div>", syncBoxId(),
            !s.enabled ? "LoRa aus" : loraLink.ready() ? "Funk bereit" : "<span class='err'>Funk-Fehler</span>");
  if (loraLink.ready()) {
    r->printf("<div class='meta'>Frontend %s · %d dBm · Sendezeit letzte Stunde %.2f %% (Limit 8 %%)</div>",
              loraLink.femName(), loraLink.txPower(), loraLink.airtimePercent());
    r->printf("<div class='meta'>gesendet %u · empfangen %u · Empfangsfehler %u · verworfen (Budget) %u</div>",
              loraLink.txCount, loraLink.rxCount, loraLink.rxErrors, loraLink.budgetDrops);
  }
  r->printf("<div class='meta'>Laufzeit %lu min · freier Speicher %u B (min. %u B)</div>", millis() / 60000,
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  r->print("<a href='/link/ping'><button class='button b-blue'>Ping an alle</button></a>");
  r->print("</div>");

  std::vector<PeerInfo> peers = syncPeers();
  r->printf("<h2>Andere Boxen (%u)</h2>", (unsigned)peers.size());
  if (peers.empty()) r->print("<p>Noch keine andere Box gehört.</p>");
  for (const PeerInfo& p : peers) {
    r->print("<div class='card'>");
    r->printf("<h2>%s</h2>", esc(p.name).c_str());
    r->printf("<div class='meta'>Box-ID %04x · %s · zuletzt vor %s</div>", p.id,
              p.online() ? "<span style='color:#4CAF50'>online</span>" : "<span class='err'>offline</span>",
              ago(p.lastSeenMs).c_str());
    r->printf("<div class='meta'>Wir hören sie: RSSI %.0f dBm, SNR %.1f dB</div>", p.rssi, p.snr);
    if (p.pingRttMs >= 0) {
      r->printf("<div class='meta'>Sie hören uns: RSSI %.0f dBm, SNR %.1f dB · Ping %d ms</div>",
                p.theirRssi, p.theirSnr, p.pingRttMs);
    }
    r->printf("<div class='meta'>%u Fixtures · läuft seit %u min</div>", p.fixCount, p.uptimeS / 60);
    r->print("</div>");
  }

  r->print("<h2>Einstellungen</h2><form class='f' action='/link/save' method='post'>");
  r->printf("<label>Name dieser Box</label><input name='name' maxlength='32' value='%s'>", esc(s.name).c_str());
  r->print("<label>Verbund-Passwort (bei allen Boxen gleich)</label><input name='password' type='password' placeholder='unverändert'>");
  r->printf("<label>Sendeleistung (dBm, 0–27)</label><input name='txPower' type='number' min='0' max='27' value='%d'>", s.txPower);
  r->printf("<label><input type='checkbox' name='enabled' value='1' style='width:auto'%s> LoRa aktiv</label>",
            s.enabled ? " checked" : "");
  if (s.password == "dmxbox") r->print("<p class='warn'>Es ist noch das Standard-Passwort gesetzt.</p>");
  r->print("<div style='text-align:center'><button class='button' type='submit'>Speichern</button></div></form>");
  pageEnd(req, r);
}

static void handleLinkSave(AsyncWebServerRequest* req) {
  LinkSettings s = syncSettings();
  bool wasEnabled = s.enabled;
  s.name = arg(req, "name");
  if (!arg(req, "password").isEmpty()) s.password = arg(req, "password");
  s.txPower = arg(req, "txPower").toInt();
  s.enabled = arg(req, "enabled") == "1";
  String err = syncSaveSettings(s);
  if (!err.isEmpty()) return sendMessage(req, 400, "Nicht gespeichert", err);
  if (s.enabled && !wasEnabled && !loraLink.ready()) {
    return sendMessage(req, 200, "Gespeichert", "LoRa wird beim nächsten Neustart eingeschaltet.");
  }
  req->redirect("/link");
}

static void handleLinkPing(AsyncWebServerRequest* req) {
  syncPing();
  AsyncResponseStream* r = pageBegin(req, "Ping", "<meta http-equiv='refresh' content='2;url=/link'>");
  r->print("<h1>Ping gesendet…</h1><p>Ergebnis erscheint gleich auf der Verbund-Seite.</p>");
  pageEnd(req, r);
}

// ---------------- remote fixtures ----------------

static String boxStatus(const RemoteBox& b) {
  String s = b.peer.online() ? "<span style='color:#4CAF50'>online</span>" : "<span class='err'>offline</span>";
  s += " · zuletzt vor " + ago(b.peer.lastSeenMs) + " · RSSI " + String((int)b.peer.rssi) + " dBm";
  return s;
}

static String remoteRange(const RemoteFixture& f) {
  return f.count <= 1 ? "DMX " + String(f.address)
                      : "DMX " + String(f.address) + "–" + String(f.address + f.count - 1);
}

static const RemoteFixture* findRemoteFixture(const RemoteBox& b, uint16_t id) {
  for (const RemoteFixture& f : b.fixtures) if (f.id == id) return &f;
  return nullptr;
}

static int pendingValue(const RemoteBox& b, uint16_t addr) {
  for (const auto& p : b.pending) if (p.first == addr) return p.second;
  return -1;
}

static bool isFailed(const RemoteBox& b, uint16_t addr) {
  return std::find(b.failed.begin(), b.failed.end(), addr) != b.failed.end();
}

void renderLocalBoxHeader(AsyncResponseStream* r) {
  r->printf("<h2>Diese Box: %s</h2>", esc(syncSettings().name).c_str());
}

void renderRemoteFixtures(AsyncResponseStream* r) {
  for (const RemoteBox& b : syncRemoteBoxes()) {
    r->printf("<h2>📡 %s</h2><div class='meta'>%s</div>", esc(b.peer.name).c_str(), boxStatus(b).c_str());
    if (!b.described) {
      r->printf("<p>%s</p>", b.loading ? "Fixture-Liste wird übertragen…" : "Fixture-Liste noch nicht empfangen.");
      continue;
    }
    if (b.fixtures.empty()) r->print("<p>Keine Fixtures.</p>");
    for (const RemoteFixture& f : b.fixtures) {
      r->print("<div class='card' style='border-color:#2196F3'>");
      r->printf("<h2><a href='/remote/fixture?box=%u&id=%u'>%s</a></h2>", b.peer.id, f.id, esc(f.name).c_str());
      r->printf("<div class='meta'>%s · %u Kanäle</div>", remoteRange(f).c_str(), f.count);
      r->printf("<a href='/remote/fixture?box=%u&id=%u'><button class='button b-blue'>Kanäle</button></a>", b.peer.id, f.id);
      r->print("</div>");
    }
  }
}

static int remoteActive(const RemoteBox& box, uint16_t fixtureId, uint8_t group) {
  for (const RemoteRef& r : box.refs) {
    if (r.fixtureId == fixtureId && r.group == group) return r.presetId;
  }
  return -1;
}

static String remoteRefsJson(const RemoteBox& box, const RemoteFixture& f) {
  String s = "{";
  bool first = true;
  for (const RemoteRef& r : box.refs) {
    if (r.fixtureId != f.id) continue;
    if (!first) s += ",";
    s += "\"" + String(r.group) + "\":" + String(r.presetId);
    first = false;
  }
  return s + "}";
}

// Channel values of a remote fixture (pending values win), or pending flags when flags == true.
static String remoteValuesJson(const RemoteBox& box, const RemoteFixture& f, bool flags) {
  String s = "[";
  for (uint16_t i = 0; i < f.count; i++) {
    uint16_t dmx = f.address + i;
    int pend = dmx <= 512 ? pendingValue(box, dmx) : -1;
    if (i) s += ",";
    if (flags) s += pend >= 0 ? "1" : "0";
    else s += String(pend >= 0 ? pend : (dmx <= 512 ? box.values[dmx] : 0));
  }
  return s + "]";
}

static bool loadRemote(AsyncWebServerRequest* req, RemoteBox& box, const RemoteFixture*& fix) {
  if (!syncRemoteBox(arg(req, "box").toInt(), box)) return false;
  fix = findRemoteFixture(box, arg(req, "id").toInt());
  return fix != nullptr;
}

static void handleRemoteFixture(AsyncWebServerRequest* req) {
  RemoteBox box;
  const RemoteFixture* f;
  if (!loadRemote(req, box, f)) return req->redirect("/");
  AsyncResponseStream* r = pageBegin(req, f->name, FADER_HEAD);
  r->printf("<h1>%s</h1>", esc(f->name).c_str());
  r->printf("<p>📡 %s · %s</p><div class='meta'>%s</div>", esc(box.peer.name).c_str(), remoteRange(*f).c_str(),
            boxStatus(box).c_str());
  r->print("<p><a href='/'>← Zurück</a></p>");
  if (!box.haveValues) r->print("<p class='warn'>Werte noch nicht empfangen.</p>");
  if (!box.peer.online()) r->print("<p class='err'>Box ist offline, Änderungen werden nicht übertragen.</p>");
  if (!box.lastError.isEmpty()) r->printf("<p class='err'>%s meldet: %s</p>", esc(box.peer.name).c_str(), esc(box.lastError).c_str());
  String q = "box=" + String(box.peer.id) + "&id=" + String(f->id);
  std::vector<uint8_t> present = groupsPresent(f->groups);
  faderTabs(r, present);
  FaderGrid grid;
  grid.setUrl = "/api/remote/set?" + q;
  grid.getUrl = "/api/remote/values?" + q;
  grid.presetUrl = "/api/remote/preset?" + q;
  grid.pollMs = 1000;
  grid.reloadDelayMs = 1500;
  grid.valuesJson = remoteValuesJson(box, *f, false);
  grid.refsJson = remoteRefsJson(box, *f);
  faderGridBegin(r, grid);
  for (const FixtureControl& c : controlsFromFine(f->fine)) {
    String name = c.ch < f->channelNames.size() ? f->channelNames[c.ch] : "Ch " + String(c.ch + 1);
    faderCard(r, name, "/remote/channel?" + q + "&ch=" + String(c.ch), c.ch, c.fine,
              c.ch < f->groups.size() ? f->groups[c.ch] : GROUP_CONTROL, f->address + c.ch,
              c.fine >= 0 ? f->address + c.fine : -1, nullptr);
  }
  faderGridEnd(r);
  for (uint8_t g : present) {
    std::vector<PresetInfo> list;
    for (const RemotePreset& p : f->presets) {
      if (p.group == g) list.push_back({p.id, p.name, p.group, p.stored});
    }
    faderPresetPanel(r, g, list, remoteActive(box, f->id, g));
  }
  faderScript(r);
  pageEnd(req, r);
}

static void handleRemoteValuesApi(AsyncWebServerRequest* req) {
  RemoteBox box;
  const RemoteFixture* f;
  if (!loadRemote(req, box, f)) return req->send(404, "application/json", "{}");
  req->send(200, "application/json",
            "{\"v\":" + remoteValuesJson(box, *f, false) + ",\"p\":" + remoteValuesJson(box, *f, true) +
                ",\"r\":" + remoteRefsJson(box, *f) + ",\"on\":" + (box.peer.online() ? "1" : "0") + "}");
}

static void handleRemoteSetApi(AsyncWebServerRequest* req) {
  RemoteBox box;
  const RemoteFixture* f;
  if (!loadRemote(req, box, f) || !hasArg(req, "v")) return req->send(400, "text/plain", "bad request");
  int ch = arg(req, "ch").toInt();
  if (ch < 0 || ch >= f->count) return req->send(400, "text/plain", "bad channel");
  if (!box.peer.online()) return req->send(409, "text/plain", "offline");
  long v = arg(req, "v").toInt();
  uint16_t dmx = f->address + ch;
  int16_t fine = ch < (int)f->fine.size() ? f->fine[ch] : -1;
  if (fine >= 0) {
    v = constrain(v, 0, 65535);
    syncSetRemote(box.peer.id, dmx, v >> 8);
    syncSetRemote(box.peer.id, f->address + fine, v & 0xFF);
  } else {
    syncSetRemote(box.peer.id, dmx, constrain(v, 0, 255));
  }
  req->send(200, "text/plain", "ok");
}

static void handleRemoteChannel(AsyncWebServerRequest* req) {
  RemoteBox box;
  const RemoteFixture* f;
  if (!loadRemote(req, box, f)) return req->redirect("/");
  int ch = arg(req, "ch").toInt();
  if (ch < 0 || ch >= f->count) return req->redirect("/");
  uint16_t dmx = f->address + ch;
  if (dmx < 1 || dmx > 512) return req->redirect("/");

  int pend = pendingValue(box, dmx);
  int current = pend >= 0 ? pend : box.values[dmx];
  int target = -1;
  if (hasArg(req, "val")) target = constrain(arg(req, "val").toInt(), 0, 255);
  else if (hasArg(req, "delta")) target = constrain(current + arg(req, "delta").toInt(), 0, 255);
  if (target >= 0) {
    if (!box.peer.online()) return sendMessage(req, 409, "Box offline", box.peer.name + " ist gerade nicht erreichbar.");
    syncSetRemote(box.peer.id, dmx, target);
    // redirect so that reloading the page does not send the value again
    return req->redirect("/remote/channel?box=" + String(box.peer.id) + "&id=" + String(f->id) + "&ch=" + String(ch));
  }

  String name = ch < (int)f->channelNames.size() ? f->channelNames[ch] : "Ch " + String(ch + 1);
  AsyncResponseStream* r = pageBegin(req, name, pend >= 0 ? "<meta http-equiv='refresh' content='1'>" : nullptr);
  r->printf("<h1>%s</h1>", esc(name).c_str());
  r->printf("<p>📡 %s · %s · DMX %u</p>", esc(box.peer.name).c_str(), esc(f->name).c_str(), dmx);
  r->printf("<h2>%s</h2>", box.haveValues ? String(box.values[dmx]).c_str() : "?");
  if (pend >= 0) r->printf("<p class='ch-range'>→ %d wird übertragen…</p>", pend);
  else if (isFailed(box, dmx)) r->print("<p class='err'>Letzter Wert wurde nicht bestätigt.</p>");
  if (!box.peer.online()) r->print("<p class='err'>Box ist offline.</p>");
  r->printf("<form action='/remote/channel' method='get' style='margin:10px'>"
            "<input type='hidden' name='box' value='%u'><input type='hidden' name='id' value='%u'>"
            "<input type='hidden' name='ch' value='%d'>"
            "<input type='number' name='val' value='%d' min='0' max='255' style='width:100px'>"
            "<button class='button b-blue' type='submit'>Setzen</button></form>",
            box.peer.id, f->id, ch, current);
  r->printf("<p><a href='/remote/fixture?box=%u&id=%u'>← Zurück</a></p>", box.peer.id, f->id);

  String base = "/remote/channel?box=" + String(box.peer.id) + "&id=" + String(f->id) + "&ch=" + String(ch);
  static const int deltas[] = {-50, -10, -5, -1, 50, 10, 5, 1};
  r->print("<div style='margin:20px'>");
  r->printf("<a href='%s&val=0'><button class='button'>0</button></a>", base.c_str());
  r->printf("<a href='%s&val=255'><button class='button'>255</button></a><br>", base.c_str());
  for (int i = 0; i < 8; i++) {
    r->printf("<a href='%s&delta=%d'><button class='button'>%+d</button></a>%s", base.c_str(), deltas[i], deltas[i],
              i == 3 ? "<br>" : "");
  }
  r->print("</div>");
  pageEnd(req, r);
}

static void handleRemotePresetApi(AsyncWebServerRequest* req) {
  RemoteBox box;
  const RemoteFixture* f;
  if (!loadRemote(req, box, f)) return req->send(404, "text/plain", "Fixture nicht gefunden");
  if (!box.peer.online()) return req->send(409, "text/plain", "Box ist offline");
  String op = arg(req, "op");
  uint8_t code = op == "recall"   ? PRESET_RECALL
                 : op == "store"  ? PRESET_STORE
                 : op == "create" ? PRESET_CREATE
                 : op == "rename" ? PRESET_RENAME
                 : op == "delete" ? PRESET_DELETE
                                  : 0;
  if (!code) return req->send(400, "text/plain", "Unbekannte Aktion");
  syncPresetCommand(box.peer.id, code, f->id, arg(req, "g").toInt(), arg(req, "p").toInt(), arg(req, "name"));
  req->send(200, "text/plain", "queued");
}

void registerSyncRoutes(AsyncWebServer& server) {
  server.on("/api/remote/preset", HTTP_ANY, handleRemotePresetApi);
  server.on("/link/save", HTTP_ANY, handleLinkSave);
  server.on("/link/ping", HTTP_GET, handleLinkPing);
  server.on("/link", HTTP_GET, handleLinkPage);
  server.on("/remote/fixture", HTTP_GET, handleRemoteFixture);
  server.on("/remote/channel", HTTP_GET, handleRemoteChannel);
  server.on("/api/remote/values", HTTP_GET, handleRemoteValuesApi);
  server.on("/api/remote/set", HTTP_ANY, handleRemoteSetApi);
}
#endif
