#include "cues.h"
#include "fixtures.h"
#include "presets.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_rom_crc.h>
#include <algorithm>
#include <map>

extern uint8_t data[];

static const char* SHOW_FILE = "/show.json";
static const char* PLAY_FILE = "/play.json";
static const char* CUE_DIR = "/cues";
static const size_t SEQ_NAME_MAX = 32;
static const uint16_t TIME_MAX = 60000;  // 0.1 s units: 100 min

struct CueData {
  std::vector<std::pair<uint16_t, uint8_t>> v;  // DMX address, value (sorted by address)
  std::vector<PresetRef> r;                     // preset references of groups in v
};

struct Show {
  uint16_t ver = 0;
  uint16_t startup = 0;  // 0 = last active
  uint16_t nextSeq = 1;  // sequence ids are never reused
  std::vector<SeqMeta> seqs;
};

// The executor. Prepared to become one of several playbacks later.
struct Playback {
  uint16_t seq = 0;
  uint16_t cue = 0;  // current cue id (may be one whose metadata has not arrived yet)
  uint8_t mode = CUE_HOLD;
  uint32_t cueStartMs = 0;
  uint32_t fadeMs = 0;
  bool fading = false;
  uint8_t from[513] = {};
  uint8_t to[513] = {};
  std::map<uint16_t, CueData> parts;  // this box's channels of the active sequence's cues
};

static Show show;
static uint16_t seenVer = 0;  // highest show version announced by any box
static String blobCache;
static bool blobValid = false;
static uint16_t lastSeq = 0;
static Playback pb;
static SemaphoreHandle_t cm = nullptr;

static uint8_t marks[64];
static volatile uint32_t marksVer = 0;
static volatile bool touched = false;
static bool holdEvent = false;
static uint16_t epoch = 0;      // playback state counter, see CueTransport
static uint16_t seenEpoch = 0;  // highest epoch announced by any box

// DMX address maps, rebuilt from the patch whenever a state is computed
static uint16_t pairOf[513];  // coarse <-> fine partner, 0 = 8 bit
static bool isFine[513];
static uint16_t chFix[513];   // fixture id, 0 = none
static uint8_t chGroup[513];

struct CLock {
  CLock() { xSemaphoreTakeRecursive(cm, portMAX_DELAY); }
  ~CLock() { xSemaphoreGiveRecursive(cm); }
};

// ---------- marks ----------

static inline bool markedRaw(uint16_t dmx) { return marks[(dmx - 1) >> 3] & (1 << ((dmx - 1) & 7)); }

void cuesManualChange(uint16_t from, uint16_t to) {
  if (from < 1) from = 1;
  if (to > DMX_UNIVERSE_SIZE) to = DMX_UNIVERSE_SIZE;
  if (from > to) return;
  bool added = false;
  portENTER_CRITICAL(&dmxDataMux);
  for (uint16_t d = from; d <= to; d++) {
    uint8_t& b = marks[(d - 1) >> 3];
    uint8_t bit = 1 << ((d - 1) & 7);
    added |= !(b & bit);
    b |= bit;
  }
  portEXIT_CRITICAL(&dmxDataMux);
  if (added) marksVer++;
  touched = true;
}

bool cuesMarked(uint16_t dmx) { return dmx >= 1 && dmx <= DMX_UNIVERSE_SIZE && markedRaw(dmx); }

uint16_t cuesMarkCount() {
  uint16_t n = 0;
  for (uint8_t b : marks) n += __builtin_popcount(b);
  return n;
}

void cuesMarksBitmap(uint8_t out[64]) {
  portENTER_CRITICAL(&dmxDataMux);
  memcpy(out, marks, 64);
  portEXIT_CRITICAL(&dmxDataMux);
}

uint32_t cuesMarksVersion() { return marksVer; }

// ---------- persistence ----------

