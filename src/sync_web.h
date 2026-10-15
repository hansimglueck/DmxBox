#pragma once
#ifdef HAS_LORA
#include <ESPAsyncWebServer.h>

void registerSyncRoutes(AsyncWebServer& server);
void renderRemoteTiles(AsyncResponseStream* r);
void renderLoraSection(AsyncResponseStream* r);
#endif
