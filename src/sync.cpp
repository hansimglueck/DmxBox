#ifdef HAS_LORA
#include "sync.h"
#include "lora_link.h"
#include "fixtures.h"
#include "presets.h"
#include "cues.h"
#include "system.h"
#include "firmware.h"
#include <esp_rom_crc.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <algorithm>

extern uint8_t data[];

static const char* LINK_FILE = "/link.json";
static const uint8_t PROTO_VER = 6;
static const uint32_t BEACON_INTERVAL_MS = 10000;
static const uint32_t VALUES_CHECK_MS = 200;
static const uint32_t VALUES_REFRESH_MS = 60000;
static const uint32_t SET_RETRY_MS = 1500;
static const uint32_t SET_MIN_INTERVAL_MS = 250;  // per box; faster changes are coalesced
static const uint32_t RX_HOLDOFF_MIN_MS = 40;     // after receiving, give the sender time to switch back to RX
static const uint32_t RX_HOLDOFF_MAX_MS = 90;
static const uint8_t SET_MAX_ATTEMPTS = 3;
static const uint32_t DESC_RETRY_MS = 3000;
static const uint8_t DESC_MAX_ATTEMPTS = 5;
static const size_t TX_QUEUE_MAX = 24;
static const size_t CHUNK = 200;
static const size_t MAX_CHUNKS = 32;
static const size_t CHANNEL_NAME_MAX = 24;

enum : uint8_t {
  PT_BEACON = 1,
  PT_PING = 2,
  PT_PONG = 3,
  PT_FIX_REQ = 4,
  PT_FIX_DESC = 5,
  PT_VAL_REQ = 6,
  PT_VALUES = 7,
  PT_SET = 8,
  PT_REFS = 9,
  PT_CMD = 10,
  PT_ACK = 11,
  PT_DEF_PUSH = 12,
  PT_DEF_ACK = 13,
  PT_SHOW = 14,      // show metadata chunk (broadcast)
  PT_SHOW_REQ = 15,  // request show metadata chunks from one box
  PT_MARKS = 16,     // channels changed by hand on the sender
  PT_DEF_REQ = 17,   // request one definition (by uid) from one box
};
static const uint32_t DEF_PULL_TIMEOUT_MS = 20000;
static const uint32_t DEF_RECONCILE_MS = 30000;
// command ops beyond the preset ops (PresetOp, 1..5)
// cue ops reuse the command fields: fixtureId = sequence, presetId = cue, group = insert / mode,
// address = move
enum : uint8_t { CMD_FIXTURE_SAVE = 10, CMD_FIXTURE_DELETE = 11, CMD_CUE_STORE = 20, CMD_TRANSPORT = 21, CMD_CLEAR = 22, CMD_FW_UPDATE = 30 };
static const uint32_t DEF_PUSH_RETRY_MS = 5000;
static const uint8_t DEF_PUSH_MAX_ATTEMPTS = 3;
static const uint32_t STORE_SHOW_MS = 60000;
static const uint32_t MARKS_MIN_INTERVAL_MS = 300;
static const uint32_t BEACON_AIRTIME_MS = 40;  // play state age when a beacon arrives
static const size_t PRESETS_PER_FIXTURE_MAX = 60;
static const uint32_t CMD_RETRY_MS = 1500;
static const uint8_t CMD_MAX_ATTEMPTS = 5;

struct __attribute__((packed)) PktHdr {
  uint8_t m0, m1, ver, net;
  uint16_t src;
  uint16_t seq;
  uint8_t type;
};
static const size_t HDR_LEN = sizeof(PktHdr);
static const size_t MAC_LEN = 4;
static const size_t MAX_PAYLOAD = LINK_MAX_PACKET - HDR_LEN - MAC_LEN;

// Receiving side of a fixture-list transfer
struct DescAssembly {
  uint16_t version = 0;
  uint16_t total = 0;
  uint32_t mask = 0;  // chunks received
  std::vector<uint8_t> buf;
  uint32_t lastActivityMs = 0;
  uint8_t attempts = 0;
};

struct BoxState {
  RemoteBox box;
  uint16_t errorNonce = 0;  // command that reported box.lastError
  uint16_t describedVersion = 0;
  DescAssembly asmb;
  uint32_t lastSetPacketMs = 0;
};

struct PendingSet {
  uint16_t box, addr;
  uint8_t value;
  uint8_t attempts;
  uint32_t lastSendMs;
};
static const uint8_t SET_FAILED = 0xFF;

struct PendingCmd {
  uint16_t box = 0, nonce = 0;
  uint8_t op = 0;
  uint16_t fixtureId = 0;
  uint8_t group = 0;
  uint16_t presetId = 0;
  String name;
  uint16_t address = 0, defId = 0, count = 0;  // fixture ops
  uint8_t attempts = 0;
  uint32_t lastSendMs = 0;
};

// Sending a definition to another box, followed by saving a fixture that uses it.
struct DefPush {
  uint16_t box, xfer;
  uint32_t uid = 0;
  std::vector<uint8_t> json;
  uint32_t sendMask = 0;
  uint8_t attempts = 0;
  uint32_t lastSendMs = 0;
  PendingCmd then;  // CMD_FIXTURE_SAVE (defId filled in from the ACK), op 0 = only the definition
};

// Definition that another box has in a newer revision (or that we do not have yet).
struct DefPull {
  uint16_t box;
  uint32_t uid;
};

// Receiving side of a definition transfer.
struct DefAssembly {
  uint16_t src, xfer, total;
  uint32_t mask = 0;
  std::vector<uint8_t> buf;
  bool done = false;
  uint16_t defId = 0;
  String err;
  uint32_t lastMs = 0;
};

struct StoreRun {
  uint32_t startedMs = 0;
  uint16_t localCount = 0;
  struct Entry {
    uint16_t box;
    int8_t state;  // 0 pending / 1 ok / -1 failed
    uint16_t count;
  };
  std::vector<Entry> boxes;
};

// Receiving side of a show metadata transfer.
struct ShowAssembly {
  uint16_t src = 0, ver = 0, total = 0;
  uint32_t mask = 0;
  std::vector<uint8_t> buf;
  uint32_t lastMs = 0;
};

static Link* transport = &loraLink;
static LinkSettings settings;
static uint16_t boxId = 0;
static uint8_t netId = 0;
static uint16_t seqNo = 0;
static std::vector<BoxState> boxes;
static std::vector<PendingSet> pendingSets;
static std::vector<PendingCmd> pendingCmds;
static uint16_t nextNonce = 1;
// recently executed commands (sender, nonce) so that retries are not executed twice
static std::vector<std::pair<uint16_t, uint16_t>> doneCmds;
static uint32_t lastRefsVersion = 0xFFFFFFFF;
static std::vector<DefPush> defPushes;
static std::vector<DefAssembly> defAssemblies;
static std::vector<DefPull> defPulls;
static DefPull activePull = {0, 0};
static uint32_t activePullMs = 0;
static StoreRun storeRun;
static ShowAssembly showAsm;
static std::vector<uint8_t> showBlob;     // our show metadata as sent
static uint16_t showBlobVer = 0;
static uint32_t showSendMask = 0;
static uint16_t lastShowVer = 0xFFFF;      // show version last seen in syncLoop
static bool showVerApplied = false;        // the change came from another box: no re-broadcast
static uint32_t lastMarksVer = 0xFFFFFFFF;
static uint32_t lastMarksSentMs = 0;
static bool marksSendPending = false;
// newest transport per sender, so that a late retry cannot undo a newer state. Retries end after
// CMD_MAX_ATTEMPTS * CMD_RETRY_MS; older entries are ignored (the sender may have restarted).
struct LastTransport {
  uint16_t src, nonce;
  uint32_t ms;
};
static std::vector<LastTransport> lastTransport;
static const uint32_t TRANSPORT_ORDER_MS = 12000;
// replies (stored channel count) of executed cue stores, for retried commands
static std::vector<std::pair<std::pair<uint16_t, uint16_t>, String>> storedReplies;
static bool refsSendPending = false;
static std::vector<std::vector<uint8_t>> txQueue;
static SemaphoreHandle_t mtx = nullptr;
static uint32_t nextBeaconMs = 0;
static uint32_t txHoldUntilMs = 0;
static volatile bool pingRequested = false;
static volatile bool beaconRequested = false;

// Owner side: our own fixtures as seen by the others
static uint16_t ownVersion = 0;
static bool ownBuilt = false;
static std::vector<uint8_t> descBlob;
static uint32_t descSendMask = 0;
static bool used[513] = {};
static uint8_t lastSent[513] = {};
static bool fullRefreshPending = false;
// changed values are sent once more after changes stopped for a while (one lost packet would
// otherwise leave the others with an old value until the next full refresh)
static const uint32_t VALUES_REPEAT_MS = 1000;
static bool repeatAddr[513] = {};
static uint32_t valuesRepeatAt = 0;
static bool valuesCheckNow = false;
static uint32_t lastValuesCheckMs = 0;
static uint32_t nextFullRefreshMs = 0;

struct Lock {
  Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mtx); }
};

