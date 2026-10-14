#pragma once
#ifdef HAS_LORA
#include "link.h"

class LoRaLink : public Link {
public:
  bool begin() override;
  bool ready() const override { return ok; }
  bool send(const uint8_t* buf, size_t len) override;
  size_t poll(uint8_t* buf, size_t maxLen, float& rssi, float& snr) override;
  bool busy() const override { return transmitting; }
  float airtimePercent() const override;
  const char* name() const override { return "LoRa"; }

  void setTxPower(int outputDbm);  // power at the antenna connector incl. PA gain
  int txPower() const { return txPowerDbm; }
  const char* femName() const { return femKct ? "KCT8103L" : "GC1109"; }
  uint32_t txCount = 0, rxCount = 0, rxErrors = 0, budgetDrops = 0;

private:
  bool ok = false;
  bool femKct = false;
  volatile bool transmitting = false;
  int txPowerDbm = 14;
  uint32_t txStartedMs = 0;
  // airtime per minute over the last hour, in milliseconds
  mutable uint32_t airtimeMs[60] = {};
  mutable uint32_t airtimeMinute = 0;
  void femTx(bool tx);
  void rollAirtime() const;
  uint32_t airtimeLastHourMs() const;
};

extern LoRaLink loraLink;
#endif
