#pragma once
#include <ESPAsyncWebServer.h>

void registerFixtureRoutes(AsyncWebServer& server);
void sendFixturesPage(AsyncWebServerRequest* request);

// shared page helpers (dark theme)
String esc(const String& s);
bool hasArg(AsyncWebServerRequest* r, const char* name);
String arg(AsyncWebServerRequest* r, const char* name);
AsyncResponseStream* pageBegin(AsyncWebServerRequest* req, const String& title, const char* extraHead = nullptr);
void pageEnd(AsyncWebServerRequest* req, AsyncResponseStream* r);
void sendMessage(AsyncWebServerRequest* req, int code, const String& title, const String& msg);