// ---------- crypto ----------

static void packetMac(const uint8_t* buf, size_t len, uint8_t out[MAC_LEN]) {
  uint8_t full[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  (const uint8_t*)settings.password.c_str(), settings.password.length(), buf, len, full);
  memcpy(out, full, MAC_LEN);
}

static void applySettings() {
  uint8_t digest[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
             (const uint8_t*)settings.password.c_str(), settings.password.length(), digest);
  netId = digest[0];
  loraLink.setTxPower(settings.txPower);
}

// ---------- settings ----------

static void loadSettings(const char* defaultName) {
  settings = LinkSettings();
  settings.name = defaultName;
  settings.password = "dmxbox";
  File f = LittleFS.open(LINK_FILE, "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
      settings.name = migrateBoxName(doc["name"] | defaultName);
      settings.password = doc["password"] | "dmxbox";
      settings.enabled = doc["enabled"] | true;
      settings.txPower = doc["txPower"] | 14;
    }
    f.close();
  }
}

String syncSaveSettings(const LinkSettings& s) {
  LinkSettings n = s;
  n.name.trim();
  if (n.name.isEmpty() || n.name.length() > 32) return "Name must be 1–32 characters";
  if (n.password.length() < 4) return "Password needs at least 4 characters";
  if (n.txPower < 0 || n.txPower > 27) return "Transmit power must be between 0 and 27 dBm";
  JsonDocument doc;
  doc["name"] = n.name;
  doc["password"] = n.password;
  doc["enabled"] = n.enabled;
  doc["txPower"] = n.txPower;
  File f = LittleFS.open(LINK_FILE, "w");
  if (!f || serializeJson(doc, f) == 0) return "Saving failed";
  f.close();
  {
    Lock l;
    bool passwordChanged = n.password != settings.password;
    settings = n;
    applySettings();
    if (passwordChanged) {
      boxes.clear();
      pendingSets.clear();
    }
  }
  beaconRequested = true;
  return "";
}

LinkSettings syncSettings() {
  Lock l;
  return settings;
}

uint16_t syncBoxId() { return boxId; }

// ---------- views for the web UI ----------

static RemoteBox viewOf(const BoxState& b) {
  RemoteBox v = b.box;
  v.pending.clear();
  for (const PendingSet& p : pendingSets) {
    if (p.box == b.box.peer.id) v.pending.push_back({p.addr, p.value});
  }
  return v;
}

std::vector<PeerInfo> syncPeers() {
  Lock l;
  std::vector<PeerInfo> out;
  for (const BoxState& b : boxes) out.push_back(b.box.peer);
  return out;
}

std::vector<RemoteBox> syncRemoteBoxes() {
  Lock l;
  std::vector<RemoteBox> out;
  for (const BoxState& b : boxes) out.push_back(viewOf(b));
  return out;
}

bool syncRemoteBox(uint16_t id, RemoteBox& out) {
  Lock l;
  for (const BoxState& b : boxes) {
    if (b.box.peer.id == id) {
      out = viewOf(b);
      return true;
    }
  }
  return false;
}

int syncOnlinePeers() {
  Lock l;
  int n = 0;
  for (const BoxState& b : boxes) if (b.box.peer.online()) n++;
  return n;
}

void syncSetRemote(uint16_t box, uint16_t addr, uint8_t value) {
  if (addr < 1 || addr > 512) return;
  Lock l;
  for (BoxState& b : boxes) {
    if (b.box.peer.id != box) continue;
    b.box.failed.erase(std::remove(b.box.failed.begin(), b.box.failed.end(), addr), b.box.failed.end());
  }
  for (PendingSet& p : pendingSets) {
    if (p.box == box && p.addr == addr) {
      p.value = value;
      p.attempts = 0;
      return;
    }
  }
  pendingSets.push_back({box, addr, value, 0, 0});
}

void syncPing() { pingRequested = true; }

void syncClearRemote(uint16_t box, uint16_t dmxFrom, uint16_t dmxTo) {
  Lock l;
  PendingCmd c;
  c.box = box;
  c.nonce = nextNonce++;
  c.op = CMD_CLEAR;
  c.address = dmxFrom;
  c.count = dmxTo;
  pendingCmds.push_back(c);
}

void syncFirmwareUpdate(uint16_t box) {
  Lock l;
  PendingCmd c;
  c.box = box;
  c.nonce = nextNonce++;
  c.op = CMD_FW_UPDATE;
  pendingCmds.push_back(c);
}

void syncPresetCommand(uint16_t box, uint8_t op, uint16_t fixtureId, uint8_t group, uint16_t presetId,
                       const String& name) {
  Lock l;
  PendingCmd c;
  c.box = box;
  c.nonce = nextNonce++;
  c.op = op;
  c.fixtureId = fixtureId;
  c.group = group;
  c.presetId = presetId;
  c.name = name;
  pendingCmds.push_back(c);
}

// Definition as sent over the link: DmxBox JSON plus uid and revision.
static std::vector<uint8_t> defTransferJson(const FixtureDef& d) {
  JsonDocument doc;
  defToJson(d, doc.to<JsonObject>());
  doc["uid"] = d.uid;
  doc["rev"] = d.rev;
  String s;
  serializeJson(doc, s);
  return std::vector<uint8_t>(s.c_str(), s.c_str() + s.length());
}

String syncDefUsedBy(uint32_t uid) {
  Lock l;
  String out;
  for (const BoxState& b : boxes) {
    for (const RemoteDef& rd : b.box.defs) {
      if (rd.uid != uid) continue;
      for (const RemoteFixture& f : b.box.fixtures) {
        if (f.defId == rd.id) out += (out.isEmpty() ? "" : ", ") + f.name + " (" + b.box.peer.name + ")";
      }
    }
  }
  return out;
}

void syncRemoteFixtureSave(uint16_t box, uint16_t fixtureId, const String& name, uint16_t address,
                           uint16_t remoteDefId, int pushLocalDef, uint16_t count) {
  PendingCmd c;
  c.box = box;
  c.op = CMD_FIXTURE_SAVE;
  c.fixtureId = fixtureId;
  c.name = name;
  c.address = address;
  c.defId = remoteDefId;
  c.count = count;
  std::vector<uint8_t> json;
  if (pushLocalDef >= 0) {
    fixturesLock();
    FixtureDef* d = findDef(pushLocalDef);
    if (d) json = defTransferJson(*d);
    fixturesUnlock();
  }
  Lock l;
  c.nonce = nextNonce++;
  if (json.empty()) {
    pendingCmds.push_back(c);
    return;
  }
  DefPush dp;
  dp.box = box;
  dp.xfer = nextNonce++;
  dp.json = json;
  dp.then = c;
  defPushes.push_back(dp);
}

void syncRemoteFixtureDelete(uint16_t box, uint16_t fixtureId) {
  Lock l;
  PendingCmd c;
  c.box = box;
  c.nonce = nextNonce++;
  c.op = CMD_FIXTURE_DELETE;
  c.fixtureId = fixtureId;
  pendingCmds.push_back(c);
}

int syncPendingFor(uint16_t box) {
  Lock l;
  int n = 0;
  for (const PendingCmd& c : pendingCmds) if (!box || c.box == box) n++;
  for (const DefPush& d : defPushes) if (!box || d.box == box) n++;
  return n;
}

// Caller holds the lock.
static void queueCmdToAll(uint8_t op, uint16_t a, uint8_t b, uint16_t c, uint16_t d, uint16_t e,
                          std::vector<uint16_t>* sentTo = nullptr) {
  for (const BoxState& bs : boxes) {
    if (!bs.box.peer.online()) continue;
    PendingCmd cmd;
    cmd.box = bs.box.peer.id;
    cmd.nonce = nextNonce++;
    cmd.op = op;
    cmd.fixtureId = a;
    cmd.group = b;
    cmd.presetId = c;
    cmd.address = d;
    cmd.count = e;
    pendingCmds.push_back(cmd);
    if (sentTo) sentTo->push_back(cmd.box);
  }
}

void syncTransport(const CueTransport& t) {
  Lock l;
  queueCmdToAll(CMD_TRANSPORT, t.seq, t.mode, t.cue, t.move, t.epoch);
}

void syncCueStore(const CueStoreResult& r) {
  Lock l;
  storeRun = StoreRun();
  storeRun.startedMs = millis();
  storeRun.localCount = r.count;
  std::vector<uint16_t> sent;
  queueCmdToAll(CMD_CUE_STORE, r.seq, r.insert ? 1 : 0, r.cue, 0, r.epoch, &sent);
  for (uint16_t b : sent) storeRun.boxes.push_back({b, 0, 0});
}

