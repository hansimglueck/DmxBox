#pragma once
#include <Arduino.h>

#define LINK_MAX_PACKET 255

// Transport between boxes. LoRa today; ESP-NOW can implement the same interface.
class Link {
public:
  virtual ~Link() {}
  virtual bool begin() = 0;
  virtual bool ready() const = 0;
  // Queues/starts a transmission. Returns false if busy or over budget.
  virtual bool send(const uint8_t* buf, size_t len) = 0;
  // Returns the length of a received packet (0 if none).
  virtual size_t poll(uint8_t* buf, size_t maxLen, float& rssi, float& snr) = 0;
  virtual bool busy() const = 0;
  virtual float airtimePercent() const = 0;  // share of the last hour spent transmitting
  virtual const char* name() const = 0;
};
