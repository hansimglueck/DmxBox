#include "Arduino.h"
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <esp_dmx.h>
#include <OTAWrapper.h>
#include <time.h>  // Add time library
#include "display.h"
#include "fixtures.h"
#include "presets.h"
#include "cues.h"
#include "system.h"
#include "fixture_web.h"
#include "cue_web.h"
#ifdef HAS_LORA
#include "sync.h"
#include "sync_web.h"
#endif

// Fallback if DMX_DEFAULT_INTR_FLAGS is not defined in esp_dmx library
#ifndef DMX_DEFAULT_INTR_FLAGS
#define DMX_DEFAULT_INTR_FLAGS 0
#endif

// DMX configuration from platformio.ini
#ifndef DMX_SERIAL_OUTPUT_PIN
#define DMX_SERIAL_OUTPUT_PIN 17
#endif
#ifndef DMX_SERIAL_INPUT_PIN
#define DMX_SERIAL_INPUT_PIN 16
#endif
#ifndef DMX_DIRECTION_PIN
#define DMX_DIRECTION_PIN 15
#endif
#ifndef DMX_PORT
#define DMX_PORT 1
#endif

dmx_port_t dmxPort = DMX_PORT;
bool dmxReady = false;
byte data[DMX_PACKET_SIZE];
portMUX_TYPE dmxDataMux = portMUX_INITIALIZER_UNLOCKED;
byte dmxBuffer[DMX_PACKET_SIZE];
unsigned long lastUpdate = millis();

long lastLampToggle = millis();

// uint8_t data[DMX_MAX_FRAME];

// Replace with your network credentials
#ifndef WIFI_SSID
#define WIFI_SSID "nutantPlus23"  // Fallback if not defined in platformio.ini
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "12345678"  // Fallback if not defined in platformio.ini
#endif

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

char savedSSID[64] = {0};
char savedPassword[64] = {0};
bool apModeActive = false;
unsigned long lastWiFiCheck = 0;

const byte DNS_PORT = 53;
DNSServer dnsServer;

// Set web server port number to 80
AsyncWebServer server(80);

/* Put IP Address details */
IPAddress local_ip(4,3,2,1);
IPAddress gateway(4,3,2,1);
IPAddress subnet(255,255,255,0);

const char* PARAM_CHAN = "chan";
const char* PARAM_DELTA = "delta";
const char* PARAM_ACTION = "action";

// Auxiliar variables to store the current output state
String output26State = "off";
String output27State = "off";
// Assign output variables to GPIO pins
const int output26 = 26;
const int output27 = 27;

//SharpyPlus
#define CHANNELS 31

// EEPROM layout (fixtures and channel names now live in LittleFS, see fixtures.cpp)
#define EEPROM_DMX_START 0
#define EEPROM_WIFI_SSID_START 532
#define EEPROM_WIFI_PASSWORD_START 596
#define EEPROM_TIME_START_HOUR 660  // 532 + 64 + 64
#define EEPROM_TIME_START_MINUTE 661
#define EEPROM_TIME_END_HOUR 662
#define EEPROM_TIME_END_MINUTE 663
#define EEPROM_TIME_ENABLED 664
#define EEPROM_WEEKDAY_TIME_START 665
#define EEPROM_WEEKDAY_TIME_ENABLED 693

String channel_names[CHANNELS] = {
  "Cyna", //1  
  "Magenta", //2
  "Yellow", //3 
  "Colour1",  //4   
  "Colour2", //5
  "Colour3",  //6 
  "Strobe", //7
  "Dimmer",  //8 
  "Dimmer Fine ", //9 
  "Gobo", //10 
  "Animation", //11 
  "Ani Rot", //12
  "Gobo2",  //13 
  "Gobo2 Rot",  //14
  "Rot Fine", //15 
  "4Prism", //16 
  "4Prism Rot", //17 
  "8Prism", //18 
  "8Prism Rot", //19 
  "Frost", //20 
  "Zoom", //21 
  "Focus", //22 
  "Focus Fine", //23 
  "Beam Mode", //24 
  "Pan", //25 
  "Pan Fine", //26 
  "Tilt", //27 
  "Tilt Fine", //28 
  "Function", //29 
  "Reset", //30 
  "Lamp Control" //31 
};
int lampOnValue=108;
int lampOffValue=42;
int controlChannel=31;
int panChannel=25;
int panFineChannel=26;
int tiltChannel=27;
int tiltFineChannel=28;


