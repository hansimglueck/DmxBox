#ifdef HAS_OLED
#include "display.h"
#include <Wire.h>
#include <SSD1306Wire.h>

static const char* const DISPLAY_TITLE = "L4Bx-dmxbox";
static SSD1306Wire oled(0x3c, SDA_OLED, SCL_OLED, GEOMETRY_128_64);

void displayInit() {
  // Vext powers the OLED on Heltec boards (active LOW)
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(50);

  pinMode(RST_OLED, OUTPUT);
  digitalWrite(RST_OLED, LOW);
  delay(20);
  digitalWrite(RST_OLED, HIGH);

  oled.init();
  oled.flipScreenVertically();
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
}

void displayMessage(const char* line1, const char* line2) {
  oled.clear();
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0, 0, DISPLAY_TITLE);
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, 26, line1);
  oled.drawString(0, 40, line2);
  oled.display();
}

static String formatUptime(unsigned long ms) {
  unsigned long s = ms / 1000;
  char buf[16];
  if (s < 86400) {
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
  } else {
    snprintf(buf, sizeof(buf), "%lud %02luh", s / 86400, (s / 3600) % 24);
  }
  return String(buf);
}

void displayUpdate(const DisplayStatus& s) {
  oled.clear();

  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawString(0, 0, DISPLAY_TITLE);
  oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  oled.drawString(128, 0, s.timeValid ? s.time : formatUptime(s.uptimeMs));
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.drawHorizontalLine(0, 12, 128);

  oled.drawString(0, 14, String(s.apMode ? "AP: " : "WLAN: ") + s.ssid);
  oled.drawString(0, 26, s.ip);
  oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  if (s.apMode) {
    oled.drawString(128, 26, String(s.apClients) + " Clients");
  } else {
    oled.drawString(128, 26, String(s.rssi) + " dBm");
  }
  oled.setTextAlignment(TEXT_ALIGN_LEFT);

  oled.drawHorizontalLine(0, 40, 128);
  String dmx;
  if (!s.dmxReady) {
    dmx = "DMX: FEHLER";
  } else if (s.dmxActive) {
    dmx = "DMX: aktiv";
  } else {
    dmx = "DMX: Pause (Zeitpl.)";
  }
  oled.drawString(0, 42, dmx);
  if (s.dmxReady) {
    oled.setTextAlignment(TEXT_ALIGN_RIGHT);
    oled.drawString(128, 42, String(s.fps) + " fps");
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }
  if (s.timeValid) {
    oled.drawString(0, 53, "Up " + formatUptime(s.uptimeMs));
  }

  oled.display();
}
#endif
