#pragma once
#ifdef HAS_LORA
#include <Arduino.h>
#include <vector>

struct PeerInfo {
  uint16_t id = 0;
  String name;
  uint32_t lastSeenMs = 0;
  float rssi = 0, snr = 0;            // how we hear them
  float theirRssi = 0, theirSnr = 0;  // how they heard our last ping
  int32_t pingRttMs = -1;
  uint16_t fixVersion = 0;
  uint8_t fixCount = 0;
  uint32_t uptimeS = 0;
  bool online() const { return lastSeenMs != 0 && millis() - lastSeenMs < 35000; }  // 3 missed beacons
};

struct RemotePreset {
  uint16_t id;
  String name;
  uint8_t group;
  bool stored;  // has values for this fixture
};

struct RemoteDef {
  uint16_t id;
  uint32_t crc;  // see defCrc(): equal CRC = identical definition
  uint16_t channels;
  String name;
};

struct RemoteFixture {
  uint16_t id = 0;
  uint16_t defId = 0;  // definition on that box, 0 = generic
  String name;
  uint16_t address = 1;
  uint16_t count = 0;
  std::vector<String> channelNames;
  std::vector<int16_t> fine;     // per channel: index of its fine channel, -1 = 8 bit
  std::vector<uint8_t> groups;   // per channel: feature group
  std::vector<RemotePreset> presets;
};

struct RemoteRef {
  uint16_t fixtureId;
  uint8_t group;
  uint16_t presetId;
};

enum PresetOp : uint8_t { PRESET_RECALL = 1, PRESET_STORE, PRESET_CREATE, PRESET_RENAME, PRESET_DELETE };

// Result of the last Save/Reset that went to all boxes.
struct SnapshotStatus {
  bool active = false;  // false: nothing in the last minute
  bool reset = false;
  int total = 0;        // boxes incl. this one
  int ok = 0;
  int pending = 0;
  std::vector<String> failed;
};

struct RemoteBox {
  PeerInfo peer;
  bool described = false;       // fixture list received completely
  bool loading = false;         // fixture list transfer in progress
  std::vector<RemoteFixture> fixtures;
  std::vector<RemoteDef> defs;
  uint8_t values[513] = {};     // last known DMX values (index = DMX address)
  bool haveValues = false;
  uint32_t valuesMs = 0;
  std::vector<std::pair<uint16_t, uint8_t>> pending;  // DMX address, value not yet confirmed
  std::vector<uint16_t> failed;                       // DMX addresses whose SET was never confirmed
  std::vector<RemoteRef> refs;                        // presets currently active on that box
  String lastError;                                   // last command error reported by that box
  uint32_t lastErrorMs = 0;
  uint16_t version = 0;                               // fixture list version (changes with presets too)
};

struct LinkSettings {
  String name;
  String password;
  bool enabled = true;
  int txPower = 14;
};

void syncBegin(const char* defaultName);
void syncLoop();
uint16_t syncBoxId();
LinkSettings syncSettings();
String syncSaveSettings(const LinkSettings& s);
std::vector<PeerInfo> syncPeers();
std::vector<RemoteBox> syncRemoteBoxes();
bool syncRemoteBox(uint16_t boxId, RemoteBox& out);
void syncSetRemote(uint16_t boxId, uint16_t dmxAddress, uint8_t value);
void syncPresetCommand(uint16_t boxId, uint8_t op, uint16_t fixtureId, uint8_t group, uint16_t presetId,
                       const String& name);
int syncOnlinePeers();

// Fixtures on other boxes. pushLocalDef >= 0: copy that local definition to the box first
// and use it; otherwise remoteDefId (0 = generic) is a definition that box already has.
void syncRemoteFixtureSave(uint16_t box, uint16_t fixtureId, const String& name, uint16_t address,
                           uint16_t remoteDefId, int pushLocalDef, uint16_t count);
void syncRemoteFixtureDelete(uint16_t box, uint16_t fixtureId);

// Save/Reset on this box and all online boxes.
// Commands (incl. definition transfers) to a box that are not confirmed yet; box 0 = all boxes.
int syncPendingFor(uint16_t box);

void syncSnapshotAll(bool reset);
SnapshotStatus syncSnapshotStatus();
void syncPing();
#endif