// //Beam330
// #define CHANNELS 18
// const char* ssid     = "nutant330-23";
// String channel_names[CHANNELS] = {
//   "Color", //1
//   "Strobe", //2
//   "Dimmer", //3 
//   "Gobo",  //4   35=nutant
//   "Gobo Rot", //5 17 ca senkrecht
//   "Prism",  //6 
//   "Prism Rot", //7
//   "Zoom1",  //8 
//   "Zoom2", //9 
//   "Pan", //10 
//   "Pan Fine", //11 
//   "Tilt", //12
//   "Tilt Fine",  //13 
//   "Focus",  //14
//   "Frost", //15 
//   "Lamp Ctrl", //16  139 lamp on, 0->13??->0 lamp off???
//   "PT Time", //17
//   "Reset" //18  255 für paar sekunden
//   };
// int lampOnValue=139;
// int lampOffValue=13;
// int controlChannel=16;
// int panChannel=10;
// int panFineChannel=11;
// int tiltChannel=12;
// int tiltFineChannel=13; 


// //Beam230
// #define CHANNELS 16
// const char* ssid     = "nutant230x";
// String channel_names[CHANNELS] = {
//   "Pan", //1
//   "Tilt", //2
//   "Pan Fine", //3 
//   "Tilt Fine",  //4
//   "Dimmer", //5
//   "Strobe",  //6 - has to be on for dimm 
//   "Frost", //7
//   "Focus",  //8 
//   "Gobo", //9   84 rect
//   "Gobo Shake", //10 
//   "Color", //11 
//   "??", //12
//   "Prism",  //13 
//   "Prism Rot", //14
//   "??", //15 PT-Speed?
//   "??", //16
//   };
// int lampOnValue=201;
// int lampOffValue=105;
// int controlChannel=16;
// int panChannel=1;
// int panFineChannel=3;
// int tiltChannel=2;
// int tiltFineChannel=4; 


// //Beam230neu
// #define CHANNELS 18
// const char* ssid     = "nutant230b";
// String channel_names[CHANNELS] = {
//   "Color", //1  10 red, 20 yellow, 30 med blue, 40 green poison, 50 orange, 60 violet, 70 yellow warmer, 80 light green, 90 uv, 100 rose, 110 violet white, 120 frosty cold blue, 130 cto, 140 white
//   "Strobe", //2
//   "Dimmer", //3 
//   "Gobo",  //4   62 rect
//   "Prism", //5
//   "Prism Rot",  //6 
//   "? Prism Rot", //7
//   "Bunt",  //8 
//   "Focus ", //9 
//   "Pan", //10 
//   "Pan Fine", //11 
//   "Tilt", //12
//   "Tilt Fine",  //13 
//   "PT Time",  //14
//   "Reset", //15 
//   "Lamp Ctrl", //16  201=lamp on
//   "??", //17
//   "??" //18
// };
// int lampOnValue=205;
// int lampOffValue=105;
// int controlChannel=16;
// int panChannel=10;
// int panFineChannel=11;
// int tiltChannel=12;
// int tiltFineChannel=13;


// //Beam230v21
// #define CHANNELS 16
// const char* ssid     = "china-white2";
// String channel_names[CHANNELS] = {
//   "Pan", //1  
//   "Tilt", //2
//   "Pan Fine", //3 
//   "Tilt Fine",  //4   
//   "XY Speed", //5
//   "Zoom",  //6 
//   "Strobe", //7
//   "Dimmer",  //8 
//   "Color ", //9 
//   "Gobo", //10 
//   "Gobo2", //11 
//   "Gobo Rot", //12
//   "Focus", //13 
//   "Prism",  //14
//   "Prism Rot", //15 
//   "Reset" //16  201=lamp on, 101 Lamp off, 255 Reset
// };
// int lampOnValue=201;
// int lampOffValue=101;
// int controlChannel=16;
// int panChannel=1;
// int panFineChannel=3;
// int tiltChannel=2;
// int tiltFineChannel=4;


