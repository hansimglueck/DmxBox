#include "presets.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <algorithm>

extern uint8_t data[];

static const char* PRESETS_FILE = "/presets.json";
static const char* REFS_FILE = "/presetrefs.json";

static std::vector<Preset> presets;
static std::vector<PresetRef> refs;
static uint16_t nextPresetId = 1;
static uint32_t refsVersion = 0;

struct Guard {
  Guard() { fixturesLock(); }
  ~Guard() { fixturesUnlock(); }
};

// ---------- persistence ----------

static bool writeFile(const char* path, JsonDocument& doc) {
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) return false;
  if (!LittleFS.rename(tmp, path)) {
    LittleFS.remove(path);
    if (!LittleFS.rename(tmp, path)) return false;
  }
  return true;
}

static bool savePresets() {
  JsonDocument doc;
  doc["nextId"] = nextPresetId;
  JsonArray arr = doc["presets"].to<JsonArray>();
  for (const Preset& p : presets) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = p.id;
    o["name"] = p.name;
    o["group"] = p.group;
    JsonObject vals = o["values"].to<JsonObject>();
    for (const auto& kv : p.values) {
      JsonArray list = vals[kv.first].to<JsonArray>();
      for (const auto& cv : kv.second) {
        JsonArray pair = list.add<JsonArray>();
        pair.add(cv.first);
        pair.add(cv.second);
      }
    }
  }
  fixturesBumpVersion();  // the link re-announces fixture descriptions incl. presets
  return writeFile(PRESETS_FILE, doc);
}

static void saveRefs() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const PresetRef& r : refs) {
    JsonArray e = arr.add<JsonArray>();
    e.add(r.fixtureId);
    e.add(r.group);
    e.add(r.presetId);
  }
  refsVersion++;
  writeFile(REFS_FILE, doc);
}

void presetsLoad() {
  Guard g;
  presets.clear();
  refs.clear();
  nextPresetId = 1;
  File f = LittleFS.open(PRESETS_FILE, "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
      nextPresetId = doc["nextId"] | 1;
      for (JsonObjectConst o : doc["presets"].as<JsonArrayConst>()) {
        Preset p;
        p.id = o["id"] | 0;
        p.name = o["name"] | "";
        p.group = o["group"] | (int)GROUP_COLOR;
        if (p.id == 0 || p.group >= GROUP_COUNT) continue;
        for (JsonPairConst kv : o["values"].as<JsonObjectConst>()) {
          auto& list = p.values[kv.key().c_str()];
          for (JsonArrayConst pair : kv.value().as<JsonArrayConst>()) {
            list.push_back({pair[0] | 0, pair[1] | 0});
          }
        }
        if (p.id >= nextPresetId) nextPresetId = p.id + 1;
        presets.push_back(p);
      }
    }
    f.close();
  }
  File rf = LittleFS.open(REFS_FILE, "r");
  if (rf) {
    JsonDocument doc;
    if (!deserializeJson(doc, rf)) {
      for (JsonArrayConst e : doc.as<JsonArrayConst>()) {
        refs.push_back({e[0] | 0, e[1] | 0, e[2] | 0});
      }
    }
    rf.close();
  }
  Serial.printf("Presets: %u, active references: %u\n", (unsigned)presets.size(), (unsigned)refs.size());
}

// ---------- queries ----------

static Preset* findPreset(uint16_t id) {
  for (Preset& p : presets) if (p.id == id) return &p;
  return nullptr;
}

String presetKey(const Fixture& f, uint8_t group) {
  if (groupIsSelective(group) || f.defId == 0) return "f" + String(f.id);
  return "d" + String(f.defId);
}

std::vector<PresetInfo> presetsFor(const Fixture& f, uint8_t group) {
  Guard g;
  std::vector<PresetInfo> out;
  String key = presetKey(f, group);
  for (const Preset& p : presets) {
    if (p.group != group) continue;
    out.push_back({p.id, p.name, p.group, p.values.count(key) > 0});
  }
  return out;
}

int presetActive(uint16_t fixtureId, uint8_t group) {
  Guard g;
  for (const PresetRef& r : refs) {
    if (r.fixtureId == fixtureId && r.group == group) return r.presetId;
  }
  return -1;
}

std::vector<PresetRef> presetRefs() {
  Guard g;
  return refs;
}

uint32_t presetRefsVersion() { return refsVersion; }

// ---------- helpers (lock held) ----------

static void setRef(uint16_t fixtureId, uint8_t group, uint16_t presetId) {
  for (PresetRef& r : refs) {
    if (r.fixtureId == fixtureId && r.group == group) {
      if (r.presetId == presetId) return;
      r.presetId = presetId;
      saveRefs();
      return;
    }
  }
  refs.push_back({fixtureId, group, presetId});
  saveRefs();
}

static void applyValues(const Fixture& f, const std::vector<std::pair<uint16_t, uint8_t>>& vals) {
  uint16_t n = fixtureChannelCount(f);
  portENTER_CRITICAL(&dmxDataMux);
  for (const auto& cv : vals) {
    if (cv.first >= n) continue;
    uint16_t dmx = f.address + cv.first;
    if (dmx >= 1 && dmx <= DMX_UNIVERSE_SIZE) data[dmx] = cv.second;
  }
  portEXIT_CRITICAL(&dmxDataMux);
}

