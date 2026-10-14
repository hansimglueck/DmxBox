#pragma once
#ifdef HAS_LORA
#include <ESPAsyncWebServer.h>

void registerSyncRoutes(AsyncWebServer& server);
void renderLocalBoxHeader(AsyncResponseStream* r);
void renderRemoteFixtures(AsyncResponseStream* r);
#endif
