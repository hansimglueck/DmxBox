#ifndef OTA_WRAPPER_H /* include guards */
#define OTA_WRAPPER_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#if defined(ESP32)
#include <ESPmDNS.h>
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#endif
void setupOTA(const char* OTAssid, const char* OTApassword);
void processOTA();

#endif