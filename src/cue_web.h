#pragma once
#include <ESPAsyncWebServer.h>

void registerCueRoutes(AsyncWebServer& server);
// Playback panel of the home page (sequence, cue, transport, store).
void renderPlaybackPanel(AsyncResponseStream* r);
// Compact two-line playback bar for the fixture pages (cue counter, transport, store).
void renderMiniPlayback(AsyncResponseStream* r);