CueStoreStatus syncCueStoreStatus() {
  Lock l;
  CueStoreStatus st;
  if (!storeRun.startedMs || millis() - storeRun.startedMs > STORE_SHOW_MS) return st;
  st.active = true;
  st.total = 1 + storeRun.boxes.size();
  st.ok = 1;
  st.counts.push_back({settings.name, storeRun.localCount});
  for (const auto& e : storeRun.boxes) {
    BoxState* b = nullptr;
    for (BoxState& x : boxes) if (x.box.peer.id == e.box) b = &x;
    String name = b ? b->box.peer.name : String(e.box, HEX);
    if (e.state > 0) {
      st.ok++;
      st.counts.push_back({name, e.count});
    } else if (e.state == 0) {
      st.pending++;
    } else {
      st.failed.push_back(name);
    }
  }
  return st;
}

// Caller holds the lock.
static void storeResult(uint16_t box, bool ok, uint16_t count) {
  for (auto& e : storeRun.boxes) {
    if (e.box == box && e.state == 0) {
      e.state = ok ? 1 : -1;
      e.count = count;
    }
  }
}

// ---------- packet helpers ----------

// Caller must hold the lock.
static void queuePacket(uint8_t type, const uint8_t* payload, size_t plen) {
  if (plen > MAX_PAYLOAD) return;
  std::vector<uint8_t> pkt(HDR_LEN + plen + MAC_LEN);
  PktHdr h = {'D', 'X', PROTO_VER, netId, boxId, ++seqNo, type};
  memcpy(pkt.data(), &h, HDR_LEN);
  if (plen) memcpy(pkt.data() + HDR_LEN, payload, plen);
  packetMac(pkt.data(), HDR_LEN + plen, pkt.data() + HDR_LEN + plen);
  if (txQueue.size() >= TX_QUEUE_MAX) txQueue.erase(txQueue.begin());
  txQueue.push_back(pkt);
}

static void queuePacket(uint8_t type, const std::vector<uint8_t>& payload) {
  queuePacket(type, payload.data(), payload.size());
}

template <typename T>
static void put(std::vector<uint8_t>& b, T v) {
  const uint8_t* p = (const uint8_t*)&v;
  b.insert(b.end(), p, p + sizeof(T));
}

static void putStr(std::vector<uint8_t>& b, const String& s, size_t maxLen) {
  size_t n = min((size_t)s.length(), maxLen);
  put<uint8_t>(b, n);
  b.insert(b.end(), s.c_str(), s.c_str() + n);
}

template <typename T>
static bool get(const uint8_t*& p, const uint8_t* end, T& v) {
  if (p + sizeof(T) > end) return false;
  memcpy(&v, p, sizeof(T));
  p += sizeof(T);
  return true;
}

static bool getStr(const uint8_t*& p, const uint8_t* end, String& s) {
  uint8_t n;
  if (!get(p, end, n) || p + n > end) return false;
  s = "";
  s.reserve(n);
  for (uint8_t i = 0; i < n; i++) s += (char)p[i];
  p += n;
  return true;
}

static size_t chunkCount(size_t total) { return total == 0 ? 1 : (total + CHUNK - 1) / CHUNK; }

static uint32_t chunkMask(size_t total) {
  size_t n = chunkCount(total);
  return n >= 32 ? 0xFFFFFFFF : ((1UL << n) - 1);
}

static BoxState& boxFor(uint16_t id) {
  for (BoxState& b : boxes) if (b.box.peer.id == id) return b;
  boxes.push_back(BoxState());
  boxes.back().box.peer.id = id;
  boxes.back().box.peer.name = String("Box ") + String(id, HEX);
  return boxes.back();
}

static BoxState* findBox(uint16_t id) {
  for (BoxState& b : boxes) if (b.box.peer.id == id) return &b;
  return nullptr;
}

// ---------- owner side ----------

static void rebuildOwn() {
  fixturesLock();
  ownVersion = fixturesVersion();
  memset(used, 0, sizeof(used));
  std::vector<uint8_t> blob;
  put<uint8_t>(blob, 0);  // fixture count, patched below
  uint8_t count = 0;
  for (const Fixture& f : fixtureList) {
    uint16_t n = fixtureChannelCount(f);
    std::vector<uint8_t> entry;
    put<uint16_t>(entry, f.id);
    put<uint16_t>(entry, f.defId);
    put<uint16_t>(entry, f.address);
    put<uint16_t>(entry, n);
    putStr(entry, f.name, FIXTURE_NAME_MAX);
    std::vector<int16_t> fine = fixtureFinePairs(f);
    std::vector<uint8_t> groups = fixtureGroups(f);
    for (uint16_t i = 0; i < n; i++) {
      putStr(entry, fixtureChannelName(f, i), CHANNEL_NAME_MAX);
      put<int16_t>(entry, i < fine.size() ? fine[i] : -1);
      put<uint8_t>(entry, i < groups.size() ? groups[i] : (uint8_t)GROUP_CONTROL);
    }
    std::vector<PresetInfo> plist;
    for (uint8_t g = 0; g < GROUP_COUNT; g++) {
      if (std::find(groups.begin(), groups.end(), g) == groups.end()) continue;
      for (const PresetInfo& p : presetsFor(f, g)) {
        if (plist.size() < PRESETS_PER_FIXTURE_MAX) plist.push_back(p);
      }
    }
    put<uint8_t>(entry, plist.size());
    for (const PresetInfo& p : plist) {
      put<uint16_t>(entry, p.id);
      put<uint8_t>(entry, p.group);
      put<uint8_t>(entry, p.stored ? 1 : 0);
      putStr(entry, p.name, FIXTURE_NAME_MAX);
    }
    if (blob.size() + entry.size() > CHUNK * MAX_CHUNKS || count == 255) break;
    blob.insert(blob.end(), entry.begin(), entry.end());
    count++;
    for (uint16_t a = f.address; a < f.address + n && a <= 512; a++) used[a] = true;
  }
  blob[0] = count;
  std::vector<uint8_t> defs;
  uint8_t defCount = 0;
  for (const FixtureDef& d : fixtureDefs) {
    if (defCount == 255 || blob.size() + defs.size() + 40 > CHUNK * MAX_CHUNKS) break;
    put<uint16_t>(defs, d.id);
    put<uint32_t>(defs, defCrc(d));
    put<uint16_t>(defs, d.channels.size());
    putStr(defs, d.name, FIXTURE_NAME_MAX);
    put<uint32_t>(defs, d.uid);
    put<uint16_t>(defs, d.rev);
    defCount++;
  }
  std::vector<DefTombstone> tombs = defTombstones();
  fixturesUnlock();
  put<uint8_t>(blob, defCount);
  blob.insert(blob.end(), defs.begin(), defs.end());
  uint8_t nt = 0;
  std::vector<uint8_t> tb;
  for (auto it = tombs.rbegin(); it != tombs.rend() && nt < 255; ++it) {
    if (blob.size() + tb.size() + 8 > CHUNK * MAX_CHUNKS) break;
    put<uint32_t>(tb, it->uid);
    put<uint16_t>(tb, it->rev);
    nt++;
  }
  put<uint8_t>(blob, nt);
  blob.insert(blob.end(), tb.begin(), tb.end());
  descBlob = blob;
  descSendMask = 0;
  ownBuilt = true;
  fullRefreshPending = true;
}

static void queueDescChunk(size_t index) {
  std::vector<uint8_t> b;
  put<uint16_t>(b, ownVersion);
  put<uint16_t>(b, (uint16_t)descBlob.size());
  put<uint8_t>(b, (uint8_t)index);
  size_t start = index * CHUNK;
  size_t end = min(descBlob.size(), start + CHUNK);
  if (start < end) b.insert(b.end(), descBlob.begin() + start, descBlob.begin() + end);
  queuePacket(PT_FIX_DESC, b);
}

static bool wantValue(uint16_t a, bool all) {
  return used[a] && (all || data[a] != lastSent[a]);
}

// Sends spans of changed (or all used) channels.
static void sendValues(bool all, bool repeat = false) {
  std::vector<uint8_t> pkt;
  uint8_t spans = 0;
  auto flush = [&]() {
    if (spans == 0) return;
    pkt.insert(pkt.begin(), spans);
    queuePacket(PT_VALUES, pkt);
    pkt.clear();
    spans = 0;
  };
  uint16_t a = 1;
  while (a <= 512) {
    if (!wantValue(a, all)) {
      a++;
      continue;
    }
    // one span may bridge small gaps of unchanged channels
    uint16_t start = a, end = a;
    for (uint16_t b = a + 1; b <= 512 && b - start < 64; b++) {
      if (wantValue(b, all)) end = b;
      else if (b - end > 3) break;
    }
    size_t len = end - start + 1;
    if (pkt.size() + 1 + 3 + len > MAX_PAYLOAD) flush();
    put<uint16_t>(pkt, start);
    put<uint8_t>(pkt, (uint8_t)len);
    for (uint16_t i = start; i <= end; i++) {
      pkt.push_back(data[i]);
      lastSent[i] = data[i];
      if (!all && !repeat) {
        repeatAddr[i] = true;
        valuesRepeatAt = millis() + VALUES_REPEAT_MS;
      }
    }
    spans++;
    a = end + 1;
  }
  flush();
}

static bool anyPeerOnline() {
  for (const BoxState& b : boxes) if (b.box.peer.online()) return true;
  return false;
}

// ---------- receiving ----------