static bool writeJson(const String& path, JsonDocument& doc) {
  String tmp = path + ".tmp";
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

static String partPath(uint16_t seq, uint16_t cue) { return String(CUE_DIR) + "/" + String(seq) + "_" + String(cue) + ".json"; }

static bool loadPart(uint16_t seq, uint16_t cue, CueData& out) {
  out = CueData();
  File f = LittleFS.open(partPath(seq, cue), "r");
  if (!f) return false;
  JsonDocument doc;
  bool ok = !deserializeJson(doc, f);
  f.close();
  if (!ok) return false;
  for (JsonArrayConst e : doc["v"].as<JsonArrayConst>()) {
    uint16_t a = e[0] | 0;
    if (a >= 1 && a <= DMX_UNIVERSE_SIZE) out.v.push_back({a, (uint8_t)(e[1] | 0)});
  }
  for (JsonArrayConst e : doc["r"].as<JsonArrayConst>()) {
    out.r.push_back({(uint16_t)(e[0] | 0), (uint8_t)(e[1] | 0), (uint16_t)(e[2] | 0)});
  }
  return true;
}

static bool savePart(uint16_t seq, uint16_t cue, const CueData& p) {
  JsonDocument doc;
  JsonArray v = doc["v"].to<JsonArray>();
  for (const auto& e : p.v) {
    JsonArray a = v.add<JsonArray>();
    a.add(e.first);
    a.add(e.second);
  }
  JsonArray r = doc["r"].to<JsonArray>();
  for (const PresetRef& e : p.r) {
    JsonArray a = r.add<JsonArray>();
    a.add(e.fixtureId);
    a.add(e.group);
    a.add(e.presetId);
  }
  return writeJson(partPath(seq, cue), doc);
}

static void showToJson(const Show& s, JsonDocument& doc) {
  doc["v"] = s.ver;
  doc["st"] = s.startup;
  doc["ns"] = s.nextSeq;
  JsonArray seqs = doc["s"].to<JsonArray>();
  for (const SeqMeta& q : s.seqs) {
    JsonObject o = seqs.add<JsonObject>();
    o["i"] = q.id;
    o["n"] = q.name;
    o["nc"] = q.nextCue;
    JsonArray cues = o["c"].to<JsonArray>();
    for (const CueMeta& c : q.cues) {
      JsonArray a = cues.add<JsonArray>();
      a.add(c.id);
      a.add(c.name);
      a.add(c.fade);
      a.add(c.dur);
    }
  }
}

static bool parseShow(const JsonDocument& doc, Show& s) {
  if (!doc["s"].is<JsonArrayConst>()) return false;
  s = Show();
  s.ver = doc["v"] | 0;
  s.startup = doc["st"] | 0;
  s.nextSeq = doc["ns"] | 1;
  for (JsonObjectConst o : doc["s"].as<JsonArrayConst>()) {
    SeqMeta q;
    q.id = o["i"] | 0;
    q.name = o["n"] | "";
    q.nextCue = o["nc"] | 1;
    if (!q.id) continue;
    for (JsonArrayConst a : o["c"].as<JsonArrayConst>()) {
      CueMeta c;
      c.id = a[0] | 0;
      c.name = a[1] | "";
      c.fade = a[2] | 0;
      c.dur = a[3] | 10;
      if (!c.id) continue;
      if (c.id >= q.nextCue) q.nextCue = c.id + 1;
      q.cues.push_back(c);
    }
    if (q.id >= s.nextSeq) s.nextSeq = q.id + 1;
    s.seqs.push_back(q);
  }
  return true;
}

static void saveShow(bool bump) {
  // a change made here must beat every version already out there, so that it is the one distributed
  if (bump) show.ver = ((int16_t)(seenVer - show.ver) > 0 ? seenVer : show.ver) + 1;
  JsonDocument doc;
  showToJson(show, doc);
  writeJson(SHOW_FILE, doc);
  blobValid = false;
}

static bool loadShow() {
  File f = LittleFS.open(SHOW_FILE, "r");
  if (!f) return false;
  JsonDocument doc;
  bool ok = !deserializeJson(doc, f) && parseShow(doc, show);
  f.close();
  blobValid = false;
  return ok;
}

static void saveLast() {
  JsonDocument doc;
  doc["seq"] = lastSeq;
  writeJson(PLAY_FILE, doc);
}

static void loadLast() {
  File f = LittleFS.open(PLAY_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  if (!deserializeJson(doc, f)) lastSeq = doc["seq"] | 0;
  f.close();
}

// ---------- model helpers (lock held) ----------

static SeqMeta* findSeq(uint16_t id) {
  for (SeqMeta& s : show.seqs) if (s.id == id) return &s;
  return nullptr;
}

static int cueIndex(const SeqMeta& s, uint16_t cue) {
  for (size_t i = 0; i < s.cues.size(); i++) if (s.cues[i].id == cue) return i;
  return -1;
}

static String cleanName(String n, size_t maxLen) {
  n.trim();
  if (n.length() > maxLen) n = n.substring(0, maxLen);
  return n;
}

static void clampTimes(uint16_t& fade, uint16_t& dur) {
  if (dur < 1) dur = 1;
  if (dur > TIME_MAX) dur = TIME_MAX;
  if (fade > dur) fade = dur;
}

static void loadParts() {
  pb.parts.clear();
  SeqMeta* s = findSeq(pb.seq);
  if (!s) return;
  for (const CueMeta& c : s->cues) {
    CueData d;
    if (loadPart(s->id, c.id, d)) pb.parts[c.id] = d;
  }
}

static void selectSeq(uint16_t id) {
  pb.seq = id;
  pb.cue = 0;
  loadParts();
  if (id && id != lastSeq) {
    lastSeq = id;
    saveLast();
  }
}

// Parts of deleted cues/sequences (ids below the "next" counters that are no longer listed).
static void cleanupParts() {
  File dir = LittleFS.open(CUE_DIR);
  if (!dir) return;
  std::vector<String> remove;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    f.close();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    int us = n.indexOf('_');
    if (us < 1 || !n.endsWith(".json")) continue;
    uint16_t seq = n.substring(0, us).toInt(), cue = n.substring(us + 1).toInt();
    SeqMeta* s = findSeq(seq);
    bool orphan = s ? (cue < s->nextCue && cueIndex(*s, cue) < 0) : seq < show.nextSeq;
    if (orphan) remove.push_back(String(CUE_DIR) + "/" + n);
  }
  dir.close();
  for (const String& p : remove) LittleFS.remove(p);
}

static void buildMaps() {
  memset(pairOf, 0, sizeof(pairOf));
  memset(isFine, 0, sizeof(isFine));
  memset(chFix, 0, sizeof(chFix));
  memset(chGroup, 0, sizeof(chGroup));
  fixturesLock();
  for (const Fixture& f : fixtureList) {
    uint16_t n = fixtureChannelCount(f);
    std::vector<int16_t> fine = fixtureFinePairs(f);
    std::vector<uint8_t> groups = fixtureGroups(f);
    for (uint16_t i = 0; i < n; i++) {
      uint16_t a = f.address + i;
      if (a < 1 || a > DMX_UNIVERSE_SIZE) continue;
      chFix[a] = f.id;
      chGroup[a] = i < groups.size() ? groups[i] : (uint8_t)GROUP_CONTROL;
      if (i < fine.size() && fine[i] >= 0) {
        uint16_t fa = f.address + fine[i];
        if (fa >= 1 && fa <= DMX_UNIVERSE_SIZE) {
          pairOf[a] = fa;
          pairOf[fa] = a;
          isFine[fa] = true;
        }
      }
    }
  }
  fixturesUnlock();
}

static void eraseRef(std::vector<PresetRef>& refs, uint16_t fix, uint8_t group) {
  refs.erase(std::remove_if(refs.begin(), refs.end(),
                            [&](const PresetRef& r) { return r.fixtureId == fix && r.group == group; }),
             refs.end());
}

// state(idx): all 0, then cues 0..idx; referenced presets with their current values.
static void computeState(int idx, uint8_t out[513], std::vector<PresetRef>& refsOut) {
  memset(out, 0, 513);
  buildMaps();
  std::vector<PresetRef> refs;
  SeqMeta* s = findSeq(pb.seq);
  if (s) {
    for (int i = 0; i <= idx && i < (int)s->cues.size(); i++) {
      auto it = pb.parts.find(s->cues[i].id);
      if (it == pb.parts.end()) continue;
      for (const auto& e : it->second.v) {
        out[e.first] = e.second;
        if (chFix[e.first]) eraseRef(refs, chFix[e.first], chGroup[e.first]);
      }
      for (const PresetRef& r : it->second.r) {
        eraseRef(refs, r.fixtureId, r.group);
        refs.push_back(r);
      }
    }
  }
  refsOut.clear();
  fixturesLock();
  for (const PresetRef& r : refs) {
    std::vector<std::pair<uint16_t, uint8_t>> vals;
    Fixture* f = findFixture(r.fixtureId);
    if (!f || !presetValuesFor(r.fixtureId, r.presetId, vals)) continue;
    uint16_t n = fixtureChannelCount(*f);
    for (const auto& cv : vals) {
      uint16_t a = f->address + cv.first;
      if (cv.first < n && a >= 1 && a <= DMX_UNIVERSE_SIZE) out[a] = cv.second;
    }
    refsOut.push_back(r);
  }
  fixturesUnlock();
}

static void clearMarks() {
  portENTER_CRITICAL(&dmxDataMux);
  memset(marks, 0, sizeof(marks));
  portEXIT_CRITICAL(&dmxDataMux);
  marksVer++;
  touched = false;
}

static int currentIndex();

void cuesClearChannels(uint16_t from, uint16_t to) {
  if (from < 1) from = 1;
  if (to > DMX_UNIVERSE_SIZE) to = DMX_UNIVERSE_SIZE;
  if (from > to) return;
  CLock l;
  uint8_t state[513];
  std::vector<PresetRef> refs;
  computeState(currentIndex(), state, refs);
  portENTER_CRITICAL(&dmxDataMux);
  for (uint16_t d = from; d <= to; d++) {
    if (!pb.fading) data[d] = state[d];  // during a fade the unmarked channel follows the fade again
    marks[(d - 1) >> 3] &= ~(1 << ((d - 1) & 7));
  }
  portEXIT_CRITICAL(&dmxDataMux);
  marksVer++;
  // the cue's preset is active again once no channel of its fixture group is changed by hand
  std::vector<PresetRef> active = presetRefs();
  bool changed = false;
  for (const PresetRef& r : refs) {
    bool have = false;
    for (const PresetRef& x : active) have |= x.fixtureId == r.fixtureId && x.group == r.group;
    if (have) continue;
    bool touchedGroup = false;
    for (uint16_t d = 1; d <= DMX_UNIVERSE_SIZE && !touchedGroup; d++)
      touchedGroup = chFix[d] == r.fixtureId && chGroup[d] == r.group && markedRaw(d);
    if (touchedGroup) continue;
    active.push_back(r);
    changed = true;
  }
  if (changed) presetsSetRefs(active);
}

// Moves the executor to cue index idx of the active sequence (-1 = no cue: all 0).
static void goTo(int idx, uint8_t move) {
  SeqMeta* s = findSeq(pb.seq);
  int n = s ? s->cues.size() : 0;
  if (idx >= n) idx = n - 1;
  pb.cue = (s && idx >= 0) ? s->cues[idx].id : 0;
  std::vector<PresetRef> refs;
  computeState(idx, pb.to, refs);
  uint32_t fade = (move == MOVE_FADE && s && idx >= 0) ? (uint32_t)s->cues[idx].fade * 100 : 0;
  portENTER_CRITICAL(&dmxDataMux);
  memset(marks, 0, sizeof(marks));
  memcpy(pb.from, data, 513);
  if (!fade) memcpy(data + 1, pb.to + 1, DMX_UNIVERSE_SIZE);
  portEXIT_CRITICAL(&dmxDataMux);
  marksVer++;
  touched = false;
  presetsSetRefs(refs);
  pb.fadeMs = fade;
  pb.fading = fade > 0;
  pb.cueStartMs = millis();
}

static int currentIndex() {
  SeqMeta* s = findSeq(pb.seq);
  return s ? cueIndex(*s, pb.cue) : -1;
}

static uint16_t collectMarked(CueData& part) {
  buildMaps();
  uint8_t mk[64];
  cuesMarksBitmap(mk);
  auto isMk = [&](uint16_t a) { return mk[(a - 1) >> 3] & (1 << ((a - 1) & 7)); };
  bool want[513] = {};
  for (uint16_t a = 1; a <= DMX_UNIVERSE_SIZE; a++) {
    if (!isMk(a)) continue;
    want[a] = true;
    if (pairOf[a]) want[pairOf[a]] = true;  // 16-bit pairs always as both bytes
  }
  part = CueData();
  for (uint16_t a = 1; a <= DMX_UNIVERSE_SIZE; a++) {
    if (!want[a]) continue;
    part.v.push_back({a, data[a]});
    if (!chFix[a]) continue;
    int active = presetActive(chFix[a], chGroup[a]);
    if (active < 0) continue;
    bool have = false;
    for (const PresetRef& r : part.r) have |= r.fixtureId == chFix[a] && r.group == chGroup[a];
    if (!have) part.r.push_back({chFix[a], chGroup[a], (uint16_t)active});
  }
  return part.v.size();
}

// Tracking update: new values win, references of the touched groups are replaced.
static void mergePart(CueData& dst, const CueData& add) {
  for (const auto& e : add.v) {
    auto it = std::lower_bound(dst.v.begin(), dst.v.end(), e.first,
                               [](const std::pair<uint16_t, uint8_t>& x, uint16_t a) { return x.first < a; });
    if (it != dst.v.end() && it->first == e.first) it->second = e.second;
    else dst.v.insert(it, e);
    if (chFix[e.first]) eraseRef(dst.r, chFix[e.first], chGroup[e.first]);
  }
  for (const PresetRef& r : add.r) {
    eraseRef(dst.r, r.fixtureId, r.group);
    dst.r.push_back(r);
  }
}

static inline bool newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

static void noteEpoch(uint16_t e) {
  if (newer(e, seenEpoch)) seenEpoch = e;
}

// A state change made here: newer than everything seen so far.
static void bumpEpoch() {
  epoch = (newer(seenEpoch, epoch) ? seenEpoch : epoch) + 1;
  seenEpoch = epoch;
}

static void adoptEpoch(uint16_t e) {
  noteEpoch(e);
  if (newer(e, epoch)) epoch = e;
}

// ---------- engine ----------

void cuesTick() {
  if (!cm || xSemaphoreTakeRecursive(cm, 0) != pdTRUE) return;
  uint32_t now = millis();
  if (touched) {
    touched = false;
    if (pb.mode == CUE_PLAY) {
      pb.mode = CUE_HOLD;
      holdEvent = true;
    }
  }
  if (pb.fading) {
    uint32_t el = now - pb.cueStartMs;
    bool done = el >= pb.fadeMs;
    float t = done ? 1.0f : (float)el / pb.fadeMs;
    uint8_t out[513];
    for (uint16_t a = 1; a <= DMX_UNIVERSE_SIZE; a++) {
      if (isFine[a]) continue;
      uint16_t f = pairOf[a];
      if (f) {
        int32_t v0 = (pb.from[a] << 8) | pb.from[f], v1 = (pb.to[a] << 8) | pb.to[f];
        int32_t v = v0 + (int32_t)lroundf((v1 - v0) * t);
        out[a] = v >> 8;
        out[f] = v & 0xFF;
      } else {
        out[a] = pb.from[a] + (int)lroundf(((int)pb.to[a] - (int)pb.from[a]) * t);
      }
    }
    portENTER_CRITICAL(&dmxDataMux);
    for (uint16_t a = 1; a <= DMX_UNIVERSE_SIZE; a++) {
      if (isFine[a]) continue;
      uint16_t f = pairOf[a];
      if (markedRaw(a) || (f && markedRaw(f))) continue;  // changed by hand during the fade
      data[a] = out[a];
      if (f) data[f] = out[f];
    }
    portEXIT_CRITICAL(&dmxDataMux);
    if (done) pb.fading = false;
  }
  if (pb.mode == CUE_PLAY) {
    SeqMeta* s = findSeq(pb.seq);
    int n = s ? s->cues.size() : 0;
    int idx = currentIndex();
    if (n < 2) {
      pb.mode = CUE_HOLD;  // a single cue has nothing to play (e.g. after deleting cues)
    } else if (idx >= 0) {
      uint32_t dur = (uint32_t)max((uint16_t)1, s->cues[idx].dur) * 100;
      if (now - pb.cueStartMs >= dur) goTo((idx + 1) % n, MOVE_FADE);
    }
  }
  xSemaphoreGiveRecursive(cm);
}

static void fillTransport(CueTransport& out, uint8_t move) {
  out.seq = pb.seq;
  out.cue = pb.cue;
  out.mode = pb.mode;
  out.move = move;
  out.epoch = epoch;
}

String cuesTransport(uint8_t op, uint16_t arg, CueTransport& out) {
  CLock l;
  SeqMeta* s = findSeq(pb.seq);
  int n = s ? s->cues.size() : 0;
  int idx = currentIndex();
  uint8_t move = MOVE_INSTANT;
  switch (op) {
    case OP_GO:
    case OP_BACK:
    case OP_SKIP:
    case OP_SKIP_BACK: {
      if (n == 0) return "The sequence has no cues yet";
      bool fwd = op == OP_GO || op == OP_SKIP;
      int next = fwd ? (idx + 1) % n : (idx <= 0 ? n - 1 : idx - 1);
      move = (op == OP_GO || op == OP_BACK) ? MOVE_FADE : MOVE_INSTANT;
      goTo(next, move);
      break;
    }
    case OP_GOTO: {
      int i = s ? cueIndex(*s, arg) : -1;
      if (i < 0) return "Cue not found";
      goTo(i, MOVE_INSTANT);
      pb.mode = CUE_HOLD;
      break;
    }
    case OP_PLAY:
      // continues right away with the next cue and its fade (manual values are dropped by the move)
      if (n < 2) return "Playing needs at least two cues";
      goTo(idx < 0 ? 0 : (idx + 1) % n, MOVE_FADE);
      pb.mode = CUE_PLAY;
      move = MOVE_FADE;
      break;
    case OP_HOLD:
      pb.mode = CUE_HOLD;
      move = MOVE_KEEP;
      break;
    case OP_SELECT: {
      SeqMeta* t = findSeq(arg);
      if (!t) return "Sequence not found";
      selectSeq(arg);
      goTo(0, MOVE_FADE);
      pb.mode = t->cues.size() > 1 ? CUE_PLAY : CUE_HOLD;
      move = MOVE_FADE;
      break;
    }
    case OP_DISCARD:
      goTo(idx, MOVE_INSTANT);
      break;
    default:
      return "Unknown action";
  }
  bumpEpoch();
  fillTransport(out, move);
  return "";
}

void cuesApplyTransport(const CueTransport& t) {
  CLock l;
  adoptEpoch(t.epoch);
  uint8_t move = t.move;
  if (t.seq != pb.seq) {
    selectSeq(t.seq);
    if (move == MOVE_KEEP) move = MOVE_INSTANT;
  }
  SeqMeta* s = findSeq(pb.seq);
  int idx = s ? cueIndex(*s, t.cue) : -1;
  if (move == MOVE_KEEP && t.cue == pb.cue) {
    if (t.mode == CUE_PLAY && cuesMarkCount()) goTo(idx, MOVE_INSTANT);
    else if (t.mode == CUE_PLAY && pb.mode != CUE_PLAY && !pb.fading) pb.cueStartMs = millis();
    pb.mode = t.mode;
    return;
  }
  if (idx < 0 && t.cue) {
    // cue not in our metadata yet: resolved when the show arrives
    pb.cue = t.cue;
    pb.mode = t.mode;
    clearMarks();
    return;
  }
  goTo(idx, move == MOVE_KEEP ? MOVE_INSTANT : move);
  pb.mode = t.mode;
}

bool cuesTakeHoldEvent(CueTransport& out) {
  CLock l;
  if (!holdEvent) return false;
  holdEvent = false;
  bumpEpoch();
  fillTransport(out, MOVE_KEEP);
  return true;
}

void cuesAlign(uint16_t seq, uint16_t cue, uint8_t mode, uint32_t msInCue, uint16_t ep, bool leader) {
  CLock l;
  noteEpoch(ep);
  if (newer(epoch, ep) || (ep == epoch && !leader)) return;
  uint32_t now = millis();
  // a newer state usually arrives as a command (with retries) right after; only a state that stays
  // newer for longer than those retries was really missed
  static uint16_t newerEpoch = 0;
  static uint32_t newerSinceMs = 0;
  if (newer(ep, epoch)) {
    if (ep != newerEpoch) {
      newerEpoch = ep;
      newerSinceMs = now;
      return;
    }
    if (now - newerSinceMs < 9000) return;
  }
  if (seq != pb.seq || cue != pb.cue) {
    // missed a transport (or the leader's state differs): take over that state
    CueTransport t;
    t.seq = seq;
    t.cue = cue;
    t.mode = mode;
    t.move = MOVE_INSTANT;
    t.epoch = ep;
    cuesApplyTransport(t);
    pb.cueStartMs = now - msInCue;
    return;
  }
  // already on that cue: keep the output (and any marks), take over epoch, mode and timing
  adoptEpoch(ep);
  if (mode != pb.mode) pb.mode = mode;
  if (pb.mode == CUE_PLAY && !pb.fading) {
    int32_t diff = (int32_t)(now - pb.cueStartMs) - (int32_t)msInCue;
    if (diff > 150 || diff < -150) pb.cueStartMs = now - msInCue;
  }
}

// ---------- storing ----------

CueStoreResult cuesStore(bool insert, const String& name, uint16_t fade, uint16_t dur, const String& newSeqName) {
  CLock l;
  CueStoreResult res;
  res.insert = insert;
  SeqMeta* s = findSeq(pb.seq);
  bool showChanged = false;
  if (!s) {
    String n = cleanName(newSeqName, SEQ_NAME_MAX);
    if (n.isEmpty()) {
      res.err = "Name the new sequence first";
      return res;
    }
    SeqMeta q;
    q.id = show.nextSeq++;
    q.name = n;
    show.seqs.push_back(q);
    selectSeq(q.id);
    s = findSeq(q.id);
    showChanged = true;
    insert = res.insert = true;
  }
  int idx = cueIndex(*s, pb.cue);
  if (!insert && idx < 0) {
    res.err = "There is no current cue to update";
    return res;
  }
  CueData part;
  res.count = collectMarked(part);
  if (insert) {
    CueMeta m;
    m.id = s->nextCue++;
    m.name = cleanName(name, SEQ_NAME_MAX);
    if (m.name.isEmpty()) m.name = "Cue " + String(idx + 2);
    m.fade = fade;
    m.dur = dur;
    clampTimes(m.fade, m.dur);
    s->cues.insert(s->cues.begin() + (idx + 1), m);
    if (!savePart(s->id, m.id, part)) res.err = "Saving failed";
    pb.parts[m.id] = part;
    pb.cue = m.id;
    idx++;
    showChanged = true;
  } else {
    CueData& cur = pb.parts[pb.cue];
    mergePart(cur, part);
    if (!savePart(s->id, pb.cue, cur)) res.err = "Saving failed";
  }
  if (showChanged) saveShow(true);
  pb.mode = CUE_HOLD;
  goTo(idx, MOVE_INSTANT);
  bumpEpoch();
  res.seq = pb.seq;
  res.cue = pb.cue;
  res.epoch = epoch;
  return res;
}

uint16_t cuesStorePart(uint16_t seq, uint16_t cue, bool insert, uint16_t ep) {
  CLock l;
  adoptEpoch(ep);
  if (seq != pb.seq) selectSeq(seq);
  CueData part;
  uint16_t count = collectMarked(part);
  CueData stored;
  if (!insert) loadPart(seq, cue, stored);
  mergePart(stored, part);
  savePart(seq, cue, stored);
  pb.parts[cue] = stored;
  pb.mode = CUE_HOLD;
  SeqMeta* s = findSeq(seq);
  int idx = s ? cueIndex(*s, cue) : -1;
  if (idx >= 0) {
    goTo(idx, MOVE_INSTANT);
  } else {
    pb.cue = cue;
    clearMarks();
  }
  return count;
}

// ---------- show editing ----------

String cuesNewSequence(const String& name, CueTransport& out) {
  CLock l;
  String n = cleanName(name, SEQ_NAME_MAX);
  if (n.isEmpty()) return "Name missing";
  SeqMeta q;
  q.id = show.nextSeq++;
  q.name = n;
  show.seqs.push_back(q);
  saveShow(true);
  selectSeq(q.id);
  pb.mode = CUE_HOLD;
  goTo(-1, MOVE_INSTANT);
  bumpEpoch();
  fillTransport(out, MOVE_INSTANT);
  return "";
}

String cuesRenameSequence(uint16_t seq, const String& name) {
  CLock l;
  SeqMeta* s = findSeq(seq);
  if (!s) return "Sequence not found";
  String n = cleanName(name, SEQ_NAME_MAX);
  if (n.isEmpty()) return "Name missing";
  s->name = n;
  saveShow(true);
  return "";
}

String cuesDeleteSequence(uint16_t seq) {
  CLock l;
  if (seq == pb.seq) return "This sequence is active. Select another one first.";
  auto it = std::find_if(show.seqs.begin(), show.seqs.end(), [seq](const SeqMeta& s) { return s.id == seq; });
  if (it == show.seqs.end()) return "Sequence not found";
  show.seqs.erase(it);
  if (show.startup == seq) show.startup = 0;
  saveShow(true);
  cleanupParts();
  return "";
}

String cuesSetStartup(uint16_t seq) {
  CLock l;
  if (seq && !findSeq(seq)) return "Sequence not found";
  show.startup = seq;
  saveShow(true);
  return "";
}

String cuesEditCue(uint16_t seq, uint16_t cue, const String& name, uint16_t fade, uint16_t dur) {
  CLock l;
  SeqMeta* s = findSeq(seq);
  int i = s ? cueIndex(*s, cue) : -1;
  if (i < 0) return "Cue not found";
  String n = cleanName(name, SEQ_NAME_MAX);
  if (!n.isEmpty()) s->cues[i].name = n;
  clampTimes(fade, dur);
  s->cues[i].fade = fade;
  s->cues[i].dur = dur;
  saveShow(true);
  return "";
}

String cuesMoveCue(uint16_t seq, uint16_t cue, int dir) {
  CLock l;
  SeqMeta* s = findSeq(seq);
  int i = s ? cueIndex(*s, cue) : -1;
  if (i < 0) return "Cue not found";
  int j = i + (dir < 0 ? -1 : 1);
  if (j < 0 || j >= (int)s->cues.size()) return "";
  std::swap(s->cues[i], s->cues[j]);
  saveShow(true);
  if (seq == pb.seq) goTo(currentIndex(), MOVE_INSTANT);  // the tracked state may have changed
  return "";
}

String cuesDeleteCue(uint16_t seq, uint16_t cue) {
  CLock l;
  SeqMeta* s = findSeq(seq);
  int i = s ? cueIndex(*s, cue) : -1;
  if (i < 0) return "Cue not found";
  int cur = seq == pb.seq ? currentIndex() : -1;
  s->cues.erase(s->cues.begin() + i);
  saveShow(true);
  cleanupParts();
  if (seq == pb.seq) {
    pb.parts.erase(cue);
    // on the deleted cue: go to the one before it
    int next = cur == i ? max(0, i - 1) : (cur > i ? cur - 1 : cur);
    if (s->cues.empty()) next = -1;
    goTo(next, MOVE_INSTANT);
  }
  return "";
}

// ---------- queries ----------

PlayInfo cuesPlayInfo() {
  CLock l;
  PlayInfo p;
  SeqMeta* s = findSeq(pb.seq);
  if (!s) return p;
  p.seq = s->id;
  p.seqName = s->name;
  p.count = s->cues.size();
  p.idx = cueIndex(*s, pb.cue);
  p.cue = pb.cue;
  p.mode = pb.mode;
  p.ms = millis() - pb.cueStartMs;
  p.epoch = epoch;
  if (p.idx >= 0) {
    p.cueName = s->cues[p.idx].name;
    p.fade = s->cues[p.idx].fade;
    p.dur = s->cues[p.idx].dur;
  }
  return p;
}

std::vector<SeqMeta> cuesSequences() {
  CLock l;
  return show.seqs;
}

bool cuesNeedsShow() {
  CLock l;
  if (!pb.seq) return false;
  SeqMeta* s = findSeq(pb.seq);
  return !s || (pb.cue && cueIndex(*s, pb.cue) < 0);
}

uint16_t cuesStartup() {
  CLock l;
  return show.startup;
}

uint16_t cuesPartSize(uint16_t seq, uint16_t cue) {
  CLock l;
  if (seq == pb.seq) {
    auto it = pb.parts.find(cue);
    return it == pb.parts.end() ? 0 : it->second.v.size();
  }
  CueData d;
  loadPart(seq, cue, d);
  return d.v.size();
}

// ---------- link ----------

static void ensureBlob() {
  if (blobValid) return;
  JsonDocument doc;
  showToJson(show, doc);
  blobCache = "";
  serializeJson(doc, blobCache);
  blobValid = true;
}

uint16_t cuesShowVersion() {
  CLock l;
  return show.ver;
}

void cuesNoteShowVersion(uint16_t v) {
  CLock l;
  if ((int16_t)(v - seenVer) > 0) seenVer = v;
}

uint32_t cuesShowCrc() {
  CLock l;
  ensureBlob();
  return esp_rom_crc32_le(0, (const uint8_t*)blobCache.c_str(), blobCache.length());
}

String cuesShowBlob() {
  CLock l;
  ensureBlob();
  return blobCache;
}

bool cuesApplyShowBlob(const uint8_t* buf, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)buf, len)) return false;
  Show ns;
  if (!parseShow(doc, ns)) return false;
  CLock l;
  std::vector<uint16_t> before;
  if (SeqMeta* s = findSeq(pb.seq)) for (const CueMeta& c : s->cues) before.push_back(c.id);
  int oldIdx = currentIndex();
  bool wasPending = pb.cue && oldIdx < 0;
  show = ns;
  saveShow(false);
  cleanupParts();
  SeqMeta* s = findSeq(pb.seq);
  if (!s) return true;  // active sequence deleted elsewhere: keep the output
  std::vector<uint16_t> after;
  for (const CueMeta& c : s->cues) after.push_back(c.id);
  if (after == before && !wasPending) return true;
  loadParts();
  int idx = cueIndex(*s, pb.cue);
  // cues up to the current one unchanged (e.g. one inserted after it): the output stays, and so do the marks
  if (!wasPending && idx >= 0 && idx == oldIdx && std::equal(after.begin(), after.begin() + idx + 1, before.begin())) {
    return true;
  }
  if (idx < 0 && !wasPending) idx = s->cues.empty() ? -1 : constrain(oldIdx - 1, 0, (int)s->cues.size() - 1);
  if (idx >= 0 || s->cues.empty()) goTo(idx, MOVE_INSTANT);
  return true;
}

