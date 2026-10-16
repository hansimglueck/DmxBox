#include "firmware.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-unknown"
#endif
#ifndef FW_ENV
#define FW_ENV "unknown"
#endif

const char* firmwareVersion() { return FW_VERSION; }
const char* firmwareEnv() { return FW_ENV; }

uint32_t versionCode(const String& v) {
  unsigned major, minor, patch;
  const char* s = v.c_str();
  if (*s == 'v') s++;
  if (sscanf(s, "%u.%u.%u", &major, &minor, &patch) != 3 || major > 255 || minor > 255 || patch > 255) return 0;
  return major << 16 | minor << 8 | patch;
}

String versionString(uint32_t code) {
  return String(code >> 16) + "." + String((code >> 8) & 0xFF) + "." + String(code & 0xFF);
}

uint32_t firmwareVersionCode() {
  static uint32_t code = versionCode(FW_VERSION);
  return code;
}

#ifdef HAS_FW_UPDATE
#include "fw_certs.h"
#include "system.h"
#include "display.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

static const char* MANIFEST_URL = "https://github.com/hansimglueck/DmxBox/releases/latest/download/latest.json";
static const uint32_t FIRST_CHECK_MS = 30000;                // after boot, once the network has settled
static const uint32_t CHECK_INTERVAL_MS = 6UL * 3600 * 1000;
static const uint32_t RETRY_INTERVAL_MS = 30UL * 60 * 1000;  // after a failed check
static const uint32_t STALL_TIMEOUT_MS = 20000;              // download: no data for this long
static const uint32_t TASK_STACK = 12288;                    // TLS handshake needs a good part of it
static const size_t DOWNLOAD_BUF = 2048;

enum Job : uint8_t { JOB_CHECK = 1, JOB_INSTALL = 2 };

struct Release {
  String version, url, md5;
  int size = 0;
};

static SemaphoreHandle_t mtx = nullptr;
static FirmwareStatus st;
static bool busy = false;
static bool internet = false;      // the last check reached the release server
static uint32_t lastAttemptMs = 0;  // 0 = no check yet
static bool lastAttemptOk = false;
static volatile bool checkRequested = false;

namespace {
struct Lock {
  Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mtx); }
};
}  // namespace

static bool staConnected() { return !apModeActive && WiFi.status() == WL_CONNECTED; }

static void beginRequest(WiFiClientSecure& client, HTTPClient& http) {
  client.setCACert(FW_ROOT_CAS);
  // GitHub answers release downloads with a redirect to its asset host
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
}

static String httpError(HTTPClient& http, int code) {
  return code < 0 ? "Connection failed (" + http.errorToString(code) + ")" : "HTTP " + String(code);
}