static bool parseDesc(const std::vector<uint8_t>& blob, std::vector<RemoteFixture>& out, std::vector<RemoteDef>& defsOut,
                      std::vector<RemoteTomb>& tombsOut) {
  const uint8_t* p = blob.data();
  const uint8_t* end = p + blob.size();
  uint8_t count;
  if (!get(p, end, count)) return false;
  out.clear();
  for (uint8_t i = 0; i < count; i++) {
    RemoteFixture f;
    if (!get(p, end, f.id) || !get(p, end, f.defId) || !get(p, end, f.address) || !get(p, end, f.count) ||
        !getStr(p, end, f.name)) return false;
    for (uint16_t c = 0; c < f.count; c++) {
      String n;
      int16_t fine;
      uint8_t group;
      if (!getStr(p, end, n) || !get(p, end, fine) || !get(p, end, group)) return false;
      f.channelNames.push_back(n);
      f.fine.push_back(fine >= 0 && fine < (int16_t)f.count ? fine : -1);
      f.groups.push_back(group < GROUP_COUNT ? group : (uint8_t)GROUP_CONTROL);
    }
    uint8_t np;
    if (!get(p, end, np)) return false;
    for (uint8_t k = 0; k < np; k++) {
      RemotePreset rp;
      uint8_t stored;
      if (!get(p, end, rp.id) || !get(p, end, rp.group) || !get(p, end, stored) || !getStr(p, end, rp.name)) return false;
      rp.stored = stored != 0;
      if (rp.group < GROUP_COUNT) f.presets.push_back(rp);
    }
    out.push_back(f);
  }
  uint8_t nd;
  if (!get(p, end, nd)) return false;
  defsOut.clear();
  for (uint8_t i = 0; i < nd; i++) {
    RemoteDef d;
    if (!get(p, end, d.id) || !get(p, end, d.crc) || !get(p, end, d.channels) || !getStr(p, end, d.name) ||
        !get(p, end, d.uid) || !get(p, end, d.rev)) return false;
    defsOut.push_back(d);
  }
  tombsOut.clear();
  uint8_t nt;
  if (!get(p, end, nt)) return false;
  for (uint8_t i = 0; i < nt; i++) {
    RemoteTomb t;
    if (!get(p, end, t.uid) || !get(p, end, t.rev)) return false;
    tombsOut.push_back(t);
  }
  return true;
}

// ---------- link-wide definitions ----------

static inline bool revNewer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

static void queuePull(uint16_t box, uint32_t uid) {
  if (activePull.box && activePull.uid == uid) return;
  for (const DefPull& p : defPulls) if (p.uid == uid) return;
  defPulls.push_back({box, uid});
}

// Compares that box's definitions and deletions with ours: newer ones are fetched, deletions applied.
// Caller holds the lock.
static void reconcileDefs(BoxState& b) {
  if (!b.box.described) return;
  uint16_t src = b.box.peer.id;
  fixturesLock();
  for (const RemoteDef& rd : b.box.defs) {
    if (!rd.uid) continue;
    FixtureDef* local = findDefByUid(rd.uid);
    if (local) {
      if (revNewer(rd.rev, local->rev) || (rd.rev == local->rev && rd.crc != defCrc(*local) && src < boxId)) {
        queuePull(src, rd.uid);
      }
    } else {
      int t = defTombstoneRev(rd.uid);
      if (t >= 0 && !revNewer(rd.rev, (uint16_t)t)) continue;  // deleted here after that revision
      queuePull(src, rd.uid);
    }
  }
  for (const RemoteTomb& t : b.box.tombs) {
    FixtureDef* local = findDefByUid(t.uid);
    if (!local) {
      defAddTombstone(t.uid, t.rev);  // pass the deletion on
      continue;
    }
    if (!revNewer(t.rev, local->rev)) continue;  // changed here after it was deleted there
    bool used = false;
    for (const Fixture& f : fixtureList) used |= f.defId == local->id;
    if (used) {
      // still needed here: keep it and announce it again, newer than the deletion
      FixtureDef keep = *local;
      keep.rev = t.rev + 1;
      saveDef(keep, true);
      Serial.printf("Link: definition '%s' deleted on %04x but used here, kept\n", keep.name.c_str(), src);
    } else {
      Serial.printf("Link: definition '%s' deleted on %04x\n", local->name.c_str(), src);
      deleteDef(local->id, t.rev);
    }
  }
  fixturesUnlock();
}

// A definition received with uid: replaces ours if newer, otherwise creates it. Returns our id.
static uint16_t upsertDef(JsonDocument& doc, uint16_t src, String& err) {
  FixtureDef nd;
  if (!defFromJson(doc.as<JsonVariantConst>(), nd)) {
    err = "Definition unreadable";
    return 0;
  }
  nd.uid = doc["uid"] | 0u;
  nd.rev = doc["rev"] | 1;
  fixturesLock();
  FixtureDef* local = findDefByUid(nd.uid);
  uint16_t id = 0;
  if (local) {
    id = local->id;
    bool newer = revNewer(nd.rev, local->rev) || (nd.rev == local->rev && defCrc(nd) != defCrc(*local) && src < boxId);
    if (newer) {
      nd.id = local->id;
      err = saveDef(nd, true);
    }
  } else {
    err = saveDef(nd, true);
    id = err.isEmpty() ? nd.id : 0;
  }
  fixturesUnlock();
  return id;
}

static void processDefPulls(uint32_t now) {
  if (activePull.box && now - activePullMs > DEF_PULL_TIMEOUT_MS) activePull = {0, 0};
  if (activePull.box || defPulls.empty()) return;
  for (size_t i = 0; i < defPulls.size(); i++) {
    BoxState* b = findBox(defPulls[i].box);
    if (!b || !b->box.peer.online()) continue;
    activePull = defPulls[i];
    activePullMs = now;
    defPulls.erase(defPulls.begin() + i);
    std::vector<uint8_t> r;
    put<uint16_t>(r, activePull.box);
    put<uint32_t>(r, activePull.uid);
    queuePacket(PT_DEF_REQ, r);
    return;
  }
  defPulls.clear();  // all sources offline: asked again at the next comparison
}

static void requestDesc(BoxState& b, uint32_t mask) {
  std::vector<uint8_t> r;
  put<uint16_t>(r, b.box.peer.id);
  put<uint16_t>(r, b.asmb.version);
  put<uint32_t>(r, mask);
  queuePacket(PT_FIX_REQ, r);
  b.asmb.lastActivityMs = millis();
}

static void startDescFetch(BoxState& b, uint16_t version) {
  b.asmb = DescAssembly();
  b.asmb.version = version;
  b.asmb.attempts = 1;
  b.box.loading = true;
  requestDesc(b, 0xFFFFFFFF);
}

static void handleDescChunk(BoxState& b, uint16_t src, uint16_t version, uint16_t total, uint8_t index,
                            const uint8_t* p, size_t dlen) {
  if (b.box.described && version == b.describedVersion) return;  // already up to date
  DescAssembly& a = b.asmb;
  if (a.version != version || a.total != total || a.buf.size() != total || !b.box.loading) {
    uint8_t attempts = (a.version == version && b.box.loading) ? a.attempts : 1;
    a = DescAssembly();
    a.version = version;
    a.total = total;
    a.buf.assign(total, 0);
    a.attempts = attempts;
    b.box.loading = true;
  }
  size_t start = (size_t)index * CHUNK;
  if (index >= MAX_CHUNKS || start + dlen > total) return;
  if (dlen) memcpy(a.buf.data() + start, p, dlen);
  a.mask |= 1UL << index;
  a.lastActivityMs = millis();
  uint32_t full = chunkMask(total);
  if ((a.mask & full) != full) return;

  std::vector<RemoteFixture> fx;
  std::vector<RemoteDef> defs;
  std::vector<RemoteTomb> tombs;
  if (parseDesc(a.buf, fx, defs, tombs)) {
    b.box.fixtures = fx;
    b.box.defs = defs;
    b.box.tombs = tombs;
    reconcileDefs(b);
    b.box.described = true;
    b.describedVersion = version;
    b.box.version = version;
    Serial.printf("Link: %04x fixture list v%u: %u fixtures\n", src, version, (unsigned)fx.size());
    std::vector<uint8_t> r;
    put<uint16_t>(r, src);
    queuePacket(PT_VAL_REQ, r);
  } else {
    Serial.printf("Link: %04x fixture list v%u unreadable\n", src, version);
  }
  b.box.loading = false;
  a = DescAssembly();
}

// ---------- show metadata and play state ----------

// Caller holds the lock.
static uint16_t leaderId() {
  uint16_t id = boxId;
  for (const BoxState& b : boxes) if (b.box.peer.online() && b.box.peer.id < id) id = b.box.peer.id;
  return id;
}

static bool showNewer(uint16_t src, uint16_t ver, uint32_t crc) {
  uint16_t mine = cuesShowVersion();
  if (ver != mine) return (int16_t)(ver - mine) > 0;
  return crc != cuesShowCrc() && src < boxId;  // same version, different content: the lower box id wins
}

