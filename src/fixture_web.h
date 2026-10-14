#pragma once
#include <ESPAsyncWebServer.h>

void registerFixtureRoutes(AsyncWebServer& server);
void sendFixturesPage(AsyncWebServerRequest* request);