static std::vector<std::pair<uint16_t, uint8_t>> captureValues(const Fixture& f, uint8_t group) {
  std::vector<std::pair<uint16_t, uint8_t>> vals;
  std::vector<uint8_t> groups = fixtureGroups(f);
  for (uint16_t i = 0; i < groups.size(); i++) {
    uint16_t dmx = f.address + i;
    if (groups[i] == group && dmx <= DMX_UNIVERSE_SIZE) vals.push_back({i, data[dmx]});
  }
  return vals;
}

// ---------- operations ----------

String presetCreate(uint16_t fixtureId, uint8_t group, const String& name) {
  Guard g;
  Fixture* f = findFixture(fixtureId);
  if (!f) return "Fixture nicht gefunden";
  if (group >= GROUP_COUNT) return "Ungültige Gruppe";
  auto vals = captureValues(*f, group);
  if (vals.empty()) return "Das Fixture hat keine Kanäle in dieser Gruppe";
  Preset p;
  p.id = nextPresetId++;
  p.name = name;
  p.name.trim();
  if (p.name.isEmpty()) p.name = String(GROUP_NAMES[group]) + " " + String(p.id);
  if (p.name.length() > FIXTURE_NAME_MAX) p.name = p.name.substring(0, FIXTURE_NAME_MAX);
  p.group = group;
  p.values[presetKey(*f, group)] = vals;
  presets.push_back(p);
  if (!savePresets()) return "Speichern fehlgeschlagen";
  setRef(f->id, group, p.id);
  return "";
}

String presetStore(uint16_t fixtureId, uint16_t presetId) {
  Guard g;
  Fixture* f = findFixture(fixtureId);
  Preset* p = findPreset(presetId);
  if (!f || !p) return "Fixture oder Preset nicht gefunden";
  auto vals = captureValues(*f, p->group);
  if (vals.empty()) return "Das Fixture hat keine Kanäle in dieser Gruppe";
  String key = presetKey(*f, p->group);
  p->values[key] = vals;
  if (!savePresets()) return "Speichern fehlgeschlagen";
  setRef(f->id, p->group, p->id);
  // live update of every fixture that follows this preset with the same key
  for (const PresetRef& r : refs) {
    if (r.presetId != p->id || r.fixtureId == f->id) continue;
    Fixture* other = findFixture(r.fixtureId);
    if (other && presetKey(*other, p->group) == key) applyValues(*other, vals);
  }
  return "";
}

String presetRecall(uint16_t fixtureId, uint16_t presetId) {
  Guard g;
  Fixture* f = findFixture(fixtureId);
  Preset* p = findPreset(presetId);
  if (!f || !p) return "Fixture oder Preset nicht gefunden";
  auto it = p->values.find(presetKey(*f, p->group));
  if (it == p->values.end()) return "Preset '" + p->name + "' ist für dieses Fixture nicht gespeichert";
  applyValues(*f, it->second);
  setRef(f->id, p->group, p->id);
  return "";
}

String presetRename(uint16_t presetId, const String& name) {
  Guard g;
  Preset* p = findPreset(presetId);
  if (!p) return "Preset nicht gefunden";
  String n = name;
  n.trim();
  if (n.isEmpty()) return "Name fehlt";
  p->name = n.substring(0, FIXTURE_NAME_MAX);
  return savePresets() ? "" : "Speichern fehlgeschlagen";
}

String presetDelete(uint16_t presetId) {
  Guard g;
  auto it = std::find_if(presets.begin(), presets.end(), [presetId](const Preset& p) { return p.id == presetId; });
  if (it == presets.end()) return "Preset nicht gefunden";
  presets.erase(it);
  size_t before = refs.size();
  refs.erase(std::remove_if(refs.begin(), refs.end(), [presetId](const PresetRef& r) { return r.presetId == presetId; }),
             refs.end());
  if (refs.size() != before) saveRefs();
  return savePresets() ? "" : "Speichern fehlgeschlagen";
}

void presetChannelsChanged(uint16_t dmxFrom, uint16_t dmxTo) {
  Guard g;
  if (refs.empty()) return;
  bool changed = false;
  for (const Fixture& f : fixtureList) {
    uint16_t n = fixtureChannelCount(f);
    if (n == 0 || f.address > dmxTo || f.address + n - 1 < dmxFrom) continue;
    std::vector<uint8_t> groups = fixtureGroups(f);
    for (uint16_t dmx = max(dmxFrom, f.address); dmx <= dmxTo && dmx < f.address + n; dmx++) {
      uint8_t grp = groups[dmx - f.address];
      size_t before = refs.size();
      refs.erase(std::remove_if(refs.begin(), refs.end(),
                                [&](const PresetRef& r) { return r.fixtureId == f.id && r.group == grp; }),
                 refs.end());
      if (refs.size() != before) changed = true;
    }
  }
  if (changed) saveRefs();
}

void presetsFixtureDeleted(uint16_t fixtureId) {
  Guard g;
  size_t before = refs.size();
  refs.erase(std::remove_if(refs.begin(), refs.end(), [fixtureId](const PresetRef& r) { return r.fixtureId == fixtureId; }),
             refs.end());
  if (refs.size() != before) saveRefs();
  String key = "f" + String(fixtureId);
  bool changed = false;
  for (Preset& p : presets) changed |= p.values.erase(key) > 0;
  if (changed) savePresets();
}