static void requestShow(uint16_t src, uint16_t ver, uint32_t mask) {
  std::vector<uint8_t> r;
  put<uint16_t>(r, src);
  put<uint16_t>(r, ver);
  put<uint32_t>(r, mask);
  queuePacket(PT_SHOW_REQ, r);
}

static void handleShowBeacon(uint16_t src, uint16_t showVer, uint32_t showCrc, uint16_t playSeq, uint16_t playCue,
                             uint8_t playMode, uint32_t playMs, uint16_t playEpoch) {
  cuesNoteShowVersion(showVer);
  if (showNewer(src, showVer, showCrc)) {
    bool busy = showAsm.src == src && showAsm.ver == showVer && millis() - showAsm.lastMs < 5000;
    if (!busy) {
      showAsm = ShowAssembly();
      showAsm.src = src;
      showAsm.ver = showVer;
      showAsm.lastMs = millis();
      requestShow(src, showVer, 0xFFFFFFFF);
    }
    return;
  }
  // once both have the same show: adopt a newer play state, or the leader's timing
  if (showVer == cuesShowVersion() && showCrc == cuesShowCrc()) {
    cuesAlign(playSeq, playCue, playMode, playMs + BEACON_AIRTIME_MS, playEpoch, src == leaderId());
  }
}

static void handleShowChunk(uint16_t src, uint16_t ver, uint16_t total, uint8_t index, const uint8_t* p, size_t dlen) {
  cuesNoteShowVersion(ver);
  if (index >= MAX_CHUNKS || (size_t)index * CHUNK + dlen > total) return;
  if (showAsm.src != src || showAsm.ver != ver || showAsm.total != total || showAsm.buf.size() != total) {
    if (!showNewer(src, ver, 0)) {
      // only a newer version (or one we asked for) is assembled
      if (!(showAsm.src == src && showAsm.ver == ver)) return;
    }
    showAsm = ShowAssembly();
    showAsm.src = src;
    showAsm.ver = ver;
    showAsm.total = total;
    showAsm.buf.assign(total, 0);
  }
  if (dlen) memcpy(showAsm.buf.data() + (size_t)index * CHUNK, p, dlen);
  showAsm.mask |= 1UL << index;
  showAsm.lastMs = millis();
  uint32_t full = chunkMask(total);
  if ((showAsm.mask & full) != full) return;
  bool ok = cuesApplyShowBlob(showAsm.buf.data(), showAsm.buf.size());
  Serial.printf("Link: show v%u from %04x %s\n", ver, src, ok ? "applied" : "unreadable");
  if (ok) showVerApplied = true;
  showAsm = ShowAssembly();
}

static void queueShowChunk(size_t index) {
  std::vector<uint8_t> b;
  put<uint16_t>(b, showBlobVer);
  put<uint16_t>(b, (uint16_t)showBlob.size());
  put<uint8_t>(b, (uint8_t)index);
  size_t start = index * CHUNK;
  size_t stop = min(showBlob.size(), start + CHUNK);
  if (start < stop) b.insert(b.end(), showBlob.begin() + start, showBlob.begin() + stop);
  queuePacket(PT_SHOW, b);
}

static void sendMarks() {
  uint8_t bm[64];
  cuesMarksBitmap(bm);
  std::vector<uint8_t> b;
  put<uint16_t>(b, cuesMarkCount());
  b.insert(b.end(), bm, bm + 64);
  queuePacket(PT_MARKS, b);
}

