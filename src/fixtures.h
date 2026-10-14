#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

#define FIXTURE_NAME_MAX 32
#define DMX_UNIVERSE_SIZE 512

struct ChannelRange {
  uint8_t from;
  uint8_t to;
  String name;
};

struct ChannelDef {
  String name;
  String type;
  std::vector<ChannelRange> ranges;
};

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

FixtureDef* findDef(uint16_t id);
Fixture* findFixture(uint16_t id);

uint16_t fixtureChannelCount(const Fixture& f);
String fixtureChannelName(const Fixture& f, uint16_t ch);
const ChannelDef* fixtureChannelDef(const Fixture& f, uint16_t ch);
bool fixtureOverlaps(const Fixture& f);

// Returns an empty string on success, otherwise an error message.
String saveFixture(Fixture& f);  // f.id == 0 creates a new fixture
String deleteFixture(uint16_t id);
String saveDef(FixtureDef& d);   // d.id == 0 creates a new definition
String deleteDef(uint16_t id);

void defToJson(const FixtureDef& d, JsonObject obj);
// Imports DmxBox/labX format (object, or array of objects). Returns number of
// created definitions; err is set on failure.
int importDefs(JsonVariantConst root, String& err);

// Text format used by the definition editor, one channel per line:
//   Name | type | 0-9 Open; 10-19 Red; 20 Blue
String defChannelsToText(const FixtureDef& d);
String defChannelsFromText(const String& text, FixtureDef& d);