/*/
/Channels
#define CHANNELS 20
const char* ssid     = "SchleppiDMX";
String channel_names[CHANNELS] = {
  "Chan1", //1  10 red, 20 yellow, 30 med blue, 40 green poison, 50 orange, 60 violet, 70 yellow warmer, 80 light green, 90 uv, 100 rose, 110 violet white, 120 frosty cold blue, 130 cto, 140 white
  "Chan2", //2
  "Chan3", //3 
  "Chan4",  //4   
  "Chan5", //5
  "Chan6",  //6 
  "Chan7", //7
  "Chan8",  //8 
  "Chan9 ", //9 
  "Chan10", //10 
  "Chan11", //11 
  "Chan12", //12
  "Chan13",  //13 
  "Chan14",  //14
  "Chan15", //15 
  "Chan16", //15 
  "Chan17", //15 
  "Chan18", //15 
  "Chan19", //15 
  "Chan20" //20  201=lamp on, 101 Lamp off, 255 Reset
};
int lampOnValue=201;
int lampOffValue=101;
int controlChannel=16;
int panChannel=1;
int panFineChannel=3;
int tiltChannel=2;
int tiltFineChannel=4;
*/

double values[CHANNELS];

String lampState;

// NTP Time settings - MUST BE BEFORE sendFixturesPage()
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // GMT+1 for Central Europe
const int daylightOffset_sec = 3600;  // Daylight saving time
bool timeInitialized = false;

// Time control settings - Weekday arrays (0=Monday, 1=Tuesday, ..., 6=Sunday)
uint8_t dmxWeekdayStartHour[7] = {0, 0, 0, 0, 0, 0, 0};
uint8_t dmxWeekdayStartMinute[7] = {0, 0, 0, 0, 0, 0, 0};
uint8_t dmxWeekdayEndHour[7] = {0, 0, 0, 0, 0, 0, 0};
uint8_t dmxWeekdayEndMinute[7] = {0, 0, 0, 0, 0, 0, 0};
bool timeControlEnabled = false;


bool localTimeNow(struct tm& t) {
  // getLocalTime(&t, 0) skips the read whenever millis() ticks during the call
  time_t now;
  time(&now);
  localtime_r(&now, &t);
  return t.tm_year > (2016 - 1900);
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!localTimeNow(timeinfo)) {
    return "Time not set";
  }
  
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M:%S", &timeinfo);
  return String(timeStr);
}

void initTime() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Initializing NTP time sync...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait up to 5 seconds for time sync
    int timeout = 0;
    struct tm timeinfo;
    while (!localTimeNow(timeinfo) && timeout < 10) {
      delay(500);
      Serial.print(".");
      timeout++;
      esp_task_wdt_reset();
    }

    if (localTimeNow(timeinfo)) {
      timeInitialized = true;
      Serial.println("\nTime synchronized: " + getFormattedTime());
    } else {
      Serial.println("\nFailed to sync time");
    }
  }
}

bool isDMXActiveTime() {
  if (!timeInitialized || !timeControlEnabled) {
    return true;  // Always active if no time control
  }
  
  struct tm timeinfo;
  if (!localTimeNow(timeinfo)) {
    return true;
  }
  
  // Map tm_wday (0=Sunday, 1=Monday, ..., 6=Saturday) to our array (0=Monday, ..., 6=Sunday)
  int weekdayIndex = (timeinfo.tm_wday + 6) % 7;
  
  int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int startMinutes = dmxWeekdayStartHour[weekdayIndex] * 60 + dmxWeekdayStartMinute[weekdayIndex];
  int endMinutes = dmxWeekdayEndHour[weekdayIndex] * 60 + dmxWeekdayEndMinute[weekdayIndex];
  
  // Handle overnight periods (e.g., 22:00 to 06:00)
  if (startMinutes <= endMinutes) {
    return currentMinutes >= startMinutes && currentMinutes < endMinutes;
  } else {
    return currentMinutes >= startMinutes || currentMinutes < endMinutes;
  }
}

void loadTimeControl() {
  // Load weekday-specific time control (28 bytes for 7 days × 4 values)
  for (int day = 0; day < 7; day++) {
    int addr = EEPROM_WEEKDAY_TIME_START + (day * 4);
    dmxWeekdayStartHour[day] = EEPROM.read(addr);
    dmxWeekdayStartMinute[day] = EEPROM.read(addr + 1);
    dmxWeekdayEndHour[day] = EEPROM.read(addr + 2);
    dmxWeekdayEndMinute[day] = EEPROM.read(addr + 3);
    
    // Validate values
    if (dmxWeekdayStartHour[day] > 23) dmxWeekdayStartHour[day] = 0;
    if (dmxWeekdayStartMinute[day] > 59) dmxWeekdayStartMinute[day] = 0;
    if (dmxWeekdayEndHour[day] > 23) dmxWeekdayEndHour[day] = 0;
    if (dmxWeekdayEndMinute[day] > 59) dmxWeekdayEndMinute[day] = 0;
  }
  
  timeControlEnabled = EEPROM.read(EEPROM_WEEKDAY_TIME_ENABLED) == 1;
}