static void handlePacket(const uint8_t* buf, size_t len, float rssi, float snr) {
  if (len < HDR_LEN + MAC_LEN) return;
  PktHdr h;
  memcpy(&h, buf, HDR_LEN);
  if (h.m0 != 'D' || h.m1 != 'X' || h.ver != PROTO_VER || h.net != netId || h.src == boxId) return;
  uint8_t mac[MAC_LEN];
  packetMac(buf, len - MAC_LEN, mac);
  if (memcmp(mac, buf + len - MAC_LEN, MAC_LEN) != 0) {
    Serial.printf("Link: bad MAC from %04x\n", h.src);
    return;
  }
  const uint8_t* p = buf + HDR_LEN;
  const uint8_t* end = buf + len - MAC_LEN;

  BoxState& b = boxFor(h.src);
  PeerInfo& peer = b.box.peer;
  bool wasOnline = peer.online();
  peer.lastSeenMs = millis();
  peer.rssi = rssi;
  peer.snr = snr;
  if (!wasOnline) fullRefreshPending = true;  // a newcomer needs our values

  switch (h.type) {
    case PT_BEACON: {
      if (!get(p, end, peer.fixVersion) || !get(p, end, peer.fixCount) || !get(p, end, peer.uptimeS) ||
          !getStr(p, end, peer.name)) return;
      peer.named = true;
      uint16_t showVer, playSeq, playCue, playEpoch;
      uint32_t showCrc, playMs;
      uint8_t playMode;
      if (get(p, end, showVer) && get(p, end, showCrc) && get(p, end, playSeq) && get(p, end, playCue) &&
          get(p, end, playMode) && get(p, end, playMs) && get(p, end, playEpoch)) {
        handleShowBeacon(h.src, showVer, showCrc, playSeq, playCue, playMode, playMs, playEpoch);
        // firmware fields came later: boxes with older firmware end the beacon here
        uint32_t fwCode;
        uint8_t fwFlags;
        if (get(p, end, fwCode) && get(p, end, fwFlags)) {
          peer.fwKnown = true;
          peer.fwCode = fwCode;
          peer.fwFlags = fwFlags;
        }
      }
      if (!wasOnline) {
        Serial.printf("Link: peer %04x '%s' online (RSSI %.0f, SNR %.1f)\n", h.src, peer.name.c_str(), rssi, snr);
        beaconRequested = true;
      }
      bool outdated = !b.box.described || peer.fixVersion != b.describedVersion;
      bool alreadyLoading = b.box.loading && b.asmb.version == peer.fixVersion;
      if (outdated && !alreadyLoading) startDescFetch(b, peer.fixVersion);
      break;
    }
    case PT_PING: {
      uint32_t t0;
      uint16_t target;
      if (!get(p, end, t0) || !get(p, end, target)) return;
      if (target != 0xFFFF && target != boxId) return;
      std::vector<uint8_t> r;
      put<uint32_t>(r, t0);
      put<uint16_t>(r, h.src);
      put<int16_t>(r, (int16_t)(rssi * 10));
      put<int8_t>(r, (int8_t)(snr * 4));
      queuePacket(PT_PONG, r);
      break;
    }
    case PT_PONG: {
      uint32_t t0;
      uint16_t target;
      int16_t r10;
      int8_t s4;
      if (!get(p, end, t0) || !get(p, end, target) || !get(p, end, r10) || !get(p, end, s4)) return;
      if (target != boxId) return;
      peer.pingRttMs = millis() - t0;
      peer.theirRssi = r10 / 10.0f;
      peer.theirSnr = s4 / 4.0f;
      break;
    }
    case PT_FIX_REQ: {
      uint16_t target, version;
      uint32_t mask;
      if (!get(p, end, target) || !get(p, end, version) || !get(p, end, mask)) return;
      if (target != boxId) return;
      if (version != ownVersion) mask = 0xFFFFFFFF;  // they asked for an outdated list
      descSendMask |= mask & chunkMask(descBlob.size());
      break;
    }
    case PT_FIX_DESC: {
      uint16_t version, total;
      uint8_t index;
      if (!get(p, end, version) || !get(p, end, total) || !get(p, end, index)) return;
      handleDescChunk(b, h.src, version, total, index, p, end - p);
      break;
    }
    case PT_VAL_REQ: {
      uint16_t target;
      if (!get(p, end, target) || target != boxId) return;
      fullRefreshPending = true;
      break;
    }
    case PT_VALUES: {
      uint8_t spans;
      if (!get(p, end, spans)) return;
      for (uint8_t s = 0; s < spans; s++) {
        uint16_t start;
        uint8_t n;
        if (!get(p, end, start) || !get(p, end, n) || p + n > end) return;
        for (uint8_t i = 0; i < n; i++) {
          uint16_t addr = start + i;
          if (addr >= 1 && addr <= 512) b.box.values[addr] = p[i];
        }
        p += n;
      }
      b.box.haveValues = true;
      b.box.valuesMs = millis();
      pendingSets.erase(std::remove_if(pendingSets.begin(), pendingSets.end(),
                                       [&](const PendingSet& ps) {
                                         return ps.box == h.src && ps.attempts > 0 &&
                                                b.box.values[ps.addr] == ps.value;
                                       }),
                        pendingSets.end());
      break;
    }
    case PT_SET: {
      uint16_t target;
      uint8_t n;
      if (!get(p, end, target) || target != boxId || !get(p, end, n)) return;
      for (uint8_t i = 0; i < n; i++) {
        uint16_t addr;
        uint8_t v;
        if (!get(p, end, addr) || !get(p, end, v)) return;
        if (addr < 1 || addr > 512) continue;
        data[addr] = v;
        lastSent[addr] = ~v;  // force a VALUES reply as confirmation, even if unchanged
        presetChannelsChanged(addr, addr);
        cuesManualChange(addr, addr);
      }
      valuesCheckNow = true;
      break;
    }
    case PT_REFS: {
      uint8_t offset, n;
      if (!get(p, end, offset) || !get(p, end, n)) return;
      if (offset == 0) b.box.refs.clear();
      for (uint8_t i = 0; i < n; i++) {
        RemoteRef r;
        if (!get(p, end, r.fixtureId) || !get(p, end, r.group) || !get(p, end, r.presetId)) return;
        b.box.refs.push_back(r);
      }
      break;
    }
    case PT_CMD: {
      uint16_t target, nonce, fixtureId, presetId;
      uint8_t op, group;
      String name;
      uint16_t address, defId, count;
      if (!get(p, end, target) || target != boxId || !get(p, end, nonce) || !get(p, end, op) ||
          !get(p, end, fixtureId) || !get(p, end, group) || !get(p, end, presetId) || !getStr(p, end, name) ||
          !get(p, end, address) || !get(p, end, defId) || !get(p, end, count)) return;
      String err, reply;
      uint16_t src = h.src;
      bool dup = std::find(doneCmds.begin(), doneCmds.end(), std::make_pair(src, nonce)) != doneCmds.end();
      if (!dup) {
        switch (op) {
          case PRESET_RECALL: err = presetRecall(fixtureId, presetId); break;
          case PRESET_STORE: err = presetStore(fixtureId, presetId); break;
          case PRESET_CREATE: err = presetCreate(fixtureId, group, name); break;
          case PRESET_RENAME: err = presetRename(presetId, name); break;
          case PRESET_DELETE: err = presetDelete(presetId); break;
          case CMD_FIXTURE_SAVE: {
            Fixture f;
            if (fixtureId) {
              Fixture* existing = findFixture(fixtureId);
              if (!existing) {
                err = "Fixture not found";
                break;
              }
              f = *existing;
            }
            f.name = name;
            f.address = address;
            f.defId = defId;
            f.channelCount = count;
            err = saveFixture(f);
            break;
          }
          case CMD_FIXTURE_DELETE:
            err = deleteFixture(fixtureId);
            if (err.isEmpty()) presetsFixtureDeleted(fixtureId);
            break;
          case CMD_CUE_STORE:
            reply = String(cuesStorePart(fixtureId, presetId, group != 0, count));
            break;
          case CMD_TRANSPORT: {
            // a late retry must not undo a newer transport from the same box
            auto it = std::find_if(lastTransport.begin(), lastTransport.end(),
                                   [&](const LastTransport& e) { return e.src == src; });
            uint32_t nowMs = millis();
            if (it != lastTransport.end() && nowMs - it->ms < TRANSPORT_ORDER_MS && (int16_t)(nonce - it->nonce) < 0) break;
            if (it != lastTransport.end()) *it = {src, nonce, nowMs};
            else lastTransport.push_back({src, nonce, nowMs});
            CueTransport t;
            t.seq = fixtureId;
            t.cue = presetId;
            t.mode = group;
            t.move = address;
            t.epoch = count;
            cuesApplyTransport(t);
            break;
          }
          case CMD_CLEAR:  // address .. count = DMX range
            cuesClearChannels(address, count);
            break;
          case CMD_FW_UPDATE:
            err = firmwareInstall();
            break;
          default: err = "Unknown action";
        }
        doneCmds.push_back({src, nonce});
        if (doneCmds.size() > 32) doneCmds.erase(doneCmds.begin());
        if (op == CMD_CUE_STORE) storedReplies.push_back({{src, nonce}, reply});
        if (storedReplies.size() > 8) storedReplies.erase(storedReplies.begin());
        Serial.printf("Link: command %u from %04x: %s\n", op, h.src, err.isEmpty() ? "ok" : err.c_str());
        // a cue or sequence we do not know yet: fetch the sender's show right away
        if ((op == CMD_CUE_STORE || op == CMD_TRANSPORT) && cuesNeedsShow()) requestShow(src, 0, 0xFFFFFFFF);
        // changed values go out before the ACK, so the sender has them when it sees the confirmation
        sendValues(false);
      }
      if (dup && op == CMD_CUE_STORE) {
        for (const auto& e : storedReplies) if (e.first == std::make_pair(src, nonce)) reply = e.second;
      }
      std::vector<uint8_t> r;
      put<uint16_t>(r, h.src);
      put<uint16_t>(r, nonce);
      put<uint8_t>(r, err.isEmpty() ? 1 : 0);
      putStr(r, err.isEmpty() ? reply : err, 120);
      // list version after the command: the sender fetches the new list right away instead of
      // relying on the (single, losable) beacon
      put<uint16_t>(r, fixturesVersion());
      // preset ops: the fixture's current references ride along, so the sender's page can show
      // the active preset together with the confirmation instead of waiting for the next PT_REFS
      if (op >= PRESET_RECALL && op <= PRESET_DELETE) {
        std::vector<PresetRef> fr;
        for (const PresetRef& pr : presetRefs()) if (pr.fixtureId == fixtureId) fr.push_back(pr);
        put<uint16_t>(r, fixtureId);
        put<uint8_t>(r, (uint8_t)min(fr.size(), (size_t)GROUP_COUNT));
        for (size_t i = 0; i < fr.size() && i < GROUP_COUNT; i++) {
          put<uint8_t>(r, fr[i].group);
          put<uint16_t>(r, fr[i].presetId);
        }
      }
      queuePacket(PT_ACK, r);
      break;
    }
    case PT_ACK: {
      uint16_t target, nonce;
      uint8_t ok;
      String msg;
      if (!get(p, end, target) || target != boxId || !get(p, end, nonce) || !get(p, end, ok) || !getStr(p, end, msg)) return;
      bool cueOp = false;
      for (const PendingCmd& c : pendingCmds) {
        if (c.box != h.src || c.nonce != nonce) continue;
        cueOp = c.op == CMD_CUE_STORE || c.op == CMD_TRANSPORT;
        if (c.op == CMD_CUE_STORE) storeResult(c.box, ok, msg.toInt());
        // confirmed clear: drop the marks right away instead of waiting for that box's next PT_MARKS
        if (c.op == CMD_CLEAR && ok) {
          for (uint16_t d = max<uint16_t>(c.address, 1); d <= c.count && d <= 512; d++) {
            uint8_t bit = 1 << ((d - 1) & 7);
            if (b.box.marks[(d - 1) >> 3] & bit) {
              b.box.marks[(d - 1) >> 3] &= ~bit;
              if (b.box.markCount) b.box.markCount--;
            }
          }
        }
      }
      pendingCmds.erase(std::remove_if(pendingCmds.begin(), pendingCmds.end(),
                                       [&](const PendingCmd& c) { return c.box == h.src && c.nonce == nonce; }),
                        pendingCmds.end());
      // an error is only cleared by the success of a later command (not by a late retry of an earlier
      // one, and not by the frequent cue confirmations)
      if (!ok) {
        b.box.lastError = msg;
        b.box.lastErrorMs = millis();
        b.errorNonce = nonce;
      } else if (!cueOp && (b.box.lastError.isEmpty() || (int16_t)(nonce - b.errorNonce) > 0)) {
        b.box.lastError = "";
        b.box.lastErrorMs = millis();
      }
      uint16_t listVersion;
      if (get(p, end, listVersion)) {
        bool outdated = !b.box.described || listVersion != b.describedVersion;
        bool alreadyLoading = b.box.loading && b.asmb.version == listVersion;
        if (outdated && !alreadyLoading) startDescFetch(b, listVersion);
      }
      uint16_t refFixture;
      uint8_t nrefs;
      if (get(p, end, refFixture) && get(p, end, nrefs)) {
        std::vector<RemoteRef> fresh;
        for (uint8_t i = 0; i < nrefs; i++) {
          RemoteRef rr;
          rr.fixtureId = refFixture;
          if (!get(p, end, rr.group) || !get(p, end, rr.presetId)) break;
          fresh.push_back(rr);
        }
        auto& refs = b.box.refs;
        refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const RemoteRef& x) { return x.fixtureId == refFixture; }),
                   refs.end());
        refs.insert(refs.end(), fresh.begin(), fresh.end());
      }
      break;
    }
    case PT_SHOW: {
      uint16_t ver, total;
      uint8_t index;
      if (!get(p, end, ver) || !get(p, end, total) || !get(p, end, index)) return;
      handleShowChunk(h.src, ver, total, index, p, end - p);
      break;
    }
    case PT_SHOW_REQ: {
      uint16_t target, ver;
      uint32_t mask;
      if (!get(p, end, target) || target != boxId || !get(p, end, ver) || !get(p, end, mask)) return;
      if (ver != showBlobVer) mask = 0xFFFFFFFF;
      showSendMask |= mask & chunkMask(showBlob.size());
      break;
    }
    case PT_MARKS: {
      uint16_t n;
      if (!get(p, end, n) || p + 64 > end) return;
      b.box.markCount = n;
      memcpy(b.box.marks, p, 64);
      break;
    }
    case PT_DEF_REQ: {
      uint16_t target;
      uint32_t uid;
      if (!get(p, end, target) || target != boxId || !get(p, end, uid)) return;
      bool busy = false;
      for (const DefPush& dp : defPushes) busy |= dp.box == h.src && dp.uid == uid;
      if (busy) break;
      fixturesLock();
      FixtureDef* d = findDefByUid(uid);
      std::vector<uint8_t> json;
      if (d) json = defTransferJson(*d);
      fixturesUnlock();
      if (json.empty()) break;
      DefPush dp;
      dp.box = h.src;
      dp.xfer = nextNonce++;
      dp.uid = uid;
      dp.json = json;
      dp.then.op = 0;
      defPushes.push_back(dp);
      break;
    }
    case PT_DEF_PUSH: {
      uint16_t target, xfer, total;
      uint8_t index;
      if (!get(p, end, target) || target != boxId || !get(p, end, xfer) || !get(p, end, total) || !get(p, end, index)) return;
      uint16_t src = h.src;
      DefAssembly* a = nullptr;
      for (DefAssembly& x : defAssemblies) if (x.src == src && x.xfer == xfer) a = &x;
      if (!a) {
        if (defAssemblies.size() >= 4) defAssemblies.erase(defAssemblies.begin());
        DefAssembly n;
        n.src = src;
        n.xfer = xfer;
        n.total = total;
        n.buf.assign(total, 0);
        defAssemblies.push_back(n);
        a = &defAssemblies.back();
      }
      a->lastMs = millis();
      if (!a->done) {
        size_t start = (size_t)index * CHUNK, dlen = end - p;
        if (index >= MAX_CHUNKS || start + dlen > a->total) return;
        if (dlen) memcpy(a->buf.data() + start, p, dlen);
        a->mask |= 1UL << index;
        if ((a->mask & chunkMask(a->total)) != chunkMask(a->total)) return;
        // complete: link-wide definitions (with uid) are matched by uid; older senders by content
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)a->buf.data(), a->buf.size())) {
          a->err = "Definition unreadable";
        } else if (doc["uid"].is<uint32_t>()) {
          a->defId = upsertDef(doc, src, a->err);
          uint32_t uid = doc["uid"] | 0u;
          if (activePull.box == src && activePull.uid == uid) activePull = {0, 0};
        } else {
          uint32_t crc = esp_rom_crc32_le(0, a->buf.data(), a->buf.size());
          fixturesLock();
          FixtureDef* same = findDefByCrc(crc);
          if (same) a->defId = same->id;
          else a->defId = importSingleDef(doc.as<JsonVariantConst>(), a->err);
          fixturesUnlock();
        }
        a->done = true;
        Serial.printf("Link: definition from %04x -> id %u %s\n", src, a->defId, a->err.c_str());
      }
      std::vector<uint8_t> r;
      put<uint16_t>(r, src);
      put<uint16_t>(r, xfer);
      put<uint8_t>(r, a->defId ? 1 : 0);
      put<uint16_t>(r, a->defId);
      putStr(r, a->err, 120);
      queuePacket(PT_DEF_ACK, r);
      break;
    }
    case PT_DEF_ACK: {
      uint16_t target, xfer, defId;
      uint8_t ok;
      String msg;
      if (!get(p, end, target) || target != boxId || !get(p, end, xfer) || !get(p, end, ok) || !get(p, end, defId) ||
          !getStr(p, end, msg)) return;
      for (size_t i = 0; i < defPushes.size(); i++) {
        if (defPushes[i].box != h.src || defPushes[i].xfer != xfer) continue;
        if (ok && defPushes[i].then.op) {
          PendingCmd c = defPushes[i].then;
          c.defId = defId;
          pendingCmds.push_back(c);
        } else if (!ok && defPushes[i].then.op) {
          b.box.lastError = msg;
          b.box.lastErrorMs = millis();
        }
        defPushes.erase(defPushes.begin() + i);
        break;
      }
      break;
    }
  }
}

