#include "fixtures.h"
#include <LittleFS.h>
#include <algorithm>
#include <esp_rom_crc.h>

std::vector<FixtureDef> fixtureDefs;
std::vector<Fixture> fixtureList;

static uint16_t nextFixtureId = 1;
static uint16_t nextDefId = 1;
static uint16_t version = 0;
static SemaphoreHandle_t fixMutex = nullptr;

void fixturesLock() { xSemaphoreTakeRecursive(fixMutex, portMAX_DELAY); }
void fixturesUnlock() { xSemaphoreGiveRecursive(fixMutex); }
uint16_t fixturesVersion() { return version; }

struct FixLock {
  FixLock() { fixturesLock(); }
  ~FixLock() { fixturesUnlock(); }
};

static const char* PATCH_FILE = "/patch.json";
static const char* DEFS_DIR = "/defs";

static const char* TOMB_FILE = "/deftomb.json";
static const size_t TOMB_MAX = 64;
static std::vector<DefTombstone> tombstones;

static String defPath(uint16_t id) {
  return String(DEFS_DIR) + "/" + String(id) + ".json";
}

static String clip(String s, unsigned int maxLen) {
  s.trim();
  if (s.length() > maxLen) s = s.substring(0, maxLen);
  return s;
}

static bool writeJsonFile(const String& path, JsonDocument& doc) {
  String tmp = path + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0) return false;
  if (!LittleFS.rename(tmp, path)) {
    LittleFS.remove(path);
    if (!LittleFS.rename(tmp, path)) return false;
  }
  return true;
}

// ---------- JSON <-> model ----------

// ---------- feature groups ----------

const char* const GROUP_NAMES[GROUP_COUNT] = {"Dimmer", "Position", "Gobo", "Color", "Beam", "Focus", "Control"};

int8_t parseGroupName(const String& s) {
  String t = s;
  t.trim();
  for (uint8_t g = 0; g < GROUP_COUNT; g++) {
    if (t.equalsIgnoreCase(GROUP_NAMES[g])) return g;
  }
  if (t.equalsIgnoreCase("Farbe") || t.equalsIgnoreCase("Colour")) return GROUP_COLOR;
  return -1;
}

static bool containsAny(const String& s, const char* const* words) {
  for (; *words; words++) {
    if (s.indexOf(*words) >= 0) return true;
  }
  return false;
}

uint8_t guessGroup(const String& type, const String& name) {
  static const char* const control[] = {"speed", "time", "reset", "lamp", "macro", "makro", "function",
                                        "mode", "control", "special", nullptr};
  static const char* const position[] = {"pan", "tilt", "position", nullptr};
  static const char* const focus[] = {"focus", "zoom", nullptr};
  static const char* const dimmer[] = {"dimmer", "intensity", "master", nullptr};
  static const char* const color[] = {"color", "colour", "farbe", "red", "green", "blue", "white", "amber",
                                      "cyan", "magenta", "yellow", "cto", "ctb", "hue", "saturation", "rgb", nullptr};
  static const char* const gobo[] = {"gobo", "animation", "effect", nullptr};
  static const char* const beam[] = {"shutter", "strobe", "prism", "frost", "iris", "beam", nullptr};
  static const char* const* order[] = {control, position, focus, dimmer, color, gobo, beam};
  static const uint8_t groups[] = {GROUP_CONTROL, GROUP_POSITION, GROUP_FOCUS, GROUP_DIMMER,
                                   GROUP_COLOR, GROUP_GOBO, GROUP_BEAM};
  String t = type, n = name;
  t.toLowerCase();
  n.toLowerCase();
  for (const String& s : {t, n}) {
    if (s.isEmpty()) continue;
    for (size_t i = 0; i < sizeof(groups); i++) {
      if (containsAny(s, order[i])) return groups[i];
    }
  }
  return GROUP_CONTROL;
}

