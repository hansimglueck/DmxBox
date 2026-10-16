#pragma once
#include <Arduino.h>

// State and helpers defined in main.cpp, used by the system web pages and the link.
extern const char* ssid;  // hostname / AP SSID of this box
extern char savedSSID[64];
extern bool apModeActive;
extern bool timeInitialized;
extern bool timeControlEnabled;
extern uint8_t dmxWeekdayStartHour[7];
extern uint8_t dmxWeekdayStartMinute[7];
extern uint8_t dmxWeekdayEndHour[7];
extern uint8_t dmxWeekdayEndMinute[7];
extern volatile bool restartRequested;
extern volatile uint16_t loopFps;  // main loop iterations per second (DMX frames)  // handled in loop() so that the HTTP response gets out first

bool isDMXActiveTime();
// Local time, false while the clock is not set. Unlike getLocalTime(&t, 0) it never fails at random.
bool localTimeNow(struct tm& t);
void saveTimeControl();
void saveWiFiConfig(const char* ssid, const char* password);
String getFormattedTime();

void registerSystemRoutes(class AsyncWebServer& server);
String localBoxName();
// Hostname / AP SSID derived from a box name ("Bühne links" -> "buehne-links"); "" if nothing usable is left.
String networkNameFor(const String& boxName);
// Names stored with the old default scheme "dmxbox-heltec-xxxx" become "dmxbox-xxxx".
String migrateBoxName(const String& name);
// At boot (LittleFS mounted, before WiFi): use the stored box name for hostname and AP SSID.
void applyStoredNetworkName();