void saveTimeControl() {
  // Save weekday-specific time control (28 bytes for 7 days × 4 values)
  for (int day = 0; day < 7; day++) {
    int addr = EEPROM_WEEKDAY_TIME_START + (day * 4);
    EEPROM.write(addr, dmxWeekdayStartHour[day]);
    EEPROM.write(addr + 1, dmxWeekdayStartMinute[day]);
    EEPROM.write(addr + 2, dmxWeekdayEndHour[day]);
    EEPROM.write(addr + 3, dmxWeekdayEndMinute[day]);
  }
  
  EEPROM.write(EEPROM_WEEKDAY_TIME_ENABLED, timeControlEnabled ? 1 : 0);
  EEPROM.commit();
  Serial.println("Time control settings saved");
}

// Replaces placeholder with Lamp state value
String processor(const String& var){
  Serial.println(var);
  if(var == "STATE"){
    if(data[controlChannel]==lampOnValue){
      lampState = "ON";
    }
    else{
      lampState = "OFF";
    }
    Serial.println(lampState);
    return lampState;
  }
  return String();
}


class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {
    Serial.println("CaptiveRequestHandler->handleRequest()");
    sendFixturesPage(request);
  }
};

void copyDataToDMXBuffer() {
  static unsigned long lastDebug = 0;
  
  bool dmxActive = isDMXActiveTime();
  
  portENTER_CRITICAL(&dmxDataMux);
  for (int i=0; i<=511; i++) {  
    if (dmxActive) {
      dmxBuffer[i+1] = data[i+1];
    } else {
      dmxBuffer[i+1] = 0;  // All channels to 0 when inactive
    }
  }
  portEXIT_CRITICAL(&dmxDataMux);

  // if data[ctrlChannel] == lampOnValue then toggle dmxBuffer[ctrlChannel] every 6 seconds between 0 and lampOnValue
  if (dmxActive && data[controlChannel] == lampOnValue) {
    if (millis() - lastLampToggle > 6000) {
      lastLampToggle = millis();
      if (dmxBuffer[controlChannel] == 0) {
        dmxBuffer[controlChannel] = lampOnValue;
        Serial.println("Lamp toggle: ON");
      } else {
        dmxBuffer[controlChannel] = 0;
        Serial.println("Lamp toggle: OFF");
      }
    }
  }
  
  // Debug output every 50 seconds
  if (millis() - lastDebug > 50000) {
    lastDebug = millis();
    Serial.println("=== DMX Buffer Status ===");
    for (int i = 1; i <= min(10, CHANNELS); i++) {
      Serial.printf("CH%d: %d ", i, dmxBuffer[i]);
    }
    Serial.println("\n========================");
  }
}

volatile bool restartRequested = false;
volatile uint16_t loopFps = 0;

bool connectToWiFi(const char* ssid, const char* password, int maxAttempts = 20) {
  if (strlen(ssid) == 0) {
    Serial.println("No WiFi SSID configured for connection");
    return false;
  }
  
  Serial.printf("Attempting to connect to WiFi: %s\n", ssid);
  // Don't set mode here - will be set in setup
  
  // ESP32-C3 needs a small delay before WiFi.begin()
  delay(100);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Reset watchdog every 2 attempts (every ~1 second)
    if (attempts % 2 == 0) {
      esp_task_wdt_reset();
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi connection failed!");
    return false;
  }
}

void saveWiFiConfig(const char* ssid, const char* password) {
  // Save SSID (max 64 chars)
  for (int i = 0; i < 64; i++) {
    if (i < strlen(ssid)) {
      EEPROM.write(EEPROM_WIFI_SSID_START + i, ssid[i]);
    } else {
      EEPROM.write(EEPROM_WIFI_SSID_START + i, 0);
    }
  }
  
  // Save Password (max 64 chars)
  for (int i = 0; i < 64; i++) {
    if (i < strlen(password)) {
      EEPROM.write(EEPROM_WIFI_PASSWORD_START + i, password[i]);
    } else {
      EEPROM.write(EEPROM_WIFI_PASSWORD_START + i, 0);
    }
  }
  
  EEPROM.commit();
  Serial.println("WiFi credentials saved to EEPROM");
}