std::vector<uint8_t> groupsFor(const std::vector<String>& names, const std::vector<String>& types,
                               const std::vector<int8_t>& explicitGroups, const std::vector<int16_t>& fine) {
  size_t n = names.size();
  std::vector<uint8_t> g(n);
  for (size_t i = 0; i < n; i++) {
    int8_t e = i < explicitGroups.size() ? explicitGroups[i] : -1;
    g[i] = e >= 0 ? e : guessGroup(i < types.size() ? types[i] : String(""), names[i]);
  }
  for (size_t i = 0; i < n && i < fine.size(); i++) {
    if (fine[i] >= 0 && (size_t)fine[i] < n) g[fine[i]] = g[i];
  }
  return g;
}

// "Pan fine" / "Pan Fine" / "Pan_fine" / "PanFine" -> "Pan"; "" if not a fine-style name
static String fineBaseName(String name) {
  name.trim();
  if (name.length() <= 4) return "";
  String tail = name.substring(name.length() - 4);
  tail.toLowerCase();
  if (tail != "fine") return "";
  String base = name.substring(0, name.length() - 4);
  while (base.length() && (base.endsWith(" ") || base.endsWith("_") || base.endsWith("-"))) {
    base.remove(base.length() - 1);
  }
  return base;
}

void resolveFinePairs(std::vector<ChannelDef>& channels) {
  size_t n = channels.size();
  std::vector<bool> isFine(n, false);
  for (size_t i = 0; i < n; i++) {
    int16_t f = channels[i].fine;
    if (f < 0 || (size_t)f >= n || (size_t)f == i || isFine[f]) channels[i].fine = -1;
    else isFine[f] = true;
  }
  for (size_t i = 0; i < n; i++) {
    if (isFine[i] || channels[i].fine >= 0) continue;
    String type = channels[i].type;
    type.trim();
    String tl = type;
    tl.toLowerCase();
    String base;
    if (tl.startsWith("fine:")) {
      base = type.substring(5);
      base.trim();
    } else if (tl.isEmpty() || tl == "fine" || tl.endsWith("_fine") || tl.endsWith(" fine")) {
      base = fineBaseName(channels[i].name);
    }
    if (base.isEmpty()) continue;
    for (size_t j = 0; j < n; j++) {
      if (j == i || isFine[j] || channels[j].fine >= 0) continue;
      if (channels[j].name.equalsIgnoreCase(base)) {
        channels[j].fine = i;
        isFine[i] = true;
        break;
      }
    }
  }
}

std::vector<int16_t> finePairsFromNames(const std::vector<String>& names) {
  std::vector<ChannelDef> tmp(names.size());
  for (size_t i = 0; i < names.size(); i++) tmp[i].name = names[i];
  resolveFinePairs(tmp);
  std::vector<int16_t> fine(names.size());
  for (size_t i = 0; i < names.size(); i++) fine[i] = tmp[i].fine;
  return fine;
}

std::vector<FixtureControl> controlsFromFine(const std::vector<int16_t>& fine) {
  std::vector<bool> isFine(fine.size(), false);
  for (int16_t f : fine) if (f >= 0 && (size_t)f < fine.size()) isFine[f] = true;
  std::vector<FixtureControl> out;
  for (size_t i = 0; i < fine.size(); i++) {
    if (!isFine[i]) out.push_back({(uint16_t)i, fine[i]});
  }
  return out;
}

