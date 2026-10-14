#include "fixtures.h"
#include <LittleFS.h>
#include <algorithm>

std::vector<FixtureDef> fixtureDefs;
std::vector<Fixture> fixtureList;

static uint16_t nextFixtureId = 1;
static uint16_t nextDefId = 1;

static const char* PATCH_FILE = "/patch.json";
static const char* DEFS_DIR = "/defs";

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

static void parseChannels(JsonArrayConst arr, std::vector<ChannelDef>& out) {
  std::vector<std::pair<int, ChannelDef>> tmp;
  bool allIndexed = true;
  for (JsonObjectConst c : arr) {
    ChannelDef ch;
    ch.name = clip(c["name"] | "", FIXTURE_NAME_MAX);
    ch.type = clip(c["type"] | "", FIXTURE_NAME_MAX);
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
    tmp.push_back({index, ch});
  }
  if (allIndexed) {
    std::stable_sort(tmp.begin(), tmp.end(),
                     [](const std::pair<int, ChannelDef>& a, const std::pair<int, ChannelDef>& b) {
                       return a.first < b.first;
                     });
  }
  for (size_t i = 0; i < tmp.size(); i++) {
    if (tmp[i].second.name.isEmpty()) tmp[i].second.name = "Ch " + String(i + 1);
    out.push_back(tmp[i].second);
  }
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
  return writeJsonFile(defPath(d.id), doc);
}

bool fixturesBegin() {
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
      fixtureDefs.push_back(d);
      if (id >= nextDefId) nextDefId = id + 1;
    }
  }
  std::sort(fixtureDefs.begin(), fixtureDefs.end(),
            [](const FixtureDef& a, const FixtureDef& b) { return a.name < b.name; });

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
  f.name = clip(f.name, FIXTURE_NAME_MAX);
  if (f.defId != 0 && !findDef(f.defId)) return "Definition nicht gefunden";
  if (f.defId == 0 && (f.channelCount < 1 || f.channelCount > DMX_UNIVERSE_SIZE)) {
    return "Kanalanzahl muss zwischen 1 und 512 liegen";
  }
  uint16_t count = fixtureChannelCount(f);
  if (count == 0) return "Die Definition hat keine Kanäle";
  if (f.address < 1 || f.address > DMX_UNIVERSE_SIZE) return "Adresse muss zwischen 1 und 512 liegen";
  if (f.address + count - 1 > DMX_UNIVERSE_SIZE) {
    return "Adresse " + String(f.address) + " + " + String(count) +
           " Kanäle passt nicht ins Universum (letzter Kanal wäre " + String(f.address + count - 1) + ")";
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
    if (!existing) return "Fixture nicht gefunden";
    if (f.name.isEmpty()) f.name = "Fixture " + String(f.id);
    *existing = f;
  }
  std::sort(fixtureList.begin(), fixtureList.end(),
            [](const Fixture& a, const Fixture& b) { return a.address < b.address; });
  if (!writePatch()) return "Speichern fehlgeschlagen";
  return "";
}

String deleteFixture(uint16_t id) {
  auto it = std::find_if(fixtureList.begin(), fixtureList.end(), [id](const Fixture& f) { return f.id == id; });
  if (it == fixtureList.end()) return "Fixture nicht gefunden";
  fixtureList.erase(it);
  if (!writePatch()) return "Speichern fehlgeschlagen";
  return "";
}

String saveDef(FixtureDef& d) {
  d.name = clip(d.name, FIXTURE_NAME_MAX);
  d.manufacturer = clip(d.manufacturer, FIXTURE_NAME_MAX);
  if (d.name.isEmpty()) return "Name fehlt";
  if (d.channels.empty()) return "Mindestens ein Kanal nötig";
  if (d.channels.size() > DMX_UNIVERSE_SIZE) return "Maximal 512 Kanäle";

  if (d.id != 0) {
    if (!findDef(d.id)) return "Definition nicht gefunden";
    for (const Fixture& f : fixtureList) {
      if (f.defId == d.id && f.address + d.channels.size() - 1 > DMX_UNIVERSE_SIZE) {
        return "Fixture '" + f.name + "' (Adresse " + String(f.address) + ") hätte mit " +
               String(d.channels.size()) + " Kanälen keinen Platz mehr";
      }
    }
  } else {
    d.id = nextDefId++;
  }
  if (!writeDef(d)) return "Speichern fehlgeschlagen";

  FixtureDef* existing = findDef(d.id);
  if (existing) *existing = d;
  else fixtureDefs.push_back(d);
  std::sort(fixtureDefs.begin(), fixtureDefs.end(),
            [](const FixtureDef& a, const FixtureDef& b) { return a.name < b.name; });
  return "";
}

String deleteDef(uint16_t id) {
  if (!findDef(id)) return "Definition nicht gefunden";
  String users;
  for (const Fixture& f : fixtureList) {
    if (f.defId == id) users += (users.isEmpty() ? "" : ", ") + f.name;
  }
  if (!users.isEmpty()) return "Wird noch verwendet von: " + users;
  LittleFS.remove(defPath(id));
  fixtureDefs.erase(std::remove_if(fixtureDefs.begin(), fixtureDefs.end(),
                                   [id](const FixtureDef& d) { return d.id == id; }),
                    fixtureDefs.end());
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
    err = "'" + name + "': keine Kanäle gefunden (erwartet: modes[].channels[] oder channels[])";
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

int importDefs(JsonVariantConst root, String& err) {
  int created = 0;
  if (root.is<JsonArrayConst>()) {
    for (JsonObjectConst o : root.as<JsonArrayConst>()) created += importOne(o, err);
  } else if (root.is<JsonObjectConst>()) {
    created = importOne(root.as<JsonObjectConst>(), err);
  } else {
    err = "JSON muss ein Objekt oder Array sein";
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
  for (const ChannelDef& ch : d.channels) {
    out += sanitizeText(ch.name);
    if (!ch.type.isEmpty() || !ch.ranges.empty()) out += " | " + sanitizeText(ch.type);
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
  if (!readByte(tok, pos, r.from)) return "ungültiger Bereich '" + tok + "'";
  r.to = r.from;
  skipSpaces(tok, pos);
  if (pos < (int)tok.length() && tok[pos] == '-') {
    pos++;
    skipSpaces(tok, pos);
    if (!readByte(tok, pos, r.to)) return "ungültiger Bereich '" + tok + "'";
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
    if (parts.size() > 1) ch.type = clip(parts[1], FIXTURE_NAME_MAX);
    if (parts.size() > 2) {
      for (String tok : split(parts[2], ';')) {
        tok.trim();
        if (tok.isEmpty()) continue;
        ChannelRange r;
        String e = parseRange(tok, r);
        if (!e.isEmpty()) return "Zeile " + String(lineNo + 1) + ": " + e;
        ch.ranges.push_back(r);
      }
    }
    if (ch.name.isEmpty()) ch.name = "Ch " + String(chans.size() + 1);
    chans.push_back(ch);
  }
  d.channels = chans;
  return "";
}