void loadWiFiConfig(char* ssid, char* password) {
  // Load SSID
  for (int i = 0; i < 64; i++) {
    ssid[i] = EEPROM.read(EEPROM_WIFI_SSID_START + i);
    if (ssid[i] == 0) break;
  }
  ssid[63] = 0; // Ensure null termination
  
  // Load Password
  for (int i = 0; i < 64; i++) {
    password[i] = EEPROM.read(EEPROM_WIFI_PASSWORD_START + i);
    if (password[i] == 0) break;
  }
  password[63] = 0; // Ensure null termination
  
  // Validate SSID - check for uninitialized EEPROM (0xFF) or non-printable characters
  bool ssidValid = true;
  int ssidLen = strlen(ssid);
  if (ssidLen == 0 || ssid[0] == (char)0xFF) {
    ssidValid = false;
  } else {
    for (int i = 0; i < ssidLen; i++) {
      if ((unsigned char)ssid[i] == 0xFF || ssid[i] < 32 || ssid[i] > 126) {
        ssidValid = false;
        break;
      }
    }
  }
  
  if (!ssidValid) {
    Serial.println("WARNING: EEPROM WiFi config corrupted or uninitialized - resetting to defaults");
    ssid[0] = 0;
    password[0] = 0;
  }
}

void switchToAPMode() {
  Serial.println("Starting AP mode...");
  apModeActive = true;

  WiFi.mode(WIFI_AP);
  delay(100);

  WiFi.softAP(ssid, password);

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("AP active - IP: %s\n", apIP.toString().c_str());

  dnsServer.stop();
  dnsServer.start(DNS_PORT, "*", apIP);
}

void setup(){
  // initialize digital pin LED_BUILTIN as an output.
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== DMX Box Starting ===");
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());
  displayInit();
  displayMessage("Starting...");