static void parseChannels(JsonArrayConst arr, std::vector<ChannelDef>& out) {
  struct Tmp {
    int index;
    int fineRef;
    ChannelDef ch;
  };
  std::vector<Tmp> tmp;
  bool allIndexed = true;
  for (JsonObjectConst c : arr) {
    ChannelDef ch;
    ch.name = clip(c["name"] | "", FIXTURE_NAME_MAX);
    ch.type = clip(c["type"] | "", FIXTURE_NAME_MAX);
    ch.group = parseGroupName(c["group"] | "");
    for (JsonObjectConst r : c["ranges"].as<JsonArrayConst>()) {
      int from = constrain((int)(r["from"] | 0), 0, 255);
      int to = constrain((int)(r["to"] | from), 0, 255);
      if (from > to) std::swap(from, to);
      ch.ranges.push_back({(uint8_t)from, (uint8_t)to, clip(r["name"] | "", FIXTURE_NAME_MAX)});
    }
    // labX palettes: single values
    for (JsonObjectConst p : c["palettes"].as<JsonArrayConst>()) {
      int v = constrain((int)(p["value"] | 0), 0, 255);
      ch.ranges.push_back({(uint8_t)v, (uint8_t)v, clip(p["name"] | "", FIXTURE_NAME_MAX)});
    }
    int index = c["index"] | 0;
    if (index <= 0) allIndexed = false;
    tmp.push_back({index, c["fine"] | 0, ch});
  }
  if (allIndexed) {
    std::stable_sort(tmp.begin(), tmp.end(), [](const Tmp& a, const Tmp& b) { return a.index < b.index; });
  }
  size_t base = out.size();
  for (size_t i = 0; i < tmp.size(); i++) {
    if (tmp[i].ch.name.isEmpty()) tmp[i].ch.name = "Ch " + String(i + 1);
    out.push_back(tmp[i].ch);
  }
  // "fine" refers to the 1-based "index" of the fine channel (labX / DmxBox export)
  for (size_t i = 0; i < tmp.size(); i++) {
    int ref = tmp[i].fineRef;
    if (ref <= 0) continue;
    for (size_t j = 0; j < tmp.size(); j++) {
      if (allIndexed ? tmp[j].index == ref : (int)j + 1 == ref) {
        out[base + i].fine = j;
        break;
      }
    }
  }
  resolveFinePairs(out);
}

void defToJson(const FixtureDef& d, JsonObject obj) {
  obj["name"] = d.name;
  obj["manufacturer"] = d.manufacturer;
  JsonObject mode = obj["modes"].to<JsonArray>().add<JsonObject>();
  mode["name"] = String(d.channels.size()) + "ch";
  JsonArray chans = mode["channels"].to<JsonArray>();
  for (size_t i = 0; i < d.channels.size(); i++) {
    const ChannelDef& ch = d.channels[i];
    JsonObject c = chans.add<JsonObject>();
    c["index"] = i + 1;
    c["name"] = ch.name;
    if (!ch.type.isEmpty()) c["type"] = ch.type;
    if (ch.fine >= 0) c["fine"] = ch.fine + 1;
    if (ch.group >= 0) {
      String g = GROUP_NAMES[ch.group];
      g.toLowerCase();
      c["group"] = g;
    }
    if (!ch.ranges.empty()) {
      JsonArray rs = c["ranges"].to<JsonArray>();
      for (const ChannelRange& r : ch.ranges) {
        JsonObject ro = rs.add<JsonObject>();
        ro["from"] = r.from;
        ro["to"] = r.to;
        ro["name"] = r.name;
      }
    }
  }
}

uint32_t defCrc(const FixtureDef& d) {
  JsonDocument doc;
  defToJson(d, doc.to<JsonObject>());
  String s;
  serializeJson(doc, s);
  return esp_rom_crc32_le(0, (const uint8_t*)s.c_str(), s.length());
}

FixtureDef* findDefByCrc(uint32_t crc) {
  for (FixtureDef& d : fixtureDefs) if (defCrc(d) == crc) return &d;
  return nullptr;
}

// ---------- load / save ----------

static bool writePatch() {
  JsonDocument doc;
  doc["nextId"] = nextFixtureId;
  JsonArray arr = doc["fixtures"].to<JsonArray>();
  for (const Fixture& f : fixtureList) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = f.id;
    o["name"] = f.name;
    o["address"] = f.address;
    o["def"] = f.defId;
    if (f.defId == 0) {
      o["count"] = f.channelCount;
      JsonArray names = o["names"].to<JsonArray>();
      for (const String& n : f.channelNames) names.add(n);
    }
  }
  return writeJsonFile(PATCH_FILE, doc);
}

static bool writeDef(const FixtureDef& d) {
  JsonDocument doc;
  defToJson(d, doc.to<JsonObject>());
  doc["uid"] = d.uid;
  doc["rev"] = d.rev;
  return writeJsonFile(defPath(d.id), doc);
}

static void writeTombstones() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const DefTombstone& t : tombstones) {
    JsonArray e = arr.add<JsonArray>();
    e.add(t.uid);
    e.add(t.rev);
  }
  writeJsonFile(TOMB_FILE, doc);
}

