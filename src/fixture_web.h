#pragma once
#include <ESPAsyncWebServer.h>
#include "fixtures.h"

void registerFixtureRoutes(AsyncWebServer& server);
void sendFixturesPage(AsyncWebServerRequest* request);

// Static CSS/JS served straight from flash, with a content hash in the URL so that the browser can
// cache it for good (pages stay small and are built in less RAM). <script>/<style> wrappers are stripped.
void registerAsset(AsyncWebServer& server, const char* path, const char* type, const char* text);
String assetUrl(const char* path);

// shared page helpers
String esc(const String& s);
bool hasArg(AsyncWebServerRequest* r, const char* name);
String arg(AsyncWebServerRequest* r, const char* name);
AsyncResponseStream* pageBegin(AsyncWebServerRequest* req, const String& title, const char* extraHead = nullptr);
void pageEnd(AsyncWebServerRequest* req, AsyncResponseStream* r);
void sendMessage(AsyncWebServerRequest* req, int code, const String& title, const String& msg);
// Back arrow and path from Home to this page: {label, url}, the last entry is the page itself;
// an entry without url is shown as plain text. The arrow leads to the nearest linked entry before it.
void crumbs(AsyncResponseStream* r, const std::vector<std::pair<String, String>>& path);
// Message page that moves on to `next` after `seconds` (used while the link carries a change).
// waitBox != 0: wait until that box confirmed all pending commands, then `seconds` more.
void sendProgress(AsyncWebServerRequest* req, const String& title, const String& msg, const String& next, int seconds,
                  uint16_t waitBox = 0);

struct TileChannel {
  String name;
  uint8_t value;                              // coarse byte for 16-bit pairs
  const std::vector<ChannelRange>* ranges;    // optional (color wheels: range name -> swatch)
};
struct TileGroup {
  uint8_t group;
  String preset;  // active preset name, empty if none
  std::vector<TileChannel> channels;
};
// Home page tile: whole tile opens href, group chips open href#g<group> (the fader tab).
void fixtureTile(AsyncResponseStream* r, const String& href, const String& name, const String& dmx, const String& type,
                 const String& box, const String& warn, const std::vector<TileGroup>& groups, uint16_t boxId, bool off);

String fixtureTypeLabel(const Fixture& f);
String dmxRangeLabel(uint16_t address, uint16_t count);