// ---------- periodic work ----------

static void processPendingSets(uint32_t now) {
  for (BoxState& b : boxes) {
    uint16_t id = b.box.peer.id;
    if (now - b.lastSetPacketMs < SET_MIN_INTERVAL_MS) continue;
    std::vector<uint8_t> pairs;
    uint8_t n = 0;
    for (PendingSet& ps : pendingSets) {
      if (ps.box != id || ps.attempts == SET_FAILED) continue;
      if (ps.attempts > 0 && now - ps.lastSendMs < SET_RETRY_MS) continue;
      if (ps.attempts >= SET_MAX_ATTEMPTS || !b.box.peer.online()) {
        ps.attempts = SET_FAILED;
        continue;
      }
      if (n >= 70) break;
      put<uint16_t>(pairs, ps.addr);
      put<uint8_t>(pairs, ps.value);
      ps.attempts++;
      ps.lastSendMs = now;
      n++;
    }
    if (n) {
      std::vector<uint8_t> pkt;
      put<uint16_t>(pkt, id);
      put<uint8_t>(pkt, n);
      pkt.insert(pkt.end(), pairs.begin(), pairs.end());
      queuePacket(PT_SET, pkt);
      b.lastSetPacketMs = now;
    }
  }
  for (const PendingSet& ps : pendingSets) {
    if (ps.attempts != SET_FAILED) continue;
    BoxState* b = findBox(ps.box);
    if (b && std::find(b->box.failed.begin(), b->box.failed.end(), ps.addr) == b->box.failed.end()) {
      b->box.failed.push_back(ps.addr);
    }
    Serial.printf("Link: SET %04x/%u=%u not confirmed\n", ps.box, ps.addr, ps.value);
  }
  pendingSets.erase(std::remove_if(pendingSets.begin(), pendingSets.end(),
                                   [](const PendingSet& ps) { return ps.attempts == SET_FAILED; }),
                    pendingSets.end());
}

static void processPendingCmds(uint32_t now) {
  for (PendingCmd& c : pendingCmds) {
    // jittered, so that retries do not keep colliding with periodic traffic
    if (c.attempts > 0 && now - c.lastSendMs < CMD_RETRY_MS + (c.nonce * 37u) % 500) continue;
    BoxState* b = findBox(c.box);
    if (c.attempts >= CMD_MAX_ATTEMPTS || !b || !b->box.peer.online()) {
      if (b) {
        b->box.lastError = "no answer";
        b->box.lastErrorMs = now;
      }
      if (c.op == CMD_CUE_STORE) storeResult(c.box, false, 0);
      c.attempts = 0xFF;
      continue;
    }
    std::vector<uint8_t> pkt;
    put<uint16_t>(pkt, c.box);
    put<uint16_t>(pkt, c.nonce);
    put<uint8_t>(pkt, c.op);
    put<uint16_t>(pkt, c.fixtureId);
    put<uint8_t>(pkt, c.group);
    put<uint16_t>(pkt, c.presetId);
    putStr(pkt, c.name, FIXTURE_NAME_MAX);
    put<uint16_t>(pkt, c.address);
    put<uint16_t>(pkt, c.defId);
    put<uint16_t>(pkt, c.count);
    queuePacket(PT_CMD, pkt);
    c.attempts++;
    c.lastSendMs = now;
  }
  pendingCmds.erase(std::remove_if(pendingCmds.begin(), pendingCmds.end(),
                                   [](const PendingCmd& c) { return c.attempts == 0xFF; }),
                    pendingCmds.end());
}

static void sendRefs() {
  std::vector<PresetRef> refs = presetRefs();
  size_t perPacket = (MAX_PAYLOAD - 2) / 5;
  size_t offset = 0;
  do {
    size_t n = min(perPacket, refs.size() - offset);
    std::vector<uint8_t> pkt;
    put<uint8_t>(pkt, (uint8_t)min(offset, (size_t)255));
    put<uint8_t>(pkt, (uint8_t)n);
    for (size_t i = 0; i < n; i++) {
      put<uint16_t>(pkt, refs[offset + i].fixtureId);
      put<uint8_t>(pkt, refs[offset + i].group);
      put<uint16_t>(pkt, refs[offset + i].presetId);
    }
    queuePacket(PT_REFS, pkt);
    offset += n;
  } while (offset < refs.size());
}

static void processDescRetries(uint32_t now) {
  for (BoxState& b : boxes) {
    if (!b.box.loading || now - b.asmb.lastActivityMs < DESC_RETRY_MS) continue;
    if (b.asmb.attempts >= DESC_MAX_ATTEMPTS || !b.box.peer.online()) {
      b.box.loading = false;  // retried on the next beacon
      continue;
    }
    b.asmb.attempts++;
    uint32_t missing = b.asmb.buf.empty() && b.asmb.mask == 0 ? 0xFFFFFFFF : chunkMask(b.asmb.total) & ~b.asmb.mask;
    requestDesc(b, missing);
  }
}