static void removeTombstone(uint32_t uid) {
  size_t before = tombstones.size();
  tombstones.erase(std::remove_if(tombstones.begin(), tombstones.end(),
                                  [uid](const DefTombstone& t) { return t.uid == uid; }),
                   tombstones.end());
  if (tombstones.size() != before) writeTombstones();
}

FixtureDef* findDefByUid(uint32_t uid) {
  for (FixtureDef& d : fixtureDefs) if (d.uid == uid) return &d;
  return nullptr;
}

std::vector<DefTombstone> defTombstones() {
  FixLock lock;
  return tombstones;
}

int defTombstoneRev(uint32_t uid) {
  FixLock lock;
  for (const DefTombstone& t : tombstones) if (t.uid == uid) return t.rev;
  return -1;
}

void defAddTombstone(uint32_t uid, uint16_t rev) {
  FixLock lock;
  for (DefTombstone& t : tombstones) {
    if (t.uid != uid) continue;
    if ((int16_t)(rev - t.rev) > 0) {
      t.rev = rev;
      writeTombstones();
      version++;
    }
    return;
  }
  tombstones.push_back({uid, rev});
  if (tombstones.size() > TOMB_MAX) tombstones.erase(tombstones.begin());
  writeTombstones();
  version++;
}

bool defFromJson(JsonVariantConst root, FixtureDef& d) {
  JsonObjectConst obj = root.as<JsonObjectConst>();
  if (obj.isNull()) return false;
  d.name = clip(obj["name"] | "", FIXTURE_NAME_MAX);
  if (d.name.isEmpty()) d.name = "Import";
  d.manufacturer = clip(obj["manufacturer"] | "", FIXTURE_NAME_MAX);
  JsonArrayConst chans = obj["modes"][0]["channels"].as<JsonArrayConst>();
  if (chans.isNull()) chans = obj["channels"].as<JsonArrayConst>();
  d.channels.clear();
  parseChannels(chans, d.channels);
  return !d.channels.empty();
}

bool fixturesBegin() {
  fixMutex = xSemaphoreCreateRecursiveMutex();
  version = esp_random() & 0xFFFF;
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return false;
  }
  if (!LittleFS.exists(DEFS_DIR)) LittleFS.mkdir(DEFS_DIR);
  Serial.printf("LittleFS mounted: %u / %u bytes used\n",
                (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  return true;
}

void fixturesLoad() {
  fixtureDefs.clear();
  fixtureList.clear();
  nextDefId = 1;

  File dir = LittleFS.open(DEFS_DIR);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String fname = f.name();
      int slash = fname.lastIndexOf('/');
      if (slash >= 0) fname = fname.substring(slash + 1);
      if (!fname.endsWith(".json")) continue;
      uint16_t id = fname.toInt();
      if (id == 0) continue;
      JsonDocument doc;
      if (deserializeJson(doc, f)) {
        Serial.printf("Bad definition file %s\n", fname.c_str());
        continue;
      }
      FixtureDef d;
      d.id = id;
      d.name = clip(doc["name"] | "", FIXTURE_NAME_MAX);
      d.manufacturer = clip(doc["manufacturer"] | "", FIXTURE_NAME_MAX);
      parseChannels(doc["modes"][0]["channels"].as<JsonArrayConst>(), d.channels);
      d.uid = doc["uid"] | 0;
      d.rev = doc["rev"] | 1;
      if (!d.uid) {
        // from before the link-wide definitions: identical content on different boxes gets the same uid
        d.uid = defCrc(d);
        if (findDefByUid(d.uid)) d.uid = esp_random() | 1;
        writeDef(d);
      }
      fixtureDefs.push_back(d);
      if (id >= nextDefId) nextDefId = id + 1;
    }
  }
  std::sort(fixtureDefs.begin(), fixtureDefs.end(),
            [](const FixtureDef& a, const FixtureDef& b) { return a.name < b.name; });
  tombstones.clear();
  File tf = LittleFS.exists(TOMB_FILE) ? LittleFS.open(TOMB_FILE, "r") : File();
  if (tf) {
    JsonDocument doc;
    if (!deserializeJson(doc, tf)) {
      for (JsonArrayConst e : doc.as<JsonArrayConst>()) tombstones.push_back({e[0] | 0u, (uint16_t)(e[1] | 0)});
    }
    tf.close();
  }

  File pf = LittleFS.open(PATCH_FILE, "r");
  if (pf) {
    JsonDocument doc;
    if (!deserializeJson(doc, pf)) {
      nextFixtureId = doc["nextId"] | 1;
      for (JsonObjectConst o : doc["fixtures"].as<JsonArrayConst>()) {
        Fixture f;
        f.id = o["id"] | 0;
        f.name = o["name"] | "";
        f.address = o["address"] | 1;
        f.defId = o["def"] | 0;
        f.channelCount = o["count"] | 1;
        for (JsonVariantConst n : o["names"].as<JsonArrayConst>()) f.channelNames.push_back(n.as<String>());
        if (f.id == 0) continue;
        if (f.defId != 0 && !findDef(f.defId)) {
          Serial.printf("Fixture %s: definition %u missing, using generic\n", f.name.c_str(), f.defId);
          f.defId = 0;
        }
        if (f.id >= nextFixtureId) nextFixtureId = f.id + 1;
        fixtureList.push_back(f);
      }
    } else {
      Serial.println("Bad patch file, starting empty");
    }
    pf.close();
  }
  std::sort(fixtureList.begin(), fixtureList.end(),
            [](const Fixture& a, const Fixture& b) { return a.address < b.address; });
  Serial.printf("Fixtures: %u, definitions: %u\n", (unsigned)fixtureList.size(), (unsigned)fixtureDefs.size());
}