#ifdef HOSTNAME_MAC_SUFFIX
  // several boxes share one firmware: make hostname / AP SSID unique per board
  static char uniqueName[48];
  uint64_t mac = ESP.getEfuseMac();
  snprintf(uniqueName, sizeof(uniqueName), "%s-%02x%02x", WIFI_SSID,
           (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
  ssid = uniqueName;
#endif

  // Mount before the watchdog starts: the first-time format can take several seconds
  fixturesBegin();
  applyStoredNetworkName();
  Serial.printf("Network name: %s\n", ssid);

  // Configure watchdog timer (10 seconds timeout)
  esp_task_wdt_init(10, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog timer configured (10s timeout)");
  
  
  // initialize EEPROM with predefined size BEFORE reading WiFi config
  EEPROM.begin(3000);
  Serial.println("EEPROM initialized (3000 bytes)");
  
  // Load time control settings
  loadTimeControl();
  Serial.printf("Time control: %s (weekday-based schedules loaded)\n",
                timeControlEnabled ? "ENABLED" : "DISABLED");
  
  // Reset watchdog before WiFi operations
  esp_task_wdt_reset();
  
  // Load WiFi config from EEPROM
  loadWiFiConfig(savedSSID, savedPassword);

  bool wifiConnected = false;
  if (strlen(savedSSID) > 0) {
    Serial.printf("Connecting to: %s\n", savedSSID);
    displayMessage("Connecting to WiFi:", savedSSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(ssid);
    wifiConnected = connectToWiFi(savedSSID, savedPassword);
  }

  esp_task_wdt_reset();

  if (wifiConnected) {
    apModeActive = false;
    Serial.printf("Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    initTime();
  } else {
    Serial.println("No WiFi, starting AP mode");
    switchToAPMode();
  }
  
  // Setup OTA with hostname
  setupOTA(ssid, password);
  Serial.printf("Hostname: %s.local\n", ssid);

  // DMX Setup
  Serial.printf("DMX: port=%d TX=%d RX=%d DIR=%d\n",
                dmxPort, DMX_SERIAL_OUTPUT_PIN, DMX_SERIAL_INPUT_PIN, DMX_DIRECTION_PIN);
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  bool dmxOk = dmx_driver_install(dmxPort, &config, NULL, 0);
  if (dmxOk) {
    if (!dmx_set_pin(dmxPort, DMX_SERIAL_OUTPUT_PIN, DMX_SERIAL_INPUT_PIN, DMX_DIRECTION_PIN)) {
      Serial.println("DMX pin setup failed");
    } else {
      dmxReady = true;
      Serial.println("DMX ready");
    }
  } else {
    Serial.println("DMX driver install failed");
  }
  Serial.println("setup dmx complete");

#ifndef HAS_LORA
  pinMode(2, OUTPUT);  // GPIO2 is the LoRa PA enable on the Heltec V4
#endif

  // DMX values in EEPROM are only read for the one-time migration of the old snapshot into a cue
  bool allErased = true;
  for (int i = 1; i <= 512; i++) {
    data[i] = EEPROM.read(EEPROM_DMX_START + i);
    if (data[i] != 0xFF) allErased = false;

    // Reset watchdog every 128 channels
    if (i % 128 == 0) {
      esp_task_wdt_reset();
    }
  }
  // A fresh board reads erased flash (0xFF): never start with every channel at full
  if (allErased) {
    memset(data + 1, 0, 512);
    Serial.println("EEPROM uninitialized, DMX channels start at 0");
  } else {
    Serial.println("Loaded 512 DMX channels from EEPROM");
  }
  
  fixturesLoad();
  presetsLoad();
  cuesBegin();
#ifdef HAS_LORA
  syncBegin(ssid);
#endif

  Serial.println("setup ap complete");

  registerFixtureRoutes(server);
#ifdef HAS_LORA
  registerSyncRoutes(server);
#endif

  registerSystemRoutes(server);
  registerCueRoutes(server);

  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP

  server.begin();
  Serial.println("=== Setup Complete - Web Server Started ===\n");
}

void loop(){
  static unsigned long loopCounter = 0;
  static unsigned long lastLoopDebug = 0;
  static unsigned long lastTimeSync = 0;
  
  // Reset watchdog at start of loop
  esp_task_wdt_reset();

  // restart requested by the web interface: give the HTTP response time to go out first
  static unsigned long restartAt = 0;
  if (restartRequested && !restartAt) restartAt = millis() + 1500;
  if (restartAt && (long)(millis() - restartAt) >= 0) ESP.restart();
  
  processOTA();
#ifdef HAS_LORA
  syncLoop();
#endif

  // Process DNS only in AP mode
  if (apModeActive) {
    dnsServer.processNextRequest();
  }
  
  cuesTick();
  copyDataToDMXBuffer();
  
  dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);
  dmx_write(dmxPort, dmxBuffer, DMX_PACKET_SIZE);
  dmx_send(dmxPort);
  
  loopCounter++;
  
  // Re-sync time every hour if connected
  if (WiFi.status() == WL_CONNECTED && (millis() - lastTimeSync > 3600000)) {
    lastTimeSync = millis();
    initTime();
  }

  // STA-Verbindung überwachen, bei Verlust → AP
  if (!apModeActive && millis() - lastWiFiCheck > 30000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost, switching to AP mode");
      switchToAPMode();
    }
  }
  
  static unsigned long lastDisplayUpdate = 0;
  static unsigned long framesAtLastDisplay = 0;
  if (millis() - lastDisplayUpdate >= 1000) {
    unsigned long now = millis();
    DisplayStatus st;
    st.apMode = apModeActive;
    st.ssid = apModeActive ? ssid : savedSSID;
    st.ip = (apModeActive ? WiFi.softAPIP() : WiFi.localIP()).toString();
    st.rssi = WiFi.RSSI();
    st.apClients = WiFi.softAPgetStationNum();
    st.dmxReady = dmxReady;
    st.dmxActive = isDMXActiveTime();
    st.fps = (loopCounter - framesAtLastDisplay) * 1000 / (now - lastDisplayUpdate);
    loopFps = st.fps;
    struct tm t;
    st.timeValid = timeInitialized && localTimeNow(t);
    if (st.timeValid) {
      char buf[9];
      strftime(buf, sizeof(buf), "%H:%M:%S", &t);
      st.time = buf;
    }
    st.uptimeMs = now;
#ifdef HAS_LORA
    st.linkPeers = syncOnlinePeers();
#endif
    st.boxName = localBoxName();
    displayUpdate(st);
    framesAtLastDisplay = loopCounter;
    lastDisplayUpdate = now;
  }

  // Debug output every 10 seconds
  if (millis() - lastLoopDebug > 10000) {
    lastLoopDebug = millis();
    Serial.printf("Loop count: %lu, Uptime: %lu ms\n", loopCounter, millis());
    Serial.printf("DMX sent on port %d, packet size: %d\n", dmxPort, DMX_PACKET_SIZE);
    if (timeInitialized) {
      Serial.printf("Current time: %s\n", getFormattedTime().c_str());
    }
  }
  
  delay(10);
}