static void buildBeacon() {
  std::vector<uint8_t> b;
  put<uint16_t>(b, ownVersion);
  put<uint8_t>(b, descBlob.empty() ? 0 : descBlob[0]);
  put<uint32_t>(b, millis() / 1000);
  putStr(b, settings.name, 32);
  PlayInfo pi = cuesPlayInfo();
  put<uint16_t>(b, cuesShowVersion());
  put<uint32_t>(b, cuesShowCrc());
  put<uint16_t>(b, pi.seq);
  put<uint16_t>(b, pi.cue);
  put<uint8_t>(b, pi.mode);
  put<uint32_t>(b, pi.ms);
  put<uint16_t>(b, pi.epoch);
  put<uint32_t>(b, firmwareVersionCode());
  put<uint8_t>(b, firmwareFlags());
  queuePacket(PT_BEACON, b);
}

void syncBegin(const char* defaultName) {
  mtx = xSemaphoreCreateMutex();
  uint64_t mac = ESP.getEfuseMac();
  boxId = (uint16_t)((((mac >> 32) & 0xFF) << 8) | ((mac >> 40) & 0xFF));
  loadSettings(defaultName);
  applySettings();
  if (settings.enabled) transport->begin();
  randomSeed(esp_random());
  nextNonce = esp_random();  // command nonces of the previous run are still remembered by the others
  nextBeaconMs = millis() + random(2000, 4000);
  Serial.printf("Link: box %04x '%s', %s\n", boxId, settings.name.c_str(),
                transport->ready() ? "radio ok" : "radio off");
}

void syncLoop() {
  if (!transport->ready()) return;

  uint8_t buf[LINK_MAX_PACKET];
  float rssi, snr;
  size_t len = transport->poll(buf, sizeof(buf), rssi, snr);
  Lock l;
  if (!settings.enabled) {
    txQueue.clear();
    return;
  }
  uint32_t now = millis();
  if (len) {
    handlePacket(buf, len, rssi, snr);
    txHoldUntilMs = now + random(RX_HOLDOFF_MIN_MS, RX_HOLDOFF_MAX_MS);
  }

  static uint32_t beaconRepeatAt = 0;
  if (!ownBuilt || fixturesVersion() != ownVersion) {
    rebuildOwn();
    beaconRequested = true;
    beaconRepeatAt = now + 2000;  // a second beacon in case the first one got lost
  }
  // the others see "update available" / "updating" without waiting for the next regular beacon
  static uint8_t lastFwFlags = 0;
  uint8_t fwFlags = firmwareFlags();
  if (fwFlags != lastFwFlags) {
    lastFwFlags = fwFlags;
    beaconRequested = true;
  }
  if (beaconRepeatAt && (int32_t)(now - beaconRepeatAt) >= 0) {
    beaconRepeatAt = 0;
    beaconRequested = true;
  }
  if (beaconRequested || (int32_t)(now - nextBeaconMs) >= 0) {
    beaconRequested = false;
    nextBeaconMs = now + BEACON_INTERVAL_MS + random(0, 2000);
    buildBeacon();
  }
  if (pingRequested) {
    pingRequested = false;
    std::vector<uint8_t> b;
    put<uint32_t>(b, now);
    put<uint16_t>(b, 0xFFFF);
    queuePacket(PT_PING, b);
  }

  if (valuesRepeatAt && (int32_t)(now - valuesRepeatAt) >= 0) {
    valuesRepeatAt = 0;
    for (uint16_t a = 1; a <= 512; a++) {
      if (repeatAddr[a]) lastSent[a] = ~data[a];
    }
    memset(repeatAddr, 0, sizeof(repeatAddr));
    if (anyPeerOnline()) sendValues(false, true);
  }
  if (valuesCheckNow || now - lastValuesCheckMs >= VALUES_CHECK_MS) {
    bool force = valuesCheckNow;
    valuesCheckNow = false;
    lastValuesCheckMs = now;
    if (anyPeerOnline() || force) {
      bool all = fullRefreshPending || (int32_t)(now - nextFullRefreshMs) >= 0;
      if (all) {
        fullRefreshPending = false;
        nextFullRefreshMs = now + VALUES_REFRESH_MS;
      }
      sendValues(all);
      if (all) {
        refsSendPending = true;
        marksSendPending = true;
      }
    }
  }
  // references go out twice: a single packet can collide with the fixture list transfer that
  // usually follows a preset change
  static uint32_t refsRepeatAt = 0;
  if (presetRefsVersion() != lastRefsVersion) {
    lastRefsVersion = presetRefsVersion();
    refsSendPending = true;
    refsRepeatAt = now + 2500;
  }
  if (refsRepeatAt && (int32_t)(now - refsRepeatAt) >= 0) {
    refsRepeatAt = 0;
    refsSendPending = true;
  }
  if (refsSendPending && anyPeerOnline()) {
    refsSendPending = false;
    sendRefs();
  }
  // show metadata: rebuild the blob on every change, broadcast it when the change was made here
  uint16_t sv = cuesShowVersion();
  if (sv != lastShowVer) {
    lastShowVer = sv;
    String blob = cuesShowBlob();
    showBlob.assign(blob.c_str(), blob.c_str() + blob.length());
    showBlobVer = sv;
    showSendMask = 0;
    if (!showVerApplied && anyPeerOnline()) {
      showSendMask = chunkMask(showBlob.size());
      beaconRepeatAt = now + 2500;  // carries the new version for boxes that missed a chunk
    }
    showVerApplied = false;
  }
  CueTransport hold;
  if (cuesTakeHoldEvent(hold)) queueCmdToAll(CMD_TRANSPORT, hold.seq, hold.mode, hold.cue, hold.move, hold.epoch);
  // marks go out twice, like references: a single packet can be lost
  static uint32_t marksRepeatAt = 0;
  if (cuesMarksVersion() != lastMarksVer) {
    marksSendPending = true;
    marksRepeatAt = now + 2500;
  }
  if (marksRepeatAt && (int32_t)(now - marksRepeatAt) >= 0) {
    marksRepeatAt = 0;
    marksSendPending = true;
  }
  if (marksSendPending && now - lastMarksSentMs >= MARKS_MIN_INTERVAL_MS && anyPeerOnline()) {
    marksSendPending = false;
    lastMarksVer = cuesMarksVersion();
    lastMarksSentMs = now;
    sendMarks();
  }
  processPendingCmds(now);
  static uint32_t lastReconcileMs = 0;
  if (now - lastReconcileMs >= DEF_RECONCILE_MS) {
    lastReconcileMs = now;
    for (BoxState& b : boxes) if (b.box.peer.online()) reconcileDefs(b);
  }
  processDefPulls(now);

  processPendingSets(now);
  processDescRetries(now);

  // definition transfers: (re)start, give up after a few attempts
  for (size_t i = 0; i < defPushes.size(); i++) {
    DefPush& dp = defPushes[i];
    if (dp.sendMask || (dp.attempts && now - dp.lastSendMs < DEF_PUSH_RETRY_MS)) continue;
    BoxState* b = findBox(dp.box);
    if (dp.attempts >= DEF_PUSH_MAX_ATTEMPTS || !b || !b->box.peer.online()) {
      if (b && dp.then.op) {
        b->box.lastError = "definition transfer failed";
        b->box.lastErrorMs = now;
      }
      defPushes.erase(defPushes.begin() + i);
      break;
    }
    dp.attempts++;
    dp.sendMask = chunkMask(dp.json.size());
  }
  if (txQueue.empty()) {
    for (DefPush& dp : defPushes) {
      if (!dp.sendMask) continue;
      size_t i = 0;
      while (!(dp.sendMask & (1UL << i))) i++;
      dp.sendMask &= ~(1UL << i);
      std::vector<uint8_t> pkt;
      put<uint16_t>(pkt, dp.box);
      put<uint16_t>(pkt, dp.xfer);
      put<uint16_t>(pkt, (uint16_t)dp.json.size());
      put<uint8_t>(pkt, (uint8_t)i);
      size_t start = i * CHUNK, stop = min(dp.json.size(), start + CHUNK);
      pkt.insert(pkt.end(), dp.json.begin() + start, dp.json.begin() + stop);
      queuePacket(PT_DEF_PUSH, pkt);
      if (!dp.sendMask) dp.lastSendMs = now;
      break;
    }
  }

  if (showSendMask && txQueue.empty()) {
    for (size_t i = 0; i < MAX_CHUNKS; i++) {
      if (showSendMask & (1UL << i)) {
        showSendMask &= ~(1UL << i);
        queueShowChunk(i);
        break;
      }
    }
  }

  // fixture list chunks only go out when nothing else is waiting
  if (descSendMask && txQueue.empty()) {
    for (size_t i = 0; i < MAX_CHUNKS; i++) {
      if (descSendMask & (1UL << i)) {
        descSendMask &= ~(1UL << i);
        queueDescChunk(i);
        break;
      }
    }
  }

  if (!txQueue.empty() && !transport->busy() && (int32_t)(now - txHoldUntilMs) >= 0) {
    // dropped on failure (airtime budget exhausted or radio error)
    transport->send(txQueue.front().data(), txQueue.front().size());
    txQueue.erase(txQueue.begin());
  }
}
#endif