// ---------- queries ----------

FixtureDef* findDef(uint16_t id) {
  for (FixtureDef& d : fixtureDefs) if (d.id == id) return &d;
  return nullptr;
}

Fixture* findFixture(uint16_t id) {
  for (Fixture& f : fixtureList) if (f.id == id) return &f;
  return nullptr;
}

uint16_t fixtureChannelCount(const Fixture& f) {
  if (f.defId != 0) {
    const FixtureDef* d = findDef(f.defId);
    return d ? d->channels.size() : 0;
  }
  return f.channelCount;
}

String fixtureChannelName(const Fixture& f, uint16_t ch) {
  if (f.defId != 0) {
    const ChannelDef* c = fixtureChannelDef(f, ch);
    if (c && !c->name.isEmpty()) return c->name;
  } else if (ch < f.channelNames.size() && !f.channelNames[ch].isEmpty()) {
    return f.channelNames[ch];
  }
  return "Ch " + String(ch + 1);
}

const ChannelDef* fixtureChannelDef(const Fixture& f, uint16_t ch) {
  if (f.defId == 0) return nullptr;
  const FixtureDef* d = findDef(f.defId);
  if (!d || ch >= d->channels.size()) return nullptr;
  return &d->channels[ch];
}

std::vector<int16_t> fixtureFinePairs(const Fixture& f) {
  if (f.defId != 0) {
    const FixtureDef* d = findDef(f.defId);
    std::vector<int16_t> fine;
    if (d) for (const ChannelDef& c : d->channels) fine.push_back(c.fine);
    return fine;
  }
  std::vector<String> names;
  for (uint16_t i = 0; i < f.channelCount; i++) names.push_back(fixtureChannelName(f, i));
  return finePairsFromNames(names);
}

std::vector<FixtureControl> fixtureControls(const Fixture& f) {
  return controlsFromFine(fixtureFinePairs(f));
}

std::vector<uint8_t> fixtureGroups(const Fixture& f) {
  std::vector<String> names, types;
  std::vector<int8_t> explicitGroups;
  if (f.defId != 0) {
    const FixtureDef* d = findDef(f.defId);
    if (d) {
      for (const ChannelDef& c : d->channels) {
        names.push_back(c.name);
        types.push_back(c.type);
        explicitGroups.push_back(c.group);
      }
    }
  } else {
    for (uint16_t i = 0; i < f.channelCount; i++) names.push_back(fixtureChannelName(f, i));
  }
  return groupsFor(names, types, explicitGroups, fixtureFinePairs(f));
}

