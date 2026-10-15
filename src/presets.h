#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include "fixtures.h"

// Presets per feature group (grandMA2 style). Values are stored under a key:
//   "d<defId>"     per fixture type (dimmer, color, gobo, beam, control)
//   "f<fixtureId>" per fixture (position, focus, and generic fixtures)
struct Preset {
  uint16_t id = 0;
  String name;
  uint8_t group = GROUP_COLOR;
  std::map<String, std::vector<std::pair<uint16_t, uint8_t>>> values;  // key -> (channel, value)
};

// A fixture group that currently follows a preset. Overwriting the preset updates it live;
// moving one of its channels by hand breaks the reference.
struct PresetRef {
  uint16_t fixtureId;
  uint8_t group;
  uint16_t presetId;
};

// A preset as seen from one fixture.
struct PresetInfo {
  uint16_t id;
  String name;
  uint8_t group;
  bool stored;  // has values for this fixture (or its type)
};

void presetsLoad();
String presetKey(const Fixture& f, uint8_t group);
std::vector<PresetInfo> presetsFor(const Fixture& f, uint8_t group);
int presetActive(uint16_t fixtureId, uint8_t group);  // preset id or -1
std::vector<PresetRef> presetRefs();
uint32_t presetRefsVersion();

// Return "" on success, otherwise an error message.
String presetCreate(uint16_t fixtureId, uint8_t group, const String& name);
String presetStore(uint16_t fixtureId, uint16_t presetId);
String presetRecall(uint16_t fixtureId, uint16_t presetId);
String presetRename(uint16_t presetId, const String& name);
String presetDelete(uint16_t presetId);

// Channels changed by hand (fader, remote SET): breaks references of the affected groups.
void presetChannelsChanged(uint16_t dmxFrom, uint16_t dmxTo);
void presetsFixtureDeleted(uint16_t fixtureId);

// Active references are part of the snapshot: written on Save, applied again at boot.
void presetsSnapshotSave();
void presetsClearRefs();
