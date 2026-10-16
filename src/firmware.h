#pragma once
#include <Arduino.h>

// Firmware updates from the GitHub releases (latest.json + firmware-<env>.bin, see
// .github/workflows/release.yml). Checks run in the background while the box is connected to a
// network with internet; installing is only started by hand (web page or another box via LoRa).
// Only with HAS_FW_UPDATE (boards with large enough app partitions); otherwise just the version.

enum FwState : uint8_t { FW_IDLE, FW_CHECKING, FW_UPTODATE, FW_AVAILABLE, FW_DOWNLOADING, FW_DONE, FW_ERROR };

// beacon flags
enum : uint8_t { FW_FLAG_INTERNET = 1, FW_FLAG_AVAILABLE = 2, FW_FLAG_UPDATING = 4 };

struct FirmwareStatus {
  FwState state = FW_IDLE;
  String latest;             // newest released version, "" = unknown
  uint32_t latestCode = 0;
  int progress = 0;          // percent while downloading
  String error;              // last check / install error
  uint32_t lastCheckMs = 0;  // 0 = never checked successfully
};

const char* firmwareVersion();   // "1.2.0", "1.2.0-3-gd15a417-dirty", ...
const char* firmwareEnv();       // PlatformIO environment, e.g. "heltec-v4"
uint32_t firmwareVersionCode();  // major << 16 | minor << 8 | patch of the last release this build is based on
uint32_t versionCode(const String& v);
String versionString(uint32_t code);

void firmwareBegin();
void firmwareLoop();  // from loop(): schedules checks, shows the download on the display
FirmwareStatus firmwareStatus();
void firmwareCheckNow();
// Checks once more and installs a newer release. "" = started, otherwise why not.
String firmwareInstall();
bool firmwareBusy();  // check or download running
bool firmwareHasInternet();
bool firmwareUpdateAvailable();
uint8_t firmwareFlags();  // FW_FLAG_*