void fixturesBumpVersion() {
  FixLock lock;
  version++;
}

bool fixtureOverlaps(const Fixture& f) {
  int a1 = f.address, a2 = f.address + fixtureChannelCount(f) - 1;
  for (const Fixture& g : fixtureList) {
    if (g.id == f.id) continue;
    int b1 = g.address, b2 = g.address + fixtureChannelCount(g) - 1;
    if (a1 <= b2 && b1 <= a2) return true;
  }
  return false;
}

// ---------- mutations ----------

String saveFixture(Fixture& f) {
  FixLock lock;
  version++;
  f.name = clip(f.name, FIXTURE_NAME_MAX);
  if (f.defId != 0 && !findDef(f.defId)) return "Definition not found";
  if (f.defId == 0 && (f.channelCount < 1 || f.channelCount > DMX_UNIVERSE_SIZE)) {
    return "Channel count must be between 1 and 512";
  }
  uint16_t count = fixtureChannelCount(f);
  if (count == 0) return "The definition has no channels";
  if (f.address < 1 || f.address > DMX_UNIVERSE_SIZE) return "Address must be between 1 and 512";
  if (f.address + count - 1 > DMX_UNIVERSE_SIZE) {
    return "Address " + String(f.address) + " + " + String(count) +
           " channels does not fit into the universe (last channel would be " + String(f.address + count - 1) + ")";
  }
  if (f.defId == 0) {
    if (f.channelNames.size() > f.channelCount) f.channelNames.resize(f.channelCount);
  } else {
    f.channelNames.clear();
  }

  if (f.id == 0) {
    f.id = nextFixtureId++;
    if (f.name.isEmpty()) f.name = "Fixture " + String(f.id);
    fixtureList.push_back(f);
  } else {
    Fixture* existing = findFixture(f.id);
    if (!existing) return "Fixture not found";
    if (f.name.isEmpty()) f.name = "Fixture " + String(f.id);
    *existing = f;
  }
  std::sort(fixtureList.begin(), fixtureList.end(),
            [](const Fixture& a, const Fixture& b) { return a.address < b.address; });
  if (!writePatch()) return "Saving failed";
  return "";
}

String deleteFixture(uint16_t id) {
  FixLock lock;
  version++;
  auto it = std::find_if(fixtureList.begin(), fixtureList.end(), [id](const Fixture& f) { return f.id == id; });
  if (it == fixtureList.end()) return "Fixture not found";
  fixtureList.erase(it);
  if (!writePatch()) return "Saving failed";
  return "";
}

String saveDef(FixtureDef& d, bool fromLink) {
  FixLock lock;
  version++;
  d.name = clip(d.name, FIXTURE_NAME_MAX);
  d.manufacturer = clip(d.manufacturer, FIXTURE_NAME_MAX);
  if (d.name.isEmpty()) return "Name missing";
  if (d.channels.empty()) return "At least one channel required";
  if (d.channels.size() > DMX_UNIVERSE_SIZE) return "At most 512 channels";

  if (d.id != 0) {
    FixtureDef* old = findDef(d.id);
    if (!old) return "Definition not found";
    if (!fromLink) {
      d.uid = old->uid;
      d.rev = old->rev + 1;
    }
    for (const Fixture& f : fixtureList) {
      if (f.defId == d.id && f.address + d.channels.size() - 1 > DMX_UNIVERSE_SIZE) {
        return "Fixture '" + f.name + "' (address " + String(f.address) + ") would not fit with " +
               String(d.channels.size()) + " channels";
      }
    }
  } else {
    if (!fromLink) {
      // identical content imported on several boxes ends up as one definition
      d.uid = defCrc(d);
      if (findDefByUid(d.uid) || defTombstoneRev(d.uid) >= 0) d.uid = esp_random() | 1;
      d.rev = 1;
    }
    d.id = nextDefId++;
  }
  if (!writeDef(d)) return "Saving failed";
  removeTombstone(d.uid);

  FixtureDef* existing = findDef(d.id);
  if (existing) *existing = d;
  else fixtureDefs.push_back(d);
  std::sort(fixtureDefs.begin(), fixtureDefs.end(),
            [](const FixtureDef& a, const FixtureDef& b) { return a.name < b.name; });
  return "";
}