// ---------- start ----------

static void migrateSnapshot() {
  std::vector<PresetRef> legacy = presetsTakeLegacyRefs();
  CueData part;
  for (uint16_t a = 1; a <= DMX_UNIVERSE_SIZE; a++) if (data[a]) part.v.push_back({a, data[a]});
  part.r = legacy;
  if (part.v.empty() && part.r.empty()) return;
  SeqMeta s;
  s.id = 1;
  s.name = "Snapshot";
  s.nextCue = 2;
  CueMeta c;
  c.id = 1;
  c.name = "Snapshot";
  s.cues.push_back(c);
  show = Show();
  show.ver = 1;
  show.nextSeq = 2;
  show.seqs.push_back(s);
  savePart(1, 1, part);
  saveShow(false);
  lastSeq = 1;
  saveLast();
  Serial.printf("Cues: old snapshot migrated (%u channels, %u references)\n", (unsigned)part.v.size(),
                (unsigned)part.r.size());
}

void cuesBegin() {
  cm = xSemaphoreCreateRecursiveMutex();
  CLock l;
  LittleFS.mkdir(CUE_DIR);
  loadLast();
  if (loadShow()) {
    presetsTakeLegacyRefs();
    portENTER_CRITICAL(&dmxDataMux);
    memset(data + 1, 0, DMX_UNIVERSE_SIZE);
    portEXIT_CRITICAL(&dmxDataMux);
  } else {
    migrateSnapshot();
  }
  uint16_t start = show.startup && findSeq(show.startup) ? show.startup : lastSeq;
  SeqMeta* s = findSeq(start);
  if (s) {
    selectSeq(start);
    goTo(0, MOVE_FADE);
    pb.mode = s->cues.size() > 1 ? CUE_PLAY : CUE_HOLD;
  } else {
    pb.seq = 0;
    goTo(-1, MOVE_INSTANT);
  }
  Serial.printf("Cues: %u sequences, starting %s\n", (unsigned)show.seqs.size(), s ? s->name.c_str() : "none");
}
