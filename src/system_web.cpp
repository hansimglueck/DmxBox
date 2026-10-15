#include "system.h"
#include "fixture_web.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#ifdef HAS_LORA
#include "sync.h"
#include "sync_web.h"
#endif

static const char* const DAY_NAMES[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

#ifndef HAS_LORA
// Without the link, the name lives in its own small file (with LoRa it is part of the link settings).
static String storedBoxName;
static bool boxNameLoaded = false;
#endif

String localBoxName() {
#ifdef HAS_LORA
  return syncSettings().name;
#else
  if (!boxNameLoaded) {
    boxNameLoaded = true;
    File f = LittleFS.open("/box.json", "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) storedBoxName = doc["name"] | "";
      f.close();
    }
  }
  return storedBoxName.isEmpty() ? String(ssid) : storedBoxName;
#endif
}

String networkNameFor(const String& boxName) {
  String in = boxName, out;
  in.replace("ä", "ae");
  in.replace("ö", "oe");
  in.replace("ü", "ue");
  in.replace("Ä", "ae");
  in.replace("Ö", "oe");
  in.replace("Ü", "ue");
  in.replace("ß", "ss");
  for (size_t i = 0; i < in.length() && out.length() < 32; i++) {
    char c = in[i];
    if (isAlphaNumeric(c)) out += (char)tolower(c);
    else if (!out.isEmpty() && !out.endsWith("-")) out += '-';
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}

void applyStoredNetworkName() {
  // read the stored box name directly: the link/box settings are loaded later, after WiFi
  File f = LittleFS.open(
#ifdef HAS_LORA
      "/link.json",
#else
      "/box.json",
#endif
      "r");
  if (!f) return;
  JsonDocument doc;
  bool ok = !deserializeJson(doc, f);
  f.close();
  if (!ok) return;
  String net = networkNameFor(doc["name"] | "");
  if (net.isEmpty()) return;
  static char buf[33];
  strlcpy(buf, net.c_str(), sizeof(buf));
  ssid = buf;
}

static String saveBoxName(String name) {
  name.trim();
  if (name.isEmpty() || name.length() > 32) return "Name must be 1–32 characters";
  if (networkNameFor(name).isEmpty()) return "The name needs at least one letter or digit";
#ifdef HAS_LORA
  LinkSettings s = syncSettings();
  s.name = name;
  return syncSaveSettings(s);
#else
  JsonDocument doc;
  doc["name"] = name;
  File f = LittleFS.open("/box.json", "w");
  if (!f || serializeJson(doc, f) == 0) return "Saving failed";
  f.close();
  storedBoxName = name;
  boxNameLoaded = true;
  return "";
#endif
}

static void handleBoxName(AsyncWebServerRequest* req) {
  String name = arg(req, "name");
  String err = saveBoxName(name);
  if (!err.isEmpty()) return sendMessage(req, 400, "Not saved", err);
  String net = networkNameFor(name);
  if (net == ssid) return req->redirect("/connectivity");
  // new hostname / AP SSID: restart so that WiFi, mDNS and OTA pick it up
  restartRequested = true;
  AsyncResponseStream* r = pageBegin(req, "Restarting");
  r->printf("<h1>Restarting</h1><div class='panel'>The box restarts with the network name "
            "<span class='mono'>%s</span>. It will be reachable as <a href='http://%s.local/'>%s.local</a> "
            "(or at its IP address).</div>",
            esc(net).c_str(), esc(net).c_str(), esc(net).c_str());
  pageEnd(req, r);
}

// ---------------- status ----------------

static String hhmm(uint8_t h, uint8_t m) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02u:%02u", h, m);
  return buf;
}

static String scheduleText() {
  if (!timeControlEnabled) return "off (DMX always on)";
  struct tm t;
  if (!timeInitialized || !getLocalTime(&t, 0)) return "on, waiting for time sync";
  int today = (t.tm_wday + 6) % 7;
  if (isDMXActiveTime()) return "active until " + hhmm(dmxWeekdayEndHour[today], dmxWeekdayEndMinute[today]);
  int now = t.tm_hour * 60 + t.tm_min;
  int start = dmxWeekdayStartHour[today] * 60 + dmxWeekdayStartMinute[today];
  if (now < start) return "inactive, on at " + hhmm(dmxWeekdayStartHour[today], dmxWeekdayStartMinute[today]);
  int tomorrow = (today + 1) % 7;
  return "inactive, on " + String(DAY_NAMES[tomorrow]).substring(0, 3) + " " +
         hhmm(dmxWeekdayStartHour[tomorrow], dmxWeekdayStartMinute[tomorrow]);
}

static void handleStatusApi(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonObject w = doc["wifi"].to<JsonObject>();
  bool sta = !apModeActive && WiFi.status() == WL_CONNECTED;
  w["mode"] = sta ? "STA" : "AP";
  w["ssid"] = sta ? WiFi.SSID() : String(ssid);
  w["ip"] = (sta ? WiFi.localIP() : WiFi.softAPIP()).toString();
  w["rssi"] = sta ? WiFi.RSSI() : 0;
  w["clients"] = sta ? 0 : WiFi.softAPgetStationNum();
  struct tm t;
  if (timeInitialized && getLocalTime(&t, 0)) {
    char buf[6];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    doc["time"] = buf;
  }
  doc["sched"] = scheduleText();
#ifdef HAS_LORA
  LinkSettings ls = syncSettings();
  JsonObject l = doc["lora"].to<JsonObject>();
  l["enabled"] = ls.enabled;
  l["boxes"] = syncOnlinePeers() + 1;
  SnapshotStatus ss = syncSnapshotStatus();
  if (ss.active) {
    JsonObject s = doc["snap"].to<JsonObject>();
    s["reset"] = ss.reset;
    s["total"] = ss.total;
    s["ok"] = ss.ok;
    s["pending"] = ss.pending;
    JsonArray f = s["failed"].to<JsonArray>();
    for (const String& n : ss.failed) f.add(n);
  }
  JsonObject boxesJson = doc["boxes"].to<JsonObject>();
  for (const RemoteBox& b : syncRemoteBoxes()) boxesJson[String(b.peer.id)] = b.peer.online();
  JsonArray errors = doc["errors"].to<JsonArray>();
  for (const RemoteBox& b : syncRemoteBoxes()) {
    if (b.lastError.isEmpty() || millis() - b.lastErrorMs > 60000) continue;
    JsonObject e = errors.add<JsonObject>();
    e["box"] = b.peer.name;
    e["msg"] = b.lastError;
  }
#endif
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// ---------------- connectivity ----------------

static void handleConnectivity(AsyncWebServerRequest* req) {
  AsyncResponseStream* r = pageBegin(req, "Connectivity", "<meta http-equiv='refresh' content='15'>");
  r->print("<h1>Connectivity</h1><h2 id='box'>Box</h2>");
  r->printf("<form class='row' action='/box/name' method='post'><input name='name' maxlength='32' value='%s' "
            "aria-label='Box name' style='flex:1'><button class='btn primary' type='submit'>Rename</button></form>",
            esc(localBoxName()).c_str());
  r->printf("<p class='help'>Shown in the header, on the fixture tiles and on the other boxes. The network name "
            "follows it (now <span class='mono'>%s</span>: hostname <span class='mono'>%s.local</span> and access point); "
            "changing it restarts the box.</p>", esc(String(ssid)).c_str(), esc(String(ssid)).c_str());
  r->print("<h2>WiFi</h2><div class='panel'><table>");
  bool sta = !apModeActive && WiFi.status() == WL_CONNECTED;
  r->printf("<tr><th>Mode</th><td>%s</td></tr>", sta ? "Station (connected to a network)" : "Access point");
  if (sta) {
    r->printf("<tr><th>Network</th><td>%s</td></tr>", esc(WiFi.SSID()).c_str());
    r->printf("<tr><th>IP address</th><td class='mono'>%s</td></tr>", WiFi.localIP().toString().c_str());
    r->printf("<tr><th>Signal</th><td class='mono'>%d dBm</td></tr>", WiFi.RSSI());
  } else {
    r->printf("<tr><th>Network</th><td>%s</td></tr>", esc(String(ssid)).c_str());
    r->printf("<tr><th>IP address</th><td class='mono'>%s</td></tr>", WiFi.softAPIP().toString().c_str());
    r->printf("<tr><th>Clients</th><td class='mono'>%d</td></tr>", WiFi.softAPgetStationNum());
    if (strlen(savedSSID)) r->printf("<tr><th>Saved network</th><td>%s (not reachable)</td></tr>", esc(String(savedSSID)).c_str());
  }
  r->printf("<tr><th>Hostname</th><td class='mono'>%s.local</td></tr>", esc(String(ssid)).c_str());
  r->printf("<tr><th>Time</th><td class='mono'>%s</td></tr></table></div>",
            timeInitialized ? getFormattedTime().c_str() : "not synchronised");
  r->print("<div class='row'><a class='btn' href='/wifisetup'>Scan networks</a></div>");
  r->print("<form class='f panel' action='/wificonnect' method='post'><label>Network name (SSID)</label>"
           "<input name='ssid' required><label>Password</label><input name='password' type='password'>"
           "<div class='row'><button class='btn primary grow' type='submit'>Connect and restart</button></div></form>");
#ifdef HAS_LORA
  renderLoraSection(r);
#endif
  pageEnd(req, r);
}

static void handleWifiSetup(AsyncWebServerRequest* req) {
  sendProgress(req, "WiFi scan", "Scanning for networks, this takes a few seconds…", "/wifiscan", 1);
}

static void handleWifiScan(AsyncWebServerRequest* req) {
  int n = WiFi.scanNetworks(false, false, false, 300);
  AsyncResponseStream* r = pageBegin(req, "WiFi networks");
  r->print("<p class='meta'><a href='/connectivity'>Connectivity</a></p><h1>WiFi networks</h1>");
  if (n <= 0) r->print("<p class='muted'>No networks found.</p>");
  r->print("<div class='panel'><table><tr><th>Network</th><th>Signal</th><th></th></tr>");
  for (int i = 0; i < n && i < 20; i++) {
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    String name = WiFi.SSID(i);
    r->printf("<tr><td><a href='/wifiselect?ssid=%s&open=%d'>%s</a></td><td class='mono'>%d dBm</td><td class='meta'>%s</td></tr>",
              esc(name).c_str(), open ? 1 : 0, esc(name).c_str(), WiFi.RSSI(i), open ? "open" : "");
  }
  r->print("</table></div><div class='row'><a class='btn' href='/wifisetup'>Scan again</a></div>");
  WiFi.scanDelete();
  pageEnd(req, r);
}

static void handleWifiSelect(AsyncWebServerRequest* req) {
  String name = arg(req, "ssid");
  if (name.isEmpty()) return req->redirect("/connectivity");
  bool open = arg(req, "open") == "1";
  AsyncResponseStream* r = pageBegin(req, "Connect");
  r->printf("<p class='meta'><a href='/wifisetup'>WiFi networks</a></p><h1>%s</h1>", esc(name).c_str());
  r->printf("<form class='f' action='/wificonnect' method='post'><input type='hidden' name='ssid' value='%s'>", esc(name).c_str());
  if (open) r->print("<p class='meta'>Open network, no password needed.</p><input type='hidden' name='password' value=''>");
  else r->print("<label>Password</label><input type='password' name='password' required autofocus>");
  r->print("<div class='row'><button class='btn primary grow' type='submit'>Connect and restart</button></div></form>");
  pageEnd(req, r);
}

static void handleWifiConnect(AsyncWebServerRequest* req) {
  String name = arg(req, "ssid");
  if (name.isEmpty()) return req->redirect("/connectivity");
  saveWiFiConfig(name.c_str(), arg(req, "password").c_str());
  restartRequested = true;
  AsyncResponseStream* r = pageBegin(req, "Restarting");
  r->printf("<h1>Restarting</h1><div class='panel'>Connecting to <b>%s</b>. If that fails, the box opens its own "
            "access point <span class='mono'>%s</span> again.</div>",
            esc(name).c_str(), esc(String(ssid)).c_str());
  pageEnd(req, r);
}

// ---------------- schedule ----------------

static void handleTimeSetup(AsyncWebServerRequest* req) {
  AsyncResponseStream* r = pageBegin(req, "Schedule");
  r->printf("<h1>Schedule</h1><p class='meta'>%s</p>", esc(scheduleText()).c_str());
  r->print("<form class='f' action='/timesave' method='post'>");
  r->printf("<label><input type='checkbox' name='enabled' value='1'%s> Switch DMX by schedule</label>",
            timeControlEnabled ? " checked" : "");
  r->print("<p class='help'>Outside the times below all DMX channels are sent as 0. An end time before the start time "
           "runs over midnight.</p><div class='panel'><table><tr><th>Day</th><th>On</th><th>Off</th></tr>");
  for (int d = 0; d < 7; d++) {
    r->printf("<tr><td>%s</td><td><input type='time' name='s%d' value='%s'></td><td><input type='time' name='e%d' value='%s'></td></tr>",
              DAY_NAMES[d], d, hhmm(dmxWeekdayStartHour[d], dmxWeekdayStartMinute[d]).c_str(), d,
              hhmm(dmxWeekdayEndHour[d], dmxWeekdayEndMinute[d]).c_str());
  }
  r->print("</table></div><div class='row'><button class='btn' type='button' onclick='copyAll()'>Copy Monday to all days</button>"
           "<button class='btn primary grow' type='submit'>Save</button></div></form>"
           "<script>function copyAll(){const s=document.querySelector('[name=s0]').value,e=document.querySelector('[name=e0]').value;"
           "for(let i=1;i<7;i++){document.querySelector('[name=s'+i+']').value=s;document.querySelector('[name=e'+i+']').value=e}}</script>");
  pageEnd(req, r);
}

static bool parseHHMM(const String& v, uint8_t& h, uint8_t& m) {
  int colon = v.indexOf(':');
  if (colon < 1) return false;
  int hh = v.substring(0, colon).toInt(), mm = v.substring(colon + 1).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  h = hh;
  m = mm;
  return true;
}

static void handleTimeSave(AsyncWebServerRequest* req) {
  for (int d = 0; d < 7; d++) {
    String s = "s" + String(d), e = "e" + String(d);
    if (hasArg(req, s.c_str())) parseHHMM(arg(req, s.c_str()), dmxWeekdayStartHour[d], dmxWeekdayStartMinute[d]);
    if (hasArg(req, e.c_str())) parseHHMM(arg(req, e.c_str()), dmxWeekdayEndHour[d], dmxWeekdayEndMinute[d]);
  }
  timeControlEnabled = arg(req, "enabled") == "1";
  saveTimeControl();
  req->redirect("/");
}

// ---------------- snapshot (save / reset) ----------------

static void handleSnapshotSave(AsyncWebServerRequest* req) {
#ifdef HAS_LORA
  syncSnapshotAll(false);
#else
  snapshotSave();
#endif
  req->redirect("/");
}

static void handleSnapshotReset(AsyncWebServerRequest* req) {
  if (req->method() == HTTP_POST) {
#ifdef HAS_LORA
    syncSnapshotAll(true);
#else
    snapshotReset();
#endif
    return req->redirect("/");
  }
  AsyncResponseStream* r = pageBegin(req, "Reset");
  r->print("<h1>Reset?</h1><div class='panel'>Sets all DMX channels to 0 and clears the active presets");
#ifdef HAS_LORA
  r->print(" on this box and all linked boxes");
#endif
  r->print(". Fixtures, definitions and presets themselves are kept.</div>"
           "<div class='row'><form method='post' action='/snapshot/reset'><button class='btn danger grow' type='submit'>Reset</button></form>"
           "<a class='btn grow' href='/'>Cancel</a></div>");
  pageEnd(req, r);
}

void registerSystemRoutes(AsyncWebServer& server) {
  server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "ok"); });
  server.on("/api/status", HTTP_GET, handleStatusApi);
  server.on("/connectivity", HTTP_GET, handleConnectivity);
  server.on("/box/name", HTTP_ANY, handleBoxName);
  server.on("/wifisetup", HTTP_GET, handleWifiSetup);
  server.on("/wifiscan", HTTP_GET, handleWifiScan);
  server.on("/wifiselect", HTTP_GET, handleWifiSelect);
  server.on("/wificonnect", HTTP_ANY, handleWifiConnect);
  server.on("/timesetup", HTTP_GET, handleTimeSetup);
  server.on("/timesave", HTTP_ANY, handleTimeSave);
  server.on("/snapshot/save", HTTP_POST, handleSnapshotSave);
  server.on("/snapshot/reset", HTTP_ANY, handleSnapshotReset);
}
