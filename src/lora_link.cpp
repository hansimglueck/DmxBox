#ifdef HAS_LORA
#include "lora_link.h"
#include <RadioLib.h>
#include <SPI.h>

// Heltec WiFi LoRa 32 V4 (pins as in the Meshtastic heltec_v4 variant)
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define LORA_RST 12
#define LORA_BUSY 13
#define LORA_DIO1 14
#define LORA_PA_POWER 7
#define LORA_PA_CSD 2       // GC1109 PA_EN / KCT8103L CSD
#define LORA_GC1109_TX 46   // GC1109 PA_TX_EN
#define LORA_KCT8103L_TX 5  // KCT8103L CTX

// EU868 sub-band g3 (869.4–869.65 MHz): 10 % duty cycle, 500 mW ERP
static const float LORA_FREQ = 869.525;
static const float LORA_BW = 250.0;
static const uint8_t LORA_SF = 7;
static const uint8_t LORA_CR = 5;
static const uint8_t LORA_SYNC = 0x12;
static const uint32_t AIRTIME_BUDGET_MS = 3600UL * 1000UL * 8 / 100;  // stay below the 10 % limit
static const int MAX_OUTPUT_DBM = 27;
static const int PA_GAIN_DB = 10;  // approx. FEM gain (GC1109 7–11 dB, KCT8103L 7–13 dB)

LoRaLink loraLink;
static SPIClass loraSpi(HSPI);
static SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSpi);
static volatile bool dio1Flag = false;
static void IRAM_ATTR onDio1() { dio1Flag = true; }

static int chipPower(int outputDbm) {
  return constrain(outputDbm - PA_GAIN_DB, -9, 22);
}

void LoRaLink::femTx(bool tx) {
  digitalWrite(femKct ? LORA_KCT8103L_TX : LORA_GC1109_TX, tx ? HIGH : LOW);
}

bool LoRaLink::begin() {
  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, HIGH);
  delay(5);
  // Heltec V4 ships with either FEM; the KCT8103L pulls CSD high after power-up
  pinMode(LORA_PA_CSD, INPUT);
  delay(1);
  femKct = digitalRead(LORA_PA_CSD) == HIGH;
  pinMode(LORA_PA_CSD, OUTPUT);
  digitalWrite(LORA_PA_CSD, HIGH);
  pinMode(femKct ? LORA_KCT8103L_TX : LORA_GC1109_TX, OUTPUT);
  femTx(false);

  loraSpi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int st = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, LORA_SYNC, chipPower(txPowerDbm), 8, 1.8, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa init failed: %d\n", st);
    return false;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setCurrentLimit(140);
  radio.setDio1Action(onDio1);
  st = radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa startReceive failed: %d\n", st);
    return false;
  }
  ok = true;
  Serial.printf("LoRa ready: %.3f MHz BW%.0f SF%u, FEM %s, %d dBm\n",
                LORA_FREQ, LORA_BW, LORA_SF, femName(), txPowerDbm);
  return true;
}

void LoRaLink::setTxPower(int outputDbm) {
  txPowerDbm = constrain(outputDbm, 0, MAX_OUTPUT_DBM);
  if (ok) radio.setOutputPower(chipPower(txPowerDbm));
}

void LoRaLink::rollAirtime() const {
  uint32_t nowMin = millis() / 60000;
  while (airtimeMinute < nowMin) {
    airtimeMinute++;
    airtimeMs[airtimeMinute % 60] = 0;
    if (nowMin - airtimeMinute > 60) airtimeMinute = nowMin - 60;
  }
}

uint32_t LoRaLink::airtimeLastHourMs() const {
  rollAirtime();
  uint32_t sum = 0;
  for (uint32_t v : airtimeMs) sum += v;
  return sum;
}

float LoRaLink::airtimePercent() const {
  return airtimeLastHourMs() * 100.0f / (3600.0f * 1000.0f);
}

bool LoRaLink::send(const uint8_t* buf, size_t len) {
  if (!ok || transmitting || len == 0 || len > LINK_MAX_PACKET) return false;
  uint32_t toaMs = (radio.getTimeOnAir(len) + 999) / 1000;
  if (airtimeLastHourMs() + toaMs > AIRTIME_BUDGET_MS) {
    budgetDrops++;
    return false;
  }
  femTx(true);
  dio1Flag = false;
  int st = radio.startTransmit(buf, len);
  if (st != RADIOLIB_ERR_NONE) {
    femTx(false);
    radio.startReceive();
    Serial.printf("LoRa transmit failed: %d\n", st);
    return false;
  }
  transmitting = true;
  txStartedMs = millis();
  airtimeMs[airtimeMinute % 60] += toaMs;
  txCount++;
  return true;
}

size_t LoRaLink::poll(uint8_t* buf, size_t maxLen, float& rssi, float& snr) {
  if (!ok) return 0;
  if (transmitting) {
    if (dio1Flag || millis() - txStartedMs > 2000) {
      dio1Flag = false;
      radio.finishTransmit();
      femTx(false);
      transmitting = false;
      radio.startReceive();
    }
    return 0;
  }
  if (!dio1Flag) return 0;
  dio1Flag = false;
  size_t len = radio.getPacketLength();
  if (len > maxLen) len = maxLen;
  int st = radio.readData(buf, len);
  rssi = radio.getRSSI();
  snr = radio.getSNR();
  radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    rxErrors++;
    return 0;
  }
  rxCount++;
  return len;
}
#endif
