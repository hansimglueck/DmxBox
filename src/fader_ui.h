#pragma once
#include <ESPAsyncWebServer.h>
#include <vector>
#include "fixtures.h"
#include "presets.h"

// Fader grid (vertical drag encoders, modelled on the labX DMX plugin) with
// grandMA2-style group tabs and preset bars.
//   getUrl    -> {"v":[channel values], "p":[pending flags], "r":{"<group>":<presetId>}, "m":[changed channels]}
//   setUrl    + "&ch=<channel>&v=<value>"            (16-bit value for coarse/fine pairs)
//   presetUrl + "&op=recall|store|create|rename|delete&g=<group>&p=<id>&name=<name>"
//   clearUrl  + "&ch=<channel>"                       (back to the cue value, removes the mark)
String faderHead();  // stylesheet link for pages with a fader grid
void registerFaderAssets(AsyncWebServer& server);

struct FaderGrid {
  String setUrl, getUrl, presetUrl;
  String clearUrl;  // + "&ch=<channel>": back to the cue value
  uint32_t pollMs = 1500;
  uint32_t reloadDelayMs = 0;  // remote boxes need time until the change comes back over the link
  String valuesJson;           // initial channel values (index = channel)
  String refsJson = "{}";      // initial active presets {"<group>": presetId}
  String marksJson = "[]";     // channels changed by hand since the last cue change
  String version;              // fixture/preset list version; the page reloads when the API reports another
  uint16_t box = 0;            // remote box id (0 = this box): preset actions wait for its confirmation
  String boxName;
};

void faderTabs(AsyncResponseStream* r, const std::vector<uint8_t>& groupsPresent);
void faderGridBegin(AsyncResponseStream* r, const FaderGrid& g);
void faderCard(AsyncResponseStream* r, const String& name, const String& detailUrl, uint16_t ch, int16_t fine,
               uint8_t group, uint16_t address, int16_t fineAddress, const std::vector<ChannelRange>* ranges);
void faderGridEnd(AsyncResponseStream* r);
// Named ranges of a channel from the fixture definition, offered as presets in its group's panel.
struct RangeChannel {
  uint16_t ch;
  int16_t fine;  // -1 = 8 bit
  String name;
  const std::vector<ChannelRange>* ranges;
};
void faderPresetPanel(AsyncResponseStream* r, uint8_t group, const std::vector<PresetInfo>& list, int activeId,
                      const std::vector<RangeChannel>& rangeChannels = {});
void faderScript(AsyncResponseStream* r);

// Groups that occur in a group-per-channel list, in grandMA2 order.
std::vector<uint8_t> groupsPresent(const std::vector<uint8_t>& channelGroups);
