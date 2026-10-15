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
extern volatile bool restartRequested;  // handled in loop() so that the HTTP response gets out first

bool isDMXActiveTime();
void saveTimeControl();
void saveWiFiConfig(const char* ssid, const char* password);
String getFormattedTime();

// Snapshot = DMX values + active preset references (stored on Save, restored at boot).
void snapshotSave();
void snapshotReset();

void registerSystemRoutes(class AsyncWebServer& server);
String localBoxName();
// Hostname / AP SSID derived from a box name ("Bühne links" -> "buehne-links"); "" if nothing usable is left.
String networkNameFor(const String& boxName);
// At boot (LittleFS mounted, before WiFi): use the stored box name for hostname and AP SSID.
void applyStoredNetworkName();
