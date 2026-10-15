#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

#define FIXTURE_NAME_MAX 32
#define DMX_UNIVERSE_SIZE 512

// Guards multi-byte updates of the DMX values (e.g. 16-bit coarse/fine) against the
// copy into the output buffer, so a frame never mixes a new coarse with an old fine byte.
extern portMUX_TYPE dmxDataMux;

struct ChannelRange {
  uint8_t from;
  uint8_t to;
  String name;
};

// Feature groups as in grandMA2
enum ChannelGroup : uint8_t {
  GROUP_DIMMER = 0,
  GROUP_POSITION,
  GROUP_GOBO,
  GROUP_COLOR,
  GROUP_BEAM,
  GROUP_FOCUS,
  GROUP_CONTROL,
  GROUP_COUNT
};
extern const char* const GROUP_NAMES[GROUP_COUNT];
// Position and focus presets are stored per fixture, all others per fixture type.
inline bool groupIsSelective(uint8_t g) { return g == GROUP_POSITION || g == GROUP_FOCUS; }
int8_t parseGroupName(const String& s);  // -1 if not a group name
uint8_t guessGroup(const String& type, const String& name);

struct ChannelDef {
  String name;
  String type;
  std::vector<ChannelRange> ranges;
  int16_t fine = -1;  // index of the fine (LSB) channel if this is a 16-bit coarse channel
  int8_t group = -1;  // explicit group, -1 = guessed from type/name
};

// One control in the UI: a single channel or a 16-bit coarse/fine pair.
struct FixtureControl {
  uint16_t ch;
  int16_t fine;  // -1 = 8 bit
};

// Pairs "X fine" with "X" (or type "fine:X") and fills ChannelDef::fine.
// Explicit pairs that are already set are kept.
void resolveFinePairs(std::vector<ChannelDef>& channels);
// Same rule for plain channel name lists (generic fixtures); returns fine index per channel.
std::vector<int16_t> finePairsFromNames(const std::vector<String>& names);
std::vector<FixtureControl> controlsFromFine(const std::vector<int16_t>& fine);

struct FixtureDef {
  uint16_t id = 0;
  String name;
  String manufacturer;
  std::vector<ChannelDef> channels;
};

struct Fixture {
  uint16_t id = 0;
  String name;
  uint16_t address = 1;
  uint16_t defId = 0;          // 0 = generic fixture
  uint16_t channelCount = 1;   // only used for generic fixtures
  std::vector<String> channelNames;  // only used for generic fixtures
};

extern std::vector<FixtureDef> fixtureDefs;
extern std::vector<Fixture> fixtureList;

bool fixturesBegin();  // mounts LittleFS (formats on first use)
void fixturesLoad();

// Changes on every fixture/definition change (random start value per boot).
uint16_t fixturesVersion();
// Mutations run in the web server task; other tasks must hold this lock while reading.
void fixturesLock();
void fixturesUnlock();

FixtureDef* findDef(uint16_t id);
Fixture* findFixture(uint16_t id);

uint16_t fixtureChannelCount(const Fixture& f);
String fixtureChannelName(const Fixture& f, uint16_t ch);
const ChannelDef* fixtureChannelDef(const Fixture& f, uint16_t ch);
std::vector<int16_t> fixtureFinePairs(const Fixture& f);
std::vector<FixtureControl> fixtureControls(const Fixture& f);
// Group per channel (fine channels inherit the group of their coarse channel).
std::vector<uint8_t> fixtureGroups(const Fixture& f);
std::vector<uint8_t> groupsFor(const std::vector<String>& names, const std::vector<String>& types,
                               const std::vector<int8_t>& explicitGroups, const std::vector<int16_t>& fine);
void fixturesBumpVersion();  // presets etc. that the link should re-announce
bool fixtureOverlaps(const Fixture& f);

// Returns an empty string on success, otherwise an error message.
String saveFixture(Fixture& f);  // f.id == 0 creates a new fixture
String deleteFixture(uint16_t id);
String saveDef(FixtureDef& d);   // d.id == 0 creates a new definition
String deleteDef(uint16_t id);

void defToJson(const FixtureDef& d, JsonObject obj);
// CRC32 over the JSON form: identical definitions on different boxes have the same value.
uint32_t defCrc(const FixtureDef& d);
FixtureDef* findDefByCrc(uint32_t crc);
// Imports DmxBox/labX format (object, or array of objects). Returns number of
// created definitions; err is set on failure.
int importDefs(JsonVariantConst root, String& err);
// Creates one definition from a single-mode DmxBox JSON object; returns its id, 0 on error.
uint16_t importSingleDef(JsonVariantConst root, String& err);

// Text format used by the definition editor, one channel per line:
//   Name | type | 0-9 Open; 10-19 Red; 20 Blue
String defChannelsToText(const FixtureDef& d);
String defChannelsFromText(const String& text, FixtureDef& d);