String deleteDef(uint16_t id, uint16_t tombRev) {
  FixLock lock;
  version++;
  FixtureDef* d = findDef(id);
  if (!d) return "Definition not found";
  uint32_t uid = d->uid;
  uint16_t rev = tombRev ? tombRev : d->rev + 1;
  String users;
  for (const Fixture& f : fixtureList) {
    if (f.defId == id) users += (users.isEmpty() ? "" : ", ") + f.name;
  }
  if (!users.isEmpty()) return "Still used by: " + users;
  LittleFS.remove(defPath(id));
  fixtureDefs.erase(std::remove_if(fixtureDefs.begin(), fixtureDefs.end(),
                                   [id](const FixtureDef& d) { return d.id == id; }),
                    fixtureDefs.end());
  defAddTombstone(uid, rev);
  return "";
}

// ---------- import ----------

static int importOne(JsonObjectConst obj, String& err) {
  String name = clip(obj["name"] | "", FIXTURE_NAME_MAX);
  if (name.isEmpty()) name = "Import";
  String manufacturer = obj["manufacturer"] | "";

  std::vector<std::pair<String, JsonArrayConst>> modes;
  JsonArrayConst modeArr = obj["modes"].as<JsonArrayConst>();
  if (!modeArr.isNull() && modeArr.size() > 0) {
    for (JsonObjectConst m : modeArr) {
      String modeName = m["name"] | "";
      String defName = modeArr.size() > 1 ? name + " (" + modeName + ")" : name;
      modes.push_back({defName, m["channels"].as<JsonArrayConst>()});
    }
  } else if (obj["channels"].is<JsonArrayConst>()) {
    modes.push_back({name, obj["channels"].as<JsonArrayConst>()});
  }
  if (modes.empty()) {
    err = "'" + name + "': no channels found (expected modes[].channels[] or channels[])";
    return 0;
  }

  int created = 0;
  for (auto& m : modes) {
    FixtureDef d;
    d.name = m.first;
    d.manufacturer = manufacturer;
    parseChannels(m.second, d.channels);
    String e = saveDef(d);
    if (!e.isEmpty()) {
      err = "'" + d.name + "': " + e;
      continue;
    }
    created++;
  }
  return created;
}

uint16_t importSingleDef(JsonVariantConst root, String& err) {
  JsonObjectConst obj = root.as<JsonObjectConst>();
  if (obj.isNull()) {
    err = "JSON must be an object";
    return 0;
  }
  FixtureDef d;
  d.name = clip(obj["name"] | "", FIXTURE_NAME_MAX);
  if (d.name.isEmpty()) d.name = "Import";
  d.manufacturer = clip(obj["manufacturer"] | "", FIXTURE_NAME_MAX);
  JsonArrayConst chans = obj["modes"][0]["channels"].as<JsonArrayConst>();
  if (chans.isNull()) chans = obj["channels"].as<JsonArrayConst>();
  parseChannels(chans, d.channels);
  err = saveDef(d);
  return err.isEmpty() ? d.id : 0;
}

int importDefs(JsonVariantConst root, String& err) {
  int created = 0;
  if (root.is<JsonArrayConst>()) {
    for (JsonObjectConst o : root.as<JsonArrayConst>()) created += importOne(o, err);
  } else if (root.is<JsonObjectConst>()) {
    created = importOne(root.as<JsonObjectConst>(), err);
  } else {
    err = "JSON must be an object or an array";
  }
  return created;
}

// ---------- text format ----------

static String sanitizeText(String s) {
  s.replace("|", "/");
  s.replace(";", ",");
  s.replace("\n", " ");
  s.replace("\r", "");
  return s;
}