// reached: the server answered (there is internet, even if there is no usable release)
static bool fetchManifest(Release& rel, String& err, bool& reached) {
  WiFiClientSecure client;
  HTTPClient http;
  beginRequest(client, http);
  reached = false;
  if (!http.begin(client, MANIFEST_URL)) {
    err = "Invalid manifest URL";
    return false;
  }
  int code = http.GET();
  if (code > 0) reached = true;
  if (code == 404) {
    http.end();
    err = "No release published yet";
    return false;
  }
  if (code != 200) {
    err = httpError(http, code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    err = "Release manifest unreadable";
    return false;
  }
  rel.version = doc["version"] | "";
  if (rel.version.startsWith("v")) rel.version.remove(0, 1);
  JsonObjectConst build = doc["builds"][FW_ENV];
  if (build.isNull()) {
    err = "Release " + rel.version + " has no build for " + FW_ENV;
    return false;
  }
  rel.url = build["url"] | "";
  rel.md5 = build["md5"] | "";
  rel.size = build["size"] | 0;
  if (rel.version.isEmpty() || rel.url.isEmpty()) {
    err = "Release manifest incomplete";
    return false;
  }
  return true;
}

static bool download(const Release& rel, String& err) {
  WiFiClientSecure client;
  HTTPClient http;
  beginRequest(client, http);
  if (!http.begin(client, rel.url)) {
    err = "Invalid download URL";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = "Download: " + httpError(http, code);
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len <= 0) len = rel.size;
  if (len <= 0 || (rel.size > 0 && len != rel.size)) {
    err = "Download has an unexpected size";
    http.end();
    return false;
  }
  if (!Update.begin(len, U_FLASH)) {
    err = String("Cannot start update: ") + Update.errorString();
    http.end();
    return false;
  }
  if (rel.md5.length() == 32) Update.setMD5(rel.md5.c_str());

  uint8_t* buf = (uint8_t*)malloc(DOWNLOAD_BUF);
  WiFiClient* stream = http.getStreamPtr();
  int done = 0;
  uint32_t lastDataMs = millis();
  while (buf && done < len) {
    size_t avail = stream->available();
    if (!avail) {
      if (!stream->connected()) break;
      if (millis() - lastDataMs > STALL_TIMEOUT_MS) {
        err = "Download stalled";
        break;
      }
      delay(5);
      continue;
    }
    int n = stream->readBytes(buf, min(avail, DOWNLOAD_BUF));
    if (n <= 0) continue;
    if (Update.write(buf, n) != (size_t)n) {
      err = String("Flash write failed: ") + Update.errorString();
      break;
    }
    done += n;
    lastDataMs = millis();
    Lock l;
    st.progress = (int)((int64_t)done * 100 / len);
  }
  free(buf);
  http.end();
  if (err.isEmpty() && !buf) err = "Out of memory";
  if (err.isEmpty() && done != len) err = "Download incomplete";
  if (!err.isEmpty()) {
    Update.abort();
    return false;
  }
  // checks the MD5 and only then switches the boot partition: a failed update keeps the old firmware
  if (!Update.end()) {
    err = String("Update rejected: ") + Update.errorString();
    return false;
  }
  return true;
}

static void fwTask(void* arg) {
  Job job = (Job)(uintptr_t)arg;
  Release rel;
  String err;
  bool reached;
  bool ok = fetchManifest(rel, err, reached);
  bool newer = ok && versionCode(rel.version) > firmwareVersionCode();
  {
    Lock l;
    internet = reached;
    lastAttemptOk = ok;
    if (!rel.version.isEmpty()) {
      st.latest = rel.version;
      st.latestCode = versionCode(rel.version);
    }
    if (ok) st.lastCheckMs = millis();
    st.error = err;
    st.state = !ok ? FW_ERROR : newer ? FW_AVAILABLE : FW_UPTODATE;
    if (job == JOB_INSTALL && ok && !newer) st.error = "Already up to date";
    if (job == JOB_INSTALL && newer) {
      st.state = FW_DOWNLOADING;
      st.progress = 0;
    }
  }
  Serial.printf("Firmware: %s %s, latest %s%s%s\n", FW_VERSION, FW_ENV, rel.version.isEmpty() ? "?" : rel.version.c_str(),
                err.isEmpty() ? "" : " - ", err.c_str());

  if (job == JOB_INSTALL && newer) {
    Serial.printf("Firmware: installing %s from %s\n", rel.version.c_str(), rel.url.c_str());
    bool installed = download(rel, err);
    {
      Lock l;
      st.state = installed ? FW_DONE : FW_ERROR;
      st.error = err;
    }
    Serial.printf("Firmware: %s\n", installed ? "installed, restarting" : err.c_str());
    if (installed) restartRequested = true;
  }
  {
    Lock l;
    busy = false;
  }
  vTaskDelete(nullptr);
}

static bool startJob(Job job) {
  {
    Lock l;
    if (busy) return false;
    busy = true;
    lastAttemptMs = millis() | 1;
    if (st.state != FW_AVAILABLE) st.state = FW_CHECKING;
  }
  if (xTaskCreate(fwTask, "firmware", TASK_STACK, (void*)(uintptr_t)job, 1, nullptr) != pdPASS) {
    Lock l;
    busy = false;
    st.state = FW_ERROR;
    st.error = "Out of memory";
    return false;
  }
  return true;
}

void firmwareBegin() {
  mtx = xSemaphoreCreateMutex();
  Serial.printf("Firmware %s (%s)\n", FW_VERSION, FW_ENV);
}

void firmwareLoop() {
  bool sta = staConnected();
  uint32_t now = millis();
  bool idle;
  bool due;
  {
    Lock l;
    if (!sta) internet = false;
    idle = !busy;
    due = lastAttemptMs == 0 ? now > FIRST_CHECK_MS
                             : now - lastAttemptMs > (lastAttemptOk ? CHECK_INTERVAL_MS : RETRY_INTERVAL_MS);
  }
  if (sta && idle && (due || checkRequested)) {
    checkRequested = false;
    startJob(JOB_CHECK);
  }

  // the regular status screen is paused while downloading (see loop())
  static int shown = -1;
  FirmwareStatus s = firmwareStatus();
  if (s.state == FW_DOWNLOADING && s.progress != shown) {
    shown = s.progress;
    displayMessage(("Firmware " + s.latest).c_str(), ("Downloading " + String(s.progress) + " %").c_str());
  } else if (s.state == FW_DONE && shown != 101) {
    shown = 101;
    displayMessage("Firmware updated", "Restarting...");
  }
}

FirmwareStatus firmwareStatus() {
  Lock l;
  return st;
}

void firmwareCheckNow() { checkRequested = true; }

String firmwareInstall() {
  if (!staConnected()) return "No internet (not connected to a WiFi network)";
  if (!startJob(JOB_INSTALL)) return "An update check or download is already running";
  return "";
}

bool firmwareBusy() {
  Lock l;
  return busy;
}

bool firmwareHasInternet() {
  Lock l;
  return internet && staConnected();
}

bool firmwareUpdateAvailable() {
  Lock l;
  return st.latestCode > firmwareVersionCode();
}

uint8_t firmwareFlags() {
  Lock l;
  uint8_t f = 0;
  if (internet && staConnected()) f |= FW_FLAG_INTERNET;
  if (st.latestCode > firmwareVersionCode()) f |= FW_FLAG_AVAILABLE;
  if (st.state == FW_DOWNLOADING || st.state == FW_DONE) f |= FW_FLAG_UPDATING;
  return f;
}

#else  // without HAS_FW_UPDATE (the TLS client does not fit the 1.3 MB app partition): espota only

void firmwareBegin() { Serial.printf("Firmware %s (%s)\n", FW_VERSION, FW_ENV); }
void firmwareLoop() {}
FirmwareStatus firmwareStatus() { return FirmwareStatus(); }
void firmwareCheckNow() {}
String firmwareInstall() { return "This box cannot update itself online"; }
bool firmwareBusy() { return false; }
bool firmwareHasInternet() { return false; }
bool firmwareUpdateAvailable() { return false; }
uint8_t firmwareFlags() { return 0; }

#endif
