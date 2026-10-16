#ifdef HAS_LORA
#include "sync_web.h"
#include "sync.h"
#include "lora_link.h"
#include "fixture_web.h"
#include "fader_ui.h"
#include "cue_web.h"
#include <algorithm>

static String ago(uint32_t ms) {
  uint32_t s = (millis() - ms) / 1000;
  if (s < 120) return String(s) + " s";
  if (s < 7200) return String(s / 60) + " min";
  return String(s / 3600) + " h";
}

// ---------------- connectivity: LoRa section ----------------

void renderLoraSection(AsyncResponseStream* r) {
  LinkSettings s = syncSettings();
  r->print("<h2 id='lora'>LoRa link</h2><div class='panel'><table>");
  r->printf("<tr><th>This box</th><td>%s <span class='meta mono'>%04x</span> · <a href='#box'>rename</a></td></tr>",
            esc(s.name).c_str(), syncBoxId());
  r->printf("<tr><th>Radio</th><td>%s</td></tr>",
            !s.enabled ? "off" : loraLink.ready() ? "ready" : "<span class='err'>error</span>");
  if (loraLink.ready()) {
    r->printf("<tr><th>Front end</th><td class='mono'>%s · %d dBm · 869.525 MHz</td></tr>", loraLink.femName(), loraLink.txPower());
    r->printf("<tr><th>Airtime (1 h)</th><td class='mono'>%.2f %% of 8 %% budget</td></tr>", loraLink.airtimePercent());
    r->printf("<tr><th>Packets</th><td class='mono'>tx %u · rx %u · rx errors %u · dropped %u</td></tr>",
              loraLink.txCount, loraLink.rxCount, loraLink.rxErrors, loraLink.budgetDrops);
  }
  r->printf("<tr><th>Uptime / memory</th><td class='mono'>%lu min · %u B free (min %u B)", millis() / 60000,
            (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  if (ESP.getPsramSize()) r->printf(" · PSRAM %u of %u kB free", (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getPsramSize() / 1024));
  r->print("</td></tr></table></div>");

  std::vector<PeerInfo> peers = syncPeers();
  r->print("<div class='panel'><table><tr><th>Box</th><th>Status</th><th>Signal</th><th>Ping</th></tr>");
  if (peers.empty()) r->print("<tr><td colspan='4' class='muted'>No other box heard yet.</td></tr>");
  for (const PeerInfo& p : peers) {
    r->printf("<tr><td>%s<br><span class='meta mono'>%04x · %u fixtures</span></td>", esc(p.name).c_str(), p.id, p.fixCount);
    r->printf("<td>%s<br><span class='meta'>%s ago</span></td>", p.online() ? "online" : "<span class='err'>offline</span>",
              ago(p.lastSeenMs).c_str());
    r->printf("<td class='mono'>%.0f dBm<br><span class='meta'>SNR %.1f</span></td>", p.rssi, p.snr);
    if (p.pingRttMs >= 0) r->printf("<td class='mono'>%d ms<br><span class='meta'>they: %.0f dBm</span></td></tr>", p.pingRttMs, p.theirRssi);
    else r->print("<td class='muted'>–</td></tr>");
  }
  r->print("</table></div><div class='row'><a class='btn' href='/link/ping'>Ping all boxes</a></div>");

  r->print("<form class='f panel' action='/link/save' method='post'>");
  r->print("<label>Link password (same on all boxes)</label><input name='password' type='password' placeholder='unchanged'>");
  if (s.password == "dmxbox") r->print("<p class='warn meta'>The default password is still set.</p>");
  r->printf("<label>Transmit power (dBm, 0–27)</label><input name='txPower' type='number' min='0' max='27' value='%d'>", s.txPower);
  r->printf("<label><input type='checkbox' name='enabled' value='1'%s> LoRa enabled</label>", s.enabled ? " checked" : "");
  r->print("<div class='row'><button class='btn primary grow' type='submit'>Save LoRa settings</button></div></form>");
}

static void handleLinkSave(AsyncWebServerRequest* req) {
  LinkSettings s = syncSettings();
  bool wasEnabled = s.enabled;
  if (hasArg(req, "name")) s.name = arg(req, "name");
  if (!arg(req, "password").isEmpty()) s.password = arg(req, "password");
  s.txPower = arg(req, "txPower").toInt();
  s.enabled = arg(req, "enabled") == "1";
  String err = syncSaveSettings(s);
  if (!err.isEmpty()) return sendMessage(req, 400, "Not saved", err);
  if (s.enabled && !wasEnabled && !loraLink.ready()) {
    return sendMessage(req, 200, "Saved", "LoRa will be switched on at the next restart.");
  }
  req->redirect("/connectivity#lora");
}

static void handleLinkPing(AsyncWebServerRequest* req) {
  syncPing();
  sendProgress(req, "Ping", "Ping sent, results appear in the box table.", "/connectivity#lora", 2);
}

// ---------------- remote fixtures ----------------

static const RemoteFixture* findRemoteFixture(const RemoteBox& b, uint16_t id) {
  for (const RemoteFixture& f : b.fixtures) if (f.id == id) return &f;
  return nullptr;
}

static String remoteTypeLabel(const RemoteBox& b, const RemoteFixture& f) {
  if (f.defId == 0) return "Generic " + String(f.count) + " ch";
  for (const RemoteDef& d : b.defs) if (d.id == f.defId) return d.name;
  return "?";
}

static int pendingValue(const RemoteBox& b, uint16_t addr) {
  for (const auto& p : b.pending) if (p.first == addr) return p.second;
  return -1;
}

static bool isFailed(const RemoteBox& b, uint16_t addr) {
  return std::find(b.failed.begin(), b.failed.end(), addr) != b.failed.end();
}

static int remoteActive(const RemoteBox& box, uint16_t fixtureId, uint8_t group) {
  for (const RemoteRef& r : box.refs) {
    if (r.fixtureId == fixtureId && r.group == group) return r.presetId;
  }
  return -1;
}

void renderRemoteTiles(AsyncResponseStream* r) {
  for (const RemoteBox& b : syncRemoteBoxes()) {
    bool on = b.peer.online();
    if (!b.described) {
      r->printf("<div class='tile off'><div class='name'>%s</div><div class='meta'>%s</div></div>", esc(b.peer.name).c_str(),
                b.loading ? "receiving fixture list…" : on ? "fixture list not received yet" : "offline");
      continue;
    }
    for (const RemoteFixture& f : b.fixtures) {
      std::vector<TileGroup> groups;
      std::vector<FixtureControl> controls = controlsFromFine(f.fine);
      for (uint8_t g : groupsPresent(f.groups)) {
        TileGroup tg{g, "", {}};
        int active = remoteActive(b, f.id, g);
        for (const RemotePreset& p : f.presets) if ((int)p.id == active) tg.preset = p.name;
        for (const FixtureControl& c : controls) {
          if (c.ch >= f.groups.size() || f.groups[c.ch] != g) continue;
          uint16_t dmx = f.address + c.ch;
          tg.channels.push_back({c.ch < f.channelNames.size() ? f.channelNames[c.ch] : String(""),
                                 dmx <= 512 ? b.values[dmx] : (uint8_t)0, nullptr});
        }
        groups.push_back(tg);
      }
      fixtureTile(r, "/remote/fixture?box=" + String(b.peer.id) + "&id=" + String(f.id), f.name,
                  dmxRangeLabel(f.address, f.count), remoteTypeLabel(b, f), b.peer.name, "", groups, b.peer.id, !on);
    }
  }
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

static String remoteMarksJson(const RemoteBox& box, const RemoteFixture& f) {
  String s = "[";
  bool first = true;
  for (uint16_t i = 0; i < f.count; i++) {
    uint16_t a = f.address + i;
    if (a < 1 || a > 512 || !(box.marks[(a - 1) >> 3] & (1 << ((a - 1) & 7)))) continue;
    if (!first) s += ",";
    s += String(i);
    first = false;
  }
  return s + "]";
}

static String jsonEsc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
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
  AsyncResponseStream* r = pageBegin(req, f->name, faderHead().c_str());
  r->printf("<div class='titlebar'><h1>%s</h1><span class='row'><a class='btn' href='/fixture/edit?box=%u&id=%u'>Edit</a>"
            "</span></div>", esc(f->name).c_str(), box.peer.id, f->id);
  renderMiniPlayback(r);
  if (!box.haveValues) r->print("<p class='warn meta'>Values not received yet.</p>");
  r->printf("<p class='err meta boxoffmsg'>%s is offline, changes are not transmitted.</p>", esc(box.peer.name).c_str());
  if (!box.peer.online()) r->print("<script>document.body.classList.add('boxoff')</script>");
  if (!box.lastError.isEmpty()) r->printf("<p class='err meta'>%s reports: %s</p>", esc(box.peer.name).c_str(), esc(box.lastError).c_str());
  String q = "box=" + String(box.peer.id) + "&id=" + String(f->id);
  // named ranges: from the identical definition on this box (same CRC), if there is one
  std::vector<std::vector<ChannelRange>> ranges(f->count);
  if (f->defId) {
    for (const RemoteDef& rd : box.defs) {
      if (rd.id != f->defId) continue;
      fixturesLock();
      if (const FixtureDef* d = findDefByCrc(rd.crc)) {
        for (uint16_t i = 0; i < f->count && i < d->channels.size(); i++) ranges[i] = d->channels[i].ranges;
      }
      fixturesUnlock();
    }
  }
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
  grid.marksJson = remoteMarksJson(box, *f);
  grid.version = String(box.version);
  grid.box = box.peer.id;
  grid.boxName = box.peer.name;
  faderGridBegin(r, grid);
  for (const FixtureControl& c : controlsFromFine(f->fine)) {
    String name = c.ch < f->channelNames.size() ? f->channelNames[c.ch] : "Ch " + String(c.ch + 1);
    faderCard(r, name, "/remote/channel?" + q + "&ch=" + String(c.ch), c.ch, c.fine,
              c.ch < f->groups.size() ? f->groups[c.ch] : GROUP_CONTROL, f->address + c.ch,
              c.fine >= 0 ? f->address + c.fine : -1, ranges[c.ch].empty() ? nullptr : &ranges[c.ch]);
  }
  faderGridEnd(r);
  for (uint8_t g : present) {
    std::vector<PresetInfo> list;
    for (const RemotePreset& p : f->presets) {
      if (p.group == g) list.push_back({p.id, p.name, p.group, p.stored});
    }
    std::vector<RangeChannel> rcs;
    for (const FixtureControl& c : controlsFromFine(f->fine)) {
      if (ranges[c.ch].empty() || c.ch >= f->groups.size() || f->groups[c.ch] != g) continue;
      rcs.push_back({c.ch, c.fine, c.ch < f->channelNames.size() ? f->channelNames[c.ch] : "Ch " + String(c.ch + 1), &ranges[c.ch]});
    }
    faderPresetPanel(r, g, list, remoteActive(box, f->id, g), rcs);
  }
  r->printf("<p class='meta finfo'><span class='bx'>%s</span> · <span class='mono'>%s</span> · %s</p>",
            esc(box.peer.name).c_str(), dmxRangeLabel(f->address, f->count).c_str(), esc(remoteTypeLabel(box, *f)).c_str());
  faderScript(r);
  pageEnd(req, r);
}

static void handleRemoteValuesApi(AsyncWebServerRequest* req) {
  RemoteBox box;
  const RemoteFixture* f;
  if (!loadRemote(req, box, f)) return req->send(404, "application/json", "{}");
  req->send(200, "application/json",
            "{\"v\":" + remoteValuesJson(box, *f, false) + ",\"p\":" + remoteValuesJson(box, *f, true) +
                ",\"r\":" + remoteRefsJson(box, *f) + ",\"m\":" + remoteMarksJson(box, *f) + ",\"on\":" + (box.peer.online() ? "1" : "0") +
                ",\"ver\":" + String(box.version) + ",\"err\":\"" +
                (box.lastError.isEmpty() || millis() - box.lastErrorMs > 15000 ? String("") : jsonEsc(box.lastError)) + "\"}");
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
    if (!box.peer.online()) return sendMessage(req, 409, "Box offline", box.peer.name + " is not reachable.");
    syncSetRemote(box.peer.id, dmx, target);
    // redirect so that reloading the page does not send the value again
    return req->redirect("/remote/channel?box=" + String(box.peer.id) + "&id=" + String(f->id) + "&ch=" + String(ch));
  }

  String name = ch < (int)f->channelNames.size() ? f->channelNames[ch] : "Ch " + String(ch + 1);
  AsyncResponseStream* r = pageBegin(req, name, pend >= 0 ? "<meta http-equiv='refresh' content='1'>" : nullptr);
  r->printf("<p class='meta'><a href='/remote/fixture?box=%u&id=%u'>%s</a> · %s · <span class='mono'>DMX %u</span></p>",
            box.peer.id, f->id, esc(f->name).c_str(), esc(box.peer.name).c_str(), dmx);
  r->printf("<h1>%s</h1><div class='big'>%s</div>", esc(name).c_str(),
            box.haveValues ? String(box.values[dmx]).c_str() : "?");
  if (pend >= 0) r->printf("<p class='meta'><span class='spin sm'></span> sending %d…</p>", pend);
  else if (isFailed(box, dmx)) r->print("<p class='err meta'>The last value was not confirmed.</p>");
  if (!box.peer.online()) r->print("<p class='err meta'>Box is offline.</p>");
  String base = "/remote/channel?box=" + String(box.peer.id) + "&id=" + String(f->id) + "&ch=" + String(ch);
  r->printf("<form class='row' action='/remote/channel' method='get'><input type='hidden' name='box' value='%u'>"
            "<input type='hidden' name='id' value='%u'><input type='hidden' name='ch' value='%d'>"
            "<input type='number' name='val' value='%d' min='0' max='255' style='width:110px'>"
            "<button class='btn' type='submit'>Set</button></form>",
            box.peer.id, f->id, ch, current);
  static const int deltas[] = {-50, -10, -5, -1, 1, 5, 10, 50};
  r->print("<h2>Adjust</h2><div class='row'>");
  r->printf("<a class='btn' href='%s&val=0'>0</a>", base.c_str());
  for (int d : deltas) r->printf("<a class='btn mono' href='%s&delta=%d'>%+d</a>", base.c_str(), d, d);
  r->printf("<a class='btn' href='%s&val=255'>255</a></div>", base.c_str());
  pageEnd(req, r);
}

static void handleRemotePresetApi(AsyncWebServerRequest* req) {
  RemoteBox box;
  const RemoteFixture* f;
  if (!loadRemote(req, box, f)) return req->send(404, "text/plain", "Fixture not found");
  if (!box.peer.online()) return req->send(409, "text/plain", "Box is offline");
  String op = arg(req, "op");
  uint8_t code = op == "recall"   ? PRESET_RECALL
                 : op == "store"  ? PRESET_STORE
                 : op == "create" ? PRESET_CREATE
                 : op == "rename" ? PRESET_RENAME
                 : op == "delete" ? PRESET_DELETE
                                  : 0;
  if (!code) return req->send(400, "text/plain", "Unknown action");
  syncPresetCommand(box.peer.id, code, f->id, arg(req, "g").toInt(), arg(req, "p").toInt(), arg(req, "name"));
  req->send(200, "text/plain", "queued");
}

void registerSyncRoutes(AsyncWebServer& server) {
  server.on("/api/remote/preset", HTTP_ANY, handleRemotePresetApi);
  server.on("/api/link/pending", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"n\":" + String(syncPendingFor(arg(req, "box").toInt())) + "}");
  });
  server.on("/link/save", HTTP_ANY, handleLinkSave);
  server.on("/link/ping", HTTP_GET, handleLinkPing);
  server.on("/link", HTTP_GET, [](AsyncWebServerRequest* req) { req->redirect("/connectivity#lora"); });
  server.on("/remote/fixture", HTTP_GET, handleRemoteFixture);
  server.on("/remote/channel", HTTP_GET, handleRemoteChannel);
  server.on("/api/remote/values", HTTP_GET, handleRemoteValuesApi);
  server.on("/api/remote/set", HTTP_ANY, handleRemoteSetApi);
}
#endif