String defChannelsToText(const FixtureDef& d) {
  String out;
  for (size_t idx = 0; idx < d.channels.size(); idx++) {
    const ChannelDef& ch = d.channels[idx];
    String type = ch.type;
    if (ch.group >= 0 && ch.group != guessGroup(ch.type, ch.name)) {
      type = String(GROUP_NAMES[ch.group]) + (ch.type.isEmpty() ? "" : "/" + ch.type);
    }
    for (const ChannelDef& coarse : d.channels) {
      if (coarse.fine == (int16_t)idx) type = "fine:" + coarse.name;  // keeps the pairing explicit
    }
    out += sanitizeText(ch.name);
    if (!type.isEmpty() || !ch.ranges.empty()) out += " | " + sanitizeText(type);
    if (!ch.ranges.empty()) {
      out += " | ";
      for (size_t i = 0; i < ch.ranges.size(); i++) {
        const ChannelRange& r = ch.ranges[i];
        if (i) out += "; ";
        out += String(r.from);
        if (r.to != r.from) out += "-" + String(r.to);
        if (!r.name.isEmpty()) out += " " + sanitizeText(r.name);
      }
    }
    out += "\n";
  }
  return out;
}

// Reads a number 0-255 at pos, advancing pos. Returns false if none/out of range.
static bool readByte(const String& s, int& pos, uint8_t& out) {
  int start = pos;
  long v = 0;
  while (pos < (int)s.length() && isDigit(s[pos])) {
    v = v * 10 + (s[pos] - '0');
    if (v > 255) return false;
    pos++;
  }
  if (pos == start) return false;
  out = (uint8_t)v;
  return true;
}

static void skipSpaces(const String& s, int& pos) {
  while (pos < (int)s.length() && s[pos] == ' ') pos++;
}

// "10-19 Red" or "20 Blue" or "0-9"
static String parseRange(const String& tok, ChannelRange& r) {
  int pos = 0;
  if (!readByte(tok, pos, r.from)) return "invalid range '" + tok + "'";
  r.to = r.from;
  skipSpaces(tok, pos);
  if (pos < (int)tok.length() && tok[pos] == '-') {
    pos++;
    skipSpaces(tok, pos);
    if (!readByte(tok, pos, r.to)) return "invalid range '" + tok + "'";
    if (r.from > r.to) std::swap(r.from, r.to);
  }
  r.name = clip(tok.substring(pos), FIXTURE_NAME_MAX);
  return "";
}

static std::vector<String> split(const String& s, char sep) {
  std::vector<String> parts;
  int start = 0;
  while (true) {
    int end = s.indexOf(sep, start);
    if (end < 0) {
      parts.push_back(s.substring(start));
      return parts;
    }
    parts.push_back(s.substring(start, end));
    start = end + 1;
  }
}

String defChannelsFromText(const String& text, FixtureDef& d) {
  std::vector<ChannelDef> chans;
  std::vector<String> lines = split(text, '\n');
  for (size_t lineNo = 0; lineNo < lines.size(); lineNo++) {
    String line = lines[lineNo];
    line.trim();
    if (line.isEmpty()) continue;
    std::vector<String> parts = split(line, '|');
    ChannelDef ch;
    ch.name = clip(parts[0], FIXTURE_NAME_MAX);
    if (parts.size() > 1) {
      ch.type = clip(parts[1], FIXTURE_NAME_MAX);
      // "Color/wheel" or just "Color" sets the group explicitly
      int slash = ch.type.indexOf('/');
      int8_t g = parseGroupName(slash >= 0 ? ch.type.substring(0, slash) : ch.type);
      if (g >= 0) {
        ch.group = g;
        ch.type = slash >= 0 ? ch.type.substring(slash + 1) : String("");
        ch.type.trim();
      }
    }
    if (parts.size() > 2) {
      for (String tok : split(parts[2], ';')) {
        tok.trim();
        if (tok.isEmpty()) continue;
        ChannelRange r;
        String e = parseRange(tok, r);
        if (!e.isEmpty()) return "Line " + String(lineNo + 1) + ": " + e;
        ch.ranges.push_back(r);
      }
    }
    if (ch.name.isEmpty()) ch.name = "Ch " + String(chans.size() + 1);
    chans.push_back(ch);
  }
  resolveFinePairs(chans);
  d.channels = chans;
  return "";
}
