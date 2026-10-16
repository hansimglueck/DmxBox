#pragma once
#include <Arduino.h>

struct DisplayStatus {
  bool apMode;
  const char* ssid;
  String ip;
  int rssi;
  int apClients;
  bool dmxReady;
  bool dmxActive;
  unsigned int fps;
  bool timeValid;
  String time;
  unsigned long uptimeMs;
  int linkPeers = -1;  // online boxes in the LoRa link, -1 = no link
  String boxName;
};

#ifdef HAS_OLED
void displayInit();
void displayMessage(const char* line1, const char* line2 = "");
void displayUpdate(const DisplayStatus& s);
#else
inline void displayInit() {}
inline void displayMessage(const char*, const char* = "") {}
inline void displayUpdate(const DisplayStatus&) {}
#endif
