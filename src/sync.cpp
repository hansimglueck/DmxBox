#ifdef HAS_LORA
#include "sync.h"
#include "lora_link.h"
#include "fixtures.h"
#include "presets.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <algorithm>

extern uint8_t data[];

static const char* LINK_FILE = "/link.json";
static const uint8_t PROTO_VER = 3;
static const uint32_t BEACON_INTERVAL_MS = 30000;
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
};
static const size_t PRESETS_PER_FIXTURE_MAX = 60;
static const uint32_t CMD_RETRY_MS = 1500;
static const uint8_t CMD_MAX_ATTEMPTS = 3;

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
  uint16_t box, nonce;
  uint8_t op;
  uint16_t fixtureId;
  uint8_t group;
  uint16_t presetId;
  String name;
  uint8_t attempts;
  uint32_t lastSendMs;
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
      settings.name = doc["name"] | defaultName;
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
  if (n.name.isEmpty() || n.name.length() > 32) return "Name muss 1–32 Zeichen lang sein";
  if (n.password.length() < 4) return "Passwort muss mindestens 4 Zeichen haben";
  if (n.txPower < 0 || n.txPower > 27) return "Sendeleistung muss zwischen 0 und 27 dBm liegen";
  JsonDocument doc;
  doc["name"] = n.name;
  doc["password"] = n.password;
  doc["enabled"] = n.enabled;
  doc["txPower"] = n.txPower;
  File f = LittleFS.open(LINK_FILE, "w");
  if (!f || serializeJson(doc, f) == 0) return "Speichern fehlgeschlagen";
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

void syncPresetCommand(uint16_t box, uint8_t op, uint16_t fixtureId, uint8_t group, uint16_t presetId,
                       const String& name) {
  Lock l;
  pendingCmds.push_back({box, nextNonce++, op, fixtureId, group, presetId, name, 0, 0});
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
  fixturesUnlock();
  blob[0] = count;
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
static void sendValues(bool all) {
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

static bool parseDesc(const std::vector<uint8_t>& blob, std::vector<RemoteFixture>& out) {
  const uint8_t* p = blob.data();
  const uint8_t* end = p + blob.size();
  uint8_t count;
  if (!get(p, end, count)) return false;
  out.clear();
  for (uint8_t i = 0; i < count; i++) {
    RemoteFixture f;
    if (!get(p, end, f.id) || !get(p, end, f.address) || !get(p, end, f.count) || !getStr(p, end, f.name)) return false;
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
  return true;
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
  if (parseDesc(a.buf, fx)) {
    b.box.fixtures = fx;
    b.box.described = true;
    b.describedVersion = version;
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
      if (!get(p, end, target) || target != boxId || !get(p, end, nonce) || !get(p, end, op) ||
          !get(p, end, fixtureId) || !get(p, end, group) || !get(p, end, presetId) || !getStr(p, end, name)) return;
      String err;
      uint16_t src = h.src;
      bool dup = std::find(doneCmds.begin(), doneCmds.end(), std::make_pair(src, nonce)) != doneCmds.end();
      if (!dup) {
        switch (op) {
          case PRESET_RECALL: err = presetRecall(fixtureId, presetId); break;
          case PRESET_STORE: err = presetStore(fixtureId, presetId); break;
          case PRESET_CREATE: err = presetCreate(fixtureId, group, name); break;
          case PRESET_RENAME: err = presetRename(presetId, name); break;
          case PRESET_DELETE: err = presetDelete(presetId); break;
          default: err = "Unbekannte Aktion";
        }
        doneCmds.push_back({src, nonce});
        if (doneCmds.size() > 32) doneCmds.erase(doneCmds.begin());
        Serial.printf("Link: preset command %u from %04x: %s\n", op, h.src, err.isEmpty() ? "ok" : err.c_str());
        valuesCheckNow = true;
      }
      std::vector<uint8_t> r;
      put<uint16_t>(r, h.src);
      put<uint16_t>(r, nonce);
      put<uint8_t>(r, err.isEmpty() ? 1 : 0);
      putStr(r, err, 120);
      queuePacket(PT_ACK, r);
      break;
    }
    case PT_ACK: {
      uint16_t target, nonce;
      uint8_t ok;
      String msg;
      if (!get(p, end, target) || target != boxId || !get(p, end, nonce) || !get(p, end, ok) || !getStr(p, end, msg)) return;
      pendingCmds.erase(std::remove_if(pendingCmds.begin(), pendingCmds.end(),
                                       [&](const PendingCmd& c) { return c.box == h.src && c.nonce == nonce; }),
                        pendingCmds.end());
      b.box.lastError = ok ? String("") : msg;
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
    if (c.attempts > 0 && now - c.lastSendMs < CMD_RETRY_MS) continue;
    BoxState* b = findBox(c.box);
    if (c.attempts >= CMD_MAX_ATTEMPTS || !b || !b->box.peer.online()) {
      if (b) b->box.lastError = "Box hat nicht geantwortet";
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

  if (!ownBuilt || fixturesVersion() != ownVersion) {
    rebuildOwn();
    beaconRequested = true;
  }
  if (beaconRequested || (int32_t)(now - nextBeaconMs) >= 0) {
    beaconRequested = false;
    nextBeaconMs = now + BEACON_INTERVAL_MS + random(0, 5000);
    buildBeacon();
  }
  if (pingRequested) {
    pingRequested = false;
    std::vector<uint8_t> b;
    put<uint32_t>(b, now);
    put<uint16_t>(b, 0xFFFF);
    queuePacket(PT_PING, b);
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
      if (all) refsSendPending = true;
    }
  }
  if (presetRefsVersion() != lastRefsVersion) {
    lastRefsVersion = presetRefsVersion();
    refsSendPending = true;
  }
  if (refsSendPending && anyPeerOnline()) {
    refsSendPending = false;
    sendRefs();
  }
  processPendingCmds(now);

  processPendingSets(now);
  processDescRetries(now);

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
