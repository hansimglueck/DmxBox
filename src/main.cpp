#include "Arduino.h"
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include <esp_dmx.h>
#include <OTAWrapper.h>
#include <time.h>  // Add time library

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
byte data[DMX_PACKET_SIZE];
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

// Fixture configuration
#define NUM_FIXTURES 10
#define CHANNELS_PER_FIXTURE 16
#define EEPROM_DMX_START 0
#define EEPROM_FIXTURE_START 512
#define EEPROM_WIFI_SSID_START 532
#define EEPROM_WIFI_PASSWORD_START 596
#define EEPROM_TIME_START_HOUR 660  // 532 + 64 + 64
#define EEPROM_TIME_START_MINUTE 661
#define EEPROM_TIME_END_HOUR 662
#define EEPROM_TIME_END_MINUTE 663
#define EEPROM_TIME_ENABLED 664
#define EEPROM_CHANNEL_NAMES_START 665
#define FIXTURE_CHANNEL_NAME_LEN 12
// 10 fixtures * 16 channels * 12 bytes = 1920 bytes -> end at 2585

struct Fixture {
  char name[10];
  uint16_t startAddress;
};

Fixture fixtures[NUM_FIXTURES];
char fixtureChannelNames[NUM_FIXTURES][CHANNELS_PER_FIXTURE][FIXTURE_CHANNEL_NAME_LEN];

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

// Time control settings
int dmxStartHour = 0;
int dmxStartMinute = 0;
int dmxEndHour = 0;
int dmxEndMinute = 0;
bool timeControlEnabled = false;

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
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
    while (!getLocalTime(&timeinfo, 0) && timeout < 10) {
      delay(500);
      Serial.print(".");
      timeout++;
      esp_task_wdt_reset();
    }

    if (getLocalTime(&timeinfo, 0)) {
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
  if (!getLocalTime(&timeinfo, 0)) {
    return true;
  }
  
  int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int startMinutes = dmxStartHour * 60 + dmxStartMinute;
  int endMinutes = dmxEndHour * 60 + dmxEndMinute;
  
  // Handle overnight periods (e.g., 22:00 to 06:00)
  if (startMinutes <= endMinutes) {
    return currentMinutes >= startMinutes && currentMinutes < endMinutes;
  } else {
    return currentMinutes >= startMinutes || currentMinutes < endMinutes;
  }
}

void loadTimeControl() {
  dmxStartHour = EEPROM.read(EEPROM_TIME_START_HOUR);
  dmxStartMinute = EEPROM.read(EEPROM_TIME_START_MINUTE);
  dmxEndHour = EEPROM.read(EEPROM_TIME_END_HOUR);
  dmxEndMinute = EEPROM.read(EEPROM_TIME_END_MINUTE);
  timeControlEnabled = EEPROM.read(EEPROM_TIME_ENABLED) == 1;
  
  // Validate ranges
  if (dmxStartHour > 23) dmxStartHour = 0;
  if (dmxStartMinute > 59) dmxStartMinute = 0;
  if (dmxEndHour > 23) dmxEndHour = 0;
  if (dmxEndMinute > 59) dmxEndMinute = 0;
}

void saveTimeControl() {
  EEPROM.write(EEPROM_TIME_START_HOUR, dmxStartHour);
  EEPROM.write(EEPROM_TIME_START_MINUTE, dmxStartMinute);
  EEPROM.write(EEPROM_TIME_END_HOUR, dmxEndHour);
  EEPROM.write(EEPROM_TIME_END_MINUTE, dmxEndMinute);
  EEPROM.write(EEPROM_TIME_ENABLED, timeControlEnabled ? 1 : 0);
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


void sendFixturesPage(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->print("<!DOCTYPE html><html>");
  response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  response->print("<link rel=\"icon\" href=\"data:,\">");
  response->print("<base target='_self'>");
  response->print("<style>");
  response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
  response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
  response->print("h1 { color: #4CAF50; }");
  response->print(".info { background-color: #2a2a2a; padding: 15px; margin: 20px; border-radius: 8px; border: 1px solid #555; }");
  response->print(".fixture { margin: 20px; padding: 15px; border: 2px solid #4CAF50; background-color: #2a2a2a; border-radius: 8px; }");
  response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 11px 15px;");
  response->print("text-decoration: none; font-size: 20px; margin: 8px; cursor: pointer; border-radius: 4px;}");
  response->print(".button:hover { background-color: #45a049; }");
  response->print(".button-save { background-color: #ff9800; margin: 20px; padding: 15px 30px; font-size: 24px; }");
  response->print(".button-save:hover { background-color: #e68900; }");
  response->print(".button-reset { background-color: #f44336; margin: 20px; padding: 15px 30px; font-size: 24px; }");
  response->print(".button-reset:hover { background-color: #da190b; }");
  response->print(".button-wifi { background-color: #2196F3; margin: 20px; padding: 15px 30px; font-size: 24px; }");
  response->print(".button-wifi:hover { background-color: #0b7dda; }");
  response->print("input { font-size: 18px; padding: 8px; width: 80px; background-color: #333; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; }");
  response->print("a { color: #4CAF50; text-decoration: none; }");
  
  // Connection indicator styles
  response->print("#connection-indicator { position: fixed; top: 10px; right: 10px; padding: 8px 15px;");
  response->print("background-color: #2a2a2a; border-radius: 20px; border: 2px solid #4CAF50;");
  response->print("font-size: 14px; z-index: 1000; display: flex; align-items: center; gap: 8px; }");
  response->print(".status-dot { width: 10px; height: 10px; border-radius: 50%; background-color: #4CAF50; }");
  response->print(".status-dot.disconnected { background-color: #f44336; animation: pulse 1s infinite; }");
  response->print("@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }");
  response->print("</style>");
  
  // JavaScript für dynamische Updates und Connection Monitoring
  response->print("<script>");
  response->print("let reconnectAttempts = 0;");
  response->print("const maxReconnectAttempts = 5;");
  response->print("let isConnected = true;");
  
  response->print("function updateConnectionStatus(connected, message) {");
  response->print("  const indicator = document.getElementById('connection-indicator');");
  response->print("  const dot = indicator.querySelector('.status-dot');");
  response->print("  const text = indicator.querySelector('span');");
  response->print("  if (connected) {");
  response->print("    dot.classList.remove('disconnected');");
  response->print("    text.textContent = message || 'Connected';");
  response->print("    indicator.style.borderColor = '#4CAF50';");
  response->print("  } else {");
  response->print("    dot.classList.add('disconnected');");
  response->print("    text.textContent = message || 'Disconnected';");
  response->print("    indicator.style.borderColor = '#f44336';");
  response->print("  }");
  response->print("}");
  
  response->print("function updateStatus() {");
  response->print("  fetch('/status')");
  response->print("    .then(response => {");
  response->print("      if (!response.ok) throw new Error('Server error');");
  response->print("      return response.json();");
  response->print("    })");
  response->print("    .then(data => {");
  response->print("      document.getElementById('status-box').innerHTML = data.html;");
  response->print("      reconnectAttempts = 0;");
  response->print("      if (!isConnected) {");
  response->print("        isConnected = true;");
  response->print("        updateConnectionStatus(true, 'Connected');");
  response->print("      }");
  response->print("    })");
  response->print("    .catch(error => {");
  response->print("      isConnected = false;");
  response->print("      reconnectAttempts++;");
  response->print("      if (reconnectAttempts <= maxReconnectAttempts) {");
  response->print("        updateConnectionStatus(false, 'Reconnecting... (' + reconnectAttempts + '/' + maxReconnectAttempts + ')');");
  response->print("      } else {");
  response->print("        updateConnectionStatus(false, 'Disconnected - Reload page');");
  response->print("      }");
  response->print("    });");
  response->print("}");
  
  response->print("setInterval(updateStatus, 5000);"); // Check every 5 seconds
  response->print("updateStatus();"); // Initial call
  response->print("</script>");
  
  response->print("</head>");
  response->print("<body>");
  
  // Connection indicator
  response->print("<div id='connection-indicator'>");
  response->print("<div class='status-dot'></div>");
  response->print("<span>Connected</span>");
  response->print("</div>");
  
  response->print("<h1>DMX Fixtures</h1>");
  
  // WiFi Status anzeigen - wird dynamisch aktualisiert
  response->print("<div id='status-box' class='info'>");
  response->print("Loading status...");
  response->print("</div>");
  
  response->print("<div style='margin: 30px;'>");
  response->print("<a href='/saveall'><button class='button button-save'>💾 Save All to EEPROM</button></a>");
  response->print("<a href='/resetconfirm'><button class='button button-reset'>🔄 Reset EEPROM</button></a>");
  response->print("<a href='/wifisetup'><button class='button button-wifi'>📶 WiFi Setup</button></a>");
  response->print("<a href='/timesetup'><button class='button button-wifi'>⏰ Time Setup</button></a>");
  response->print("</div>");
  
  for (int i = 0; i < NUM_FIXTURES; i++) {
    response->printf("<div class='fixture'>");
    response->printf("<h2><a href='/fixture?id=%d'>%s</a></h2>", i, fixtures[i].name);
    response->printf("<form action='/setaddress' method='get'>");
    response->printf("<input type='hidden' name='id' value='%d'>", i);
    response->printf("Start Address: <input type='number' name='addr' value='%d' min='0' max='512'>", fixtures[i].startAddress);
    response->printf("<button class='button' type='submit'>Save</button>");
    response->printf("</form></div>");
  }
  
  response->print("</body></html>");
  request->send(response);  // DIESER AUFRUF FEHLTE!
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
  
  for (int i=0; i<=511; i++) {  
    if (dmxActive) {
      dmxBuffer[i+1] = data[i+1];
    } else {
      dmxBuffer[i+1] = 0;  // All channels to 0 when inactive
    }
  }
  
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

void initFixtures() {
  for (int i = 0; i < NUM_FIXTURES; i++) {
    sprintf(fixtures[i].name, "Fix%d", i + 1);
    // Read start address from EEPROM (2 bytes per fixture)
    uint16_t addr = EEPROM.read(EEPROM_FIXTURE_START + i * 2) | 
                    (EEPROM.read(EEPROM_FIXTURE_START + i * 2 + 1) << 8);
    fixtures[i].startAddress = (addr == 0xFFFF) ? 0 : addr;  // Default 0 if uninitialized
  }
}

void saveFixtureAddresses() {
  for (int i = 0; i < NUM_FIXTURES; i++) {
    EEPROM.write(EEPROM_FIXTURE_START + i * 2, fixtures[i].startAddress & 0xFF);
    EEPROM.write(EEPROM_FIXTURE_START + i * 2 + 1, (fixtures[i].startAddress >> 8) & 0xFF);
  }
  EEPROM.commit();
  Serial.println("Fixture addresses saved");
}

void saveEEPROM() {
  // Save all 512 DMX channels
  for (int i = 1; i <= 512; i++) {  
    EEPROM.write(EEPROM_DMX_START + i, data[i]);
  }
  EEPROM.commit();
  Serial.println("All 512 DMX channels saved to EEPROM"); 
}

void resetEEPROM() {
  Serial.println("Resetting EEPROM - Setting all DMX channels to 0...");
  
  // Reset all 512 DMX channels to 0
  for (int i = 1; i <= 512; i++) {  
    data[i] = 0;
    EEPROM.write(EEPROM_DMX_START + i, 0);
  }
  
  EEPROM.commit();
  Serial.println("EEPROM reset complete - All DMX channels set to 0");
}

void resetChannelNames(int fixtureId) {
  for (int ch = 0; ch < CHANNELS_PER_FIXTURE; ch++) {
    snprintf(fixtureChannelNames[fixtureId][ch], FIXTURE_CHANNEL_NAME_LEN, "Ch %d", ch + 1);
  }
}

void loadChannelNames() {
  for (int fix = 0; fix < NUM_FIXTURES; fix++) {
    for (int ch = 0; ch < CHANNELS_PER_FIXTURE; ch++) {
      int addr = EEPROM_CHANNEL_NAMES_START + (fix * CHANNELS_PER_FIXTURE + ch) * FIXTURE_CHANNEL_NAME_LEN;
      char* name = fixtureChannelNames[fix][ch];
      for (int i = 0; i < FIXTURE_CHANNEL_NAME_LEN; i++) {
        name[i] = EEPROM.read(addr + i);
      }
      name[FIXTURE_CHANNEL_NAME_LEN - 1] = 0;
      if (name[0] == 0 || (uint8_t)name[0] == 0xFF) {
        resetChannelNames(fix);
      }
    }
  }
}

void saveChannelNames(int fixtureId) {
  for (int ch = 0; ch < CHANNELS_PER_FIXTURE; ch++) {
    int addr = EEPROM_CHANNEL_NAMES_START + (fixtureId * CHANNELS_PER_FIXTURE + ch) * FIXTURE_CHANNEL_NAME_LEN;
    for (int i = 0; i < FIXTURE_CHANNEL_NAME_LEN; i++) {
      EEPROM.write(addr + i, fixtureChannelNames[fixtureId][ch][i]);
    }
  }
  EEPROM.commit();
}

bool connectToWiFi(const char* ssid, const char* password, int maxAttempts = 20) {
  if (strlen(ssid) == 0) {
    Serial.println("No WiFi SSID configured for connection");
    return false;
  }
  
  Serial.printf("Attempting to connect to WiFi: %s\n", ssid);
  // Don't set mode here - will be set in setup
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
  
  // Configure watchdog timer FIRST (10 seconds timeout)
  esp_task_wdt_init(10, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog timer configured (10s timeout)");
  
  // initialize EEPROM with predefined size BEFORE reading WiFi config
  EEPROM.begin(3000);
  Serial.println("EEPROM initialized (3000 bytes)");
  
  // Load time control settings
  loadTimeControl();
  Serial.printf("Time control: %s, Active: %02d:%02d - %02d:%02d\n",
                timeControlEnabled ? "ENABLED" : "DISABLED",
                dmxStartHour, dmxStartMinute, dmxEndHour, dmxEndMinute);
  
  // Reset watchdog before WiFi operations
  esp_task_wdt_reset();
  
  // Load WiFi config from EEPROM
  loadWiFiConfig(savedSSID, savedPassword);

  bool wifiConnected = false;
  if (strlen(savedSSID) > 0) {
    Serial.printf("Connecting to: %s\n", savedSSID);
    WiFi.mode(WIFI_STA);
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

  esp_task_wdt_reset();  // Reset after WiFi setup complete

  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  esp_task_wdt_reset();  // Reset after SPIFFS

  // DMX Setup - FIRST install driver, THEN set pins
  Serial.println("Installing DMX driver...");
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  esp_err_t err = dmx_driver_install(dmxPort, &config, NULL, 0);
  if (err == ESP_OK) {
    Serial.printf("DMX driver installed successfully on port %d\n", dmxPort);
  } else {
    Serial.printf("ERROR: DMX driver installation failed with code %d\n", err);
  }
  
  Serial.printf("Setting DMX pins: TX=%d, RX=%d, DIR=%d\n", 
                DMX_SERIAL_OUTPUT_PIN, DMX_SERIAL_INPUT_PIN, DMX_DIRECTION_PIN);
  err = dmx_set_pin(dmxPort, DMX_SERIAL_OUTPUT_PIN, DMX_SERIAL_INPUT_PIN, DMX_DIRECTION_PIN);
  if (err == ESP_OK) {
    Serial.println("DMX pins set successfully");
  } else {
    Serial.printf("ERROR: DMX pin setup failed with code %d\n", err);
  }

  Serial.println("setup dmx complete");

  esp_task_wdt_reset();  // Reset after DMX setup

  pinMode(2, OUTPUT);

  // Load all 512 DMX channels from EEPROM
  for (int i = 1; i <= 512; i++) {  
    data[i] = EEPROM.read(EEPROM_DMX_START + i);
    
    // Reset watchdog every 128 channels
    if (i % 128 == 0) {
      esp_task_wdt_reset();
    }
  }
  Serial.println("Loaded 512 DMX channels from EEPROM");
  
  initFixtures();
  loadChannelNames();
  Serial.println("Fixtures initialized:");

  Serial.println("setup ap complete");

  esp_task_wdt_reset();  // Reset before setting up server routes

  //more handlers...
  // Route for root / web page - now shows fixtures list
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    sendFixturesPage(request);
  });
  
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    
    String html = "";

    if (!apModeActive && WiFi.status() == WL_CONNECTED) {
      html += "📡 <strong style='color: #4CAF50;'>WiFi: STA</strong><br>";
      html += "Network: " + WiFi.SSID() + "<br>";
      html += "IP: <strong>" + WiFi.localIP().toString() + "</strong><br>";
      html += "Signal: " + String(WiFi.RSSI()) + " dBm<br>";

      if (timeInitialized) {
        html += "<br>🕐 <strong>Time:</strong> " + getFormattedTime();

        if (timeControlEnabled) {
          bool isActive = isDMXActiveTime();
          html += "<br>📡 <strong>DMX:</strong> <span style='color: ";
          html += isActive ? "#4CAF50" : "#f44336";
          html += ";'>";
          html += isActive ? "ACTIVE" : "INACTIVE";
          html += "</span>";
          char timeStr[32];
          sprintf(timeStr, "<br>⏰ Active: %02d:%02d - %02d:%02d",
                  dmxStartHour, dmxStartMinute, dmxEndHour, dmxEndMinute);
          html += timeStr;
        } else {
          html += "<br>📡 <strong>DMX:</strong> <span style='color: #4CAF50;'>ALWAYS ACTIVE</span>";
        }
      }
    } else {
      html += "📶 <strong style='color: #2196F3;'>WiFi: AP</strong><br>";
      html += "Network: " + String(ssid) + "<br>";
      html += "IP: <strong>" + WiFi.softAPIP().toString() + "</strong><br>";
    }

    html += "<br>Hostname: <strong>" + String(ssid) + ".local</strong>";
    
    // JSON response with HTML content
    response->print("{\"html\":\"");
    // Escape quotes in HTML
    html.replace("\"", "\\\"");
    response->print(html);
    response->print("\"}");
    
    request->send(response);
  });
  
  server.on("/saveall", HTTP_GET, [](AsyncWebServerRequest *request){
    saveEEPROM();
    saveFixtureAddresses();
    request->redirect("/");
  });
  
  server.on("/resetconfirm", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\">");
    response->print("<base target='_self'>");  // Add this line
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
    response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
    response->print("h1 { color: #f44336; }");
    response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 15px 30px;");
    response->print("text-decoration: none; font-size: 24px; margin: 20px; cursor: pointer; display: inline-block; border-radius: 4px;}");
    response->print(".button:hover { background-color: #45a049; }");
    response->print(".button-danger { background-color: #f44336; }");
    response->print(".button-danger:hover { background-color: #da190b; }");
    response->print("p { color: #e0e0e0; }");
    response->print("</style></head>");
    response->print("<body><h1>⚠️ Confirm Reset</h1>");
    response->print("<p style='font-size: 24px; margin: 40px;'>Reset ALL DMX channels to 0?<br>This cannot be undone!</p>");
    response->print("<a href='/reseteeprom'><button class='button button-danger'>Yes, Reset Everything</button></a><br>");
    response->print("<a href='/'><button class='button'>Cancel</button></a>");
    response->print("</body></html>");
    request->send(response);
  });
  
  server.on("/reseteeprom", HTTP_GET, [](AsyncWebServerRequest *request){
    resetEEPROM();
    request->redirect("/");
  });
  
  server.on("/wifisetup", HTTP_GET, [](AsyncWebServerRequest *request){
    // Show scanning page with auto-redirect
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<meta http-equiv='refresh' content='3;url=/wifiscan'>");
    response->print("<link rel=\"icon\" href=\"data:,\">");
    response->print("<base target='_self'>");
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
    response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
    response->print("h1 { color: #2196F3; }");
    response->print(".spinner { border: 8px solid #333; border-top: 8px solid #2196F3; border-radius: 50%;");
    response->print("width: 60px; height: 60px; animation: spin 1s linear infinite; margin: 40px auto; }");
    response->print("@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }");
    response->print("p { color: #aaa; }");
    response->print("</style></head>");
    response->print("<body><h1>📶 WiFi Setup</h1>");
    response->print("<div class='spinner'></div>");
    response->print("<h2>Scanning for networks...</h2>");
    response->print("<p>Please wait, this may take a few seconds.</p>");
    response->print("</body></html>");
    request->send(response);
  });
  
  server.on("/wifiscan", HTTP_GET, [](AsyncWebServerRequest *request){
    // Feed the watchdog before long operation
    esp_task_wdt_reset();
    
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\">");
    response->print("<base target='_self'>");
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
    response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
    response->print("h1 { color: #2196F3; }");
    response->print(".network { margin: 15px; padding: 15px; border: 1px solid #555; background-color: #2a2a2a; border-radius: 4px; }");
    response->print(".network:hover { background-color: #333; border-color: #2196F3; }");
    response->print(".network a { display: block; color: #e0e0e0; text-decoration: none; }");
    response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 15px 30px;");
    response->print("text-decoration: none; font-size: 20px; margin: 20px; cursor: pointer; border-radius: 4px; display: inline-block;}");
    response->print("input { font-size: 18px; padding: 10px; width: 250px; background-color: #333; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; margin: 10px; }");
    response->print("a { color: #4CAF50; text-decoration: none; }");
    response->print(".signal { float: right; color: #4CAF50; }");
    response->print("</style></head>");
    response->print("<body><h1>📶 WiFi Networks</h1>");
    response->print("<p><a href='/'>← Back to Fixtures</a></p>");
    
    // Scan for networks with watchdog reset
    response->print("<div style='margin: 30px;'>");
    response->print("<h2>Available Networks:</h2>");
    
    Serial.println("Starting WiFi scan...");
    esp_task_wdt_reset();  // Reset before scan
    
    // Async scan with timeout
    int n = WiFi.scanNetworks(false, false, false, 300);  // 300ms per channel max
    
    esp_task_wdt_reset();  // Reset after scan
    Serial.printf("Scan complete, found %d networks\n", n);
    
    if (n == 0) {
      response->print("<p>No networks found.</p>");
      response->print("<p><a href='/wifisetup'><button class='button'>Scan Again</button></a></p>");
    } else {
      for (int i = 0; i < n && i < 20; i++) {  // Limit to 20 networks
        esp_task_wdt_reset();  // Reset for each network
        
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String signalStrength = (rssi > -50) ? "████" : (rssi > -60) ? "███░" : (rssi > -70) ? "██░░" : "█░░░";
        bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        
        response->printf("<div class='network'>");
        response->printf("<a href='/wifiselect?ssid=%s&open=%d'>", ssid.c_str(), isOpen ? 1 : 0);
        response->printf("<span>%s</span>", ssid.c_str());
        response->printf("<span class='signal'>%s %ddBm</span>", signalStrength.c_str(), rssi);
        if (isOpen) response->print(" <span style='color: #ff9800;'>🔓</span>");
        response->print("</a>");
        response->print("</div>");
      }
    }
    
    response->print("</div>");
    
    // Manual entry form
    response->print("<div style='margin: 30px;'>");
    response->print("<h2>Manual Entry:</h2>");
    response->print("<form action='/wifiselect' method='get'>");
    response->print("<input type='text' name='ssid' placeholder='SSID' required><br>");
    response->print("<input type='hidden' name='open' value='0'>");
    response->print("<button class='button' type='submit'>Next</button>");
    response->print("</form>");
    response->print("</div>");
    
    response->print("</body></html>");
    
    esp_task_wdt_reset();  // Reset before sending
    request->send(response);
  });
  
  server.on("/wifiselect", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("ssid")) {
      request->redirect("/wifisetup");
      return;
    }
    
    String selectedSSID = request->getParam("ssid")->value();
    bool isOpen = request->hasParam("open") && request->getParam("open")->value() == "1";
    
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\">");
    response->print("<base target='_self'>");
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
    response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
    response->print("h1 { color: #2196F3; }");
    response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 15px 30px;");
    response->print("text-decoration: none; font-size: 20px; margin: 20px; cursor: pointer; border-radius: 4px;}");
    response->print("input { font-size: 18px; padding: 10px; width: 250px; background-color: #333; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; margin: 10px; }");
    response->print("a { color: #4CAF50; text-decoration: none; }");
    response->print("</style></head>");
    response->print("<body><h1>Enter Password</h1>");
    response->print("<p><a href='/wifisetup'>← Back to Networks</a></p>");
    
    response->print("<div style='margin: 30px;'>");
    response->printf("<h2>Network: %s</h2>", selectedSSID.c_str());
    
    if (isOpen) {
      response->print("<p style='color: #ff9800;'>This is an open network (no password required)</p>");
    }
    
    response->print("<form action='/wificonnect' method='get'>");
    response->printf("<input type='hidden' name='ssid' value='%s'>", selectedSSID.c_str());
    
    if (isOpen) {
      response->print("<input type='hidden' name='password' value=''>");
      response->print("<button class='button' type='submit'>Connect Now</button>");
    } else {
      response->print("<input type='password' name='password' placeholder='Password' required autofocus><br>");
      response->print("<button class='button' type='submit'>Connect</button>");
    }
    
    response->print("</form>");
    response->print("</div>");
    
    response->print("</body></html>");
    request->send(response);
  });
  
  server.on("/wificonnect", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid")) {
      String newSSID = request->getParam("ssid")->value();
      String newPassword = request->hasParam("password") ? request->getParam("password")->value() : "";
      
      // Save to EEPROM
      saveWiFiConfig(newSSID.c_str(), newPassword.c_str());
      
      // Send response
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->print("<!DOCTYPE html><html>");
      response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
      response->print("<link rel=\"icon\" href=\"data:,\">");
      response->print("<base target='_self'>");
      response->print("<style>");
      response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
      response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
      response->print("h1 { color: #2196F3; }");
      response->print("</style></head>");
      response->print("<body><h1>✅ WiFi Configuration Saved</h1>");
      response->printf("<p>SSID: %s</p>", newSSID.c_str());
      response->print("<p>Device will restart in 3 seconds...</p>");
      response->print("<p>If connection succeeds, the device will be available on your network.</p>");
      response->print("<p>If connection fails, AP mode will start again.</p>");
      response->print("</body></html>");
      request->send(response);
      
      // Restart after 3 seconds
      delay(3000);
      ESP.restart();
    } else {
      request->redirect("/wifisetup");
    }
  });
  
  server.on("/timesetup", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\">");
    response->print("<base target='_self'>");
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
    response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
    response->print("h1 { color: #2196F3; }");
    response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 15px 30px;");
    response->print("text-decoration: none; font-size: 20px; margin: 20px; cursor: pointer; border-radius: 4px;}");
    response->print("input[type='number'] { font-size: 18px; padding: 10px; width: 80px; background-color: #333; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; margin: 5px; }");
    response->print("input[type='checkbox'] { width: 30px; height: 30px; margin: 10px; }");
    response->print("label { display: block; margin: 20px; }");
    response->print("a { color: #4CAF50; text-decoration: none; }");
    response->print("</style></head>");
    response->print("<body><h1>⏰ Time Control Setup</h1>");
    response->print("<p><a href='/'>← Back to Fixtures</a></p>");
    
    response->print("<form action='/timesave' method='get'>");
    response->print("<div style='margin: 30px;'>");
    
    response->print("<label>");
    response->print("<input type='checkbox' name='enabled' value='1'");
    if (timeControlEnabled) response->print(" checked");
    response->print("> Enable Time Control");
    response->print("</label>");
    
    response->print("<h2>DMX Active Time</h2>");
    response->print("<label>Start Time:");
    response->printf("<input type='number' name='start_h' value='%d' min='0' max='23'>", dmxStartHour);
    response->print(":");
    response->printf("<input type='number' name='start_m' value='%d' min='0' max='59'>", dmxStartMinute);
    response->print("</label>");
    
    response->print("<label>End Time:");
    response->printf("<input type='number' name='end_h' value='%d' min='0' max='23'>", dmxEndHour);
    response->print(":");
    response->printf("<input type='number' name='end_m' value='%d' min='0' max='59'>", dmxEndMinute);
    response->print("</label>");
    
    response->print("<p style='color: #aaa;'>DMX output will be active between Start and End time.<br>");
    response->print("Outside this time, all DMX channels will be set to 0.</p>");
    
    response->print("<button class='button' type='submit'>Save Settings</button>");
    response->print("</div>");
    response->print("</form>");
    
    response->print("</body></html>");
    request->send(response);
  });
  
  server.on("/timesave", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("start_h") && request->hasParam("start_m") &&
        request->hasParam("end_h") && request->hasParam("end_m")) {
      
      dmxStartHour = request->getParam("start_h")->value().toInt();
      dmxStartMinute = request->getParam("start_m")->value().toInt();
      dmxEndHour = request->getParam("end_h")->value().toInt();
      dmxEndMinute = request->getParam("end_m")->value().toInt();
      timeControlEnabled = request->hasParam("enabled");
      
      // Validate
      if (dmxStartHour > 23) dmxStartHour = 23;
      if (dmxStartMinute > 59) dmxStartMinute = 59;
      if (dmxEndHour > 23) dmxEndHour = 23;
      if (dmxEndMinute > 59) dmxEndMinute = 59;
      
      saveTimeControl();
    }
    request->redirect("/");
  });
  
  server.on("/setaddress", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("id") && request->hasParam("addr")) {
      int id = request->getParam("id")->value().toInt();
      int addr = request->getParam("addr")->value().toInt();
      if (id >= 0 && id < NUM_FIXTURES) {
        fixtures[id].startAddress = addr;
        saveFixtureAddresses();
      }
    }
    request->redirect("/");
  });
  
  server.on("/fixture", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("id")) {
      request->redirect("/");
      return;
    }
    
    int fixtureId = request->getParam("id")->value().toInt();
    if (fixtureId < 0 || fixtureId >= NUM_FIXTURES) {
      request->redirect("/");
      return;
    }
    
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\">");
    response->print("<base target='_self'>");  // Add this line
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
    response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
    response->print("h1, h2 { color: #4CAF50; }");
    response->print(".channel { margin: 10px; padding: 10px; border: 1px solid #555; background-color: #2a2a2a; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; }");
    response->print(".channel:hover { background-color: #333; }");
    response->print(".channel a { text-decoration: none; color: #4CAF50; flex: 1; }");
    response->print(".ch-name { color: #aaa; font-size: 16px; min-width: 80px; text-align: left; }");
    response->print(".ch-val { font-weight: bold; min-width: 40px; text-align: right; }");
    response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 11px 15px; font-size: 18px; margin: 8px; cursor: pointer; border-radius: 4px; }");
    response->print(".button-edit { background-color: #2196F3; }");
    response->print("a { text-decoration: none; color: #4CAF50; }");
    response->print("p { color: #aaa; }");
    response->print("</style></head>");
    response->printf("<body><h1>%s</h1>", fixtures[fixtureId].name);
    response->print("<p><a href='/'>← Back</a></p>");
    response->printf("<a href='/fixturenames?id=%d'><button class='button button-edit'>Edit Channel Names</button></a>", fixtureId);

    uint16_t startAddr = fixtures[fixtureId].startAddress;
    for (int i = 0; i < CHANNELS_PER_FIXTURE; i++) {
      uint16_t dmxChannel = startAddr + i;
      if (dmxChannel > 0 && dmxChannel <= 512) {
        response->printf("<div class='channel'>");
        response->printf("<span class='ch-name'>%s</span>", fixtureChannelNames[fixtureId][i]);
        response->printf("<a href='/channel?fix=%d&ch=%d'>DMX %d</a>", fixtureId, i, dmxChannel);
        response->printf("<span class='ch-val'>%d</span>", data[dmxChannel]);
        response->printf("</div>");
      }
    }

    response->print("</body></html>");
    request->send(response);
  });
  
  server.on("/channel", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("fix") || !request->hasParam("ch")) {
      request->redirect("/");
      return;
    }
    
    int fixtureId = request->getParam("fix")->value().toInt();
    int channelOffset = request->getParam("ch")->value().toInt();
    int delta = request->hasParam("delta") ? request->getParam("delta")->value().toInt() : 0;
    
    if (fixtureId < 0 || fixtureId >= NUM_FIXTURES || channelOffset < 0 || channelOffset >= CHANNELS_PER_FIXTURE) {
      request->redirect("/");
      return;
    }
    
    uint16_t dmxChannel = fixtures[fixtureId].startAddress + channelOffset;
    if (dmxChannel > 0 && dmxChannel <= 512) {
      if (request->hasParam("val")) {
        int newVal = constrain(request->getParam("val")->value().toInt(), 0, 255);
        data[dmxChannel] = newVal;
      } else if (delta != 0) {
        int newVal = constrain((int)data[dmxChannel] + delta, 0, 255);
        data[dmxChannel] = newVal;
      }
    }

    if (request->hasParam("name")) {
      String newName = request->getParam("name")->value();
      strncpy(fixtureChannelNames[fixtureId][channelOffset], newName.c_str(), FIXTURE_CHANNEL_NAME_LEN - 1);
      fixtureChannelNames[fixtureId][channelOffset][FIXTURE_CHANNEL_NAME_LEN - 1] = 0;
      saveChannelNames(fixtureId);
    }

    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\"><base target='_self'>");
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; margin: 0; padding: 0; }");
    response->print("html { font-family: Helvetica, Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; font-size: 20px;}");
    response->print("h1, h2 { color: #4CAF50; }");
    response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 15px 20px;");
    response->print("font-size: 20px; margin: 5px; cursor: pointer; display: inline-block; min-width: 80px; border-radius: 4px;}");
    response->print(".button:hover { background-color: #45a049; }");
    response->print(".button-name { background-color: #2196F3; padding: 10px 15px; font-size: 18px; }");
    response->print("input[type=text] { font-size: 18px; padding: 8px; width: 180px; background-color: #333; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; }");
    response->print("a { text-decoration: none; color: #4CAF50; }");
    response->print("p { color: #aaa; }");
    response->print("h1 { cursor: pointer; }");
    response->print("h1:hover { color: #45a049; }");
    response->print("</style></head>");
    response->printf("<body><h1 onclick=\"document.getElementById('rf').style.display='block';document.getElementById('rn').select();\">%s</h1>", fixtureChannelNames[fixtureId][channelOffset]);
    response->printf("<form id='rf' action='/channel' method='get' style='display:none; margin:10px;'>");
    response->printf("<input type='hidden' name='fix' value='%d'>", fixtureId);
    response->printf("<input type='hidden' name='ch' value='%d'>", channelOffset);
    response->printf("<input id='rn' type='text' name='name' value='%s' maxlength='%d'>", fixtureChannelNames[fixtureId][channelOffset], FIXTURE_CHANNEL_NAME_LEN - 1);
    response->print("<button class='button button-name' type='submit'>OK</button>");
    response->print("</form>");
    response->printf("<h2>DMX %d</h2>", dmxChannel);
    response->printf("<h2 onclick=\"document.getElementById('vf').style.display='block';document.getElementById('vn').select();\" style='cursor:pointer;'>%d</h2>", data[dmxChannel]);
    response->printf("<form id='vf' action='/channel' method='get' style='display:none; margin:10px;'>");
    response->printf("<input type='hidden' name='fix' value='%d'>", fixtureId);
    response->printf("<input type='hidden' name='ch' value='%d'>", channelOffset);
    response->printf("<input id='vn' type='number' name='val' value='%d' min='0' max='255' style='font-size:18px;padding:8px;width:100px;background-color:#333;color:#e0e0e0;border:1px solid #555;border-radius:4px;'>", data[dmxChannel]);
    response->print("<button class='button button-name' type='submit'>OK</button>");
    response->print("</form>");
    response->printf("<p><a href='/fixture?id=%d'>← Back</a></p>", fixtureId);
    
    response->print("<div style='margin: 30px;'>");
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=-255'><button class='button'>0</button></a>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=255'><button class='button'>255</button></a><br>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=-50'><button class='button'>-50</button></a>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=-10'><button class='button'>-10</button></a>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=-5'><button class='button'>-5</button></a>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=-1'><button class='button'>-1</button></a><br>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=50'><button class='button'>+50</button></a>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=10'><button class='button'>+10</button></a>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=5'><button class='button'>+5</button></a>", fixtureId, channelOffset);
    response->printf("<a href='/channel?fix=%d&ch=%d&delta=1'><button class='button'>+1</button></a>", fixtureId, channelOffset);
    response->print("</div>");
    
    response->print("</body></html>");
    request->send(response);
  });
  
  server.on("/fixturenames", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("id")) { request->redirect("/"); return; }
    int fixtureId = request->getParam("id")->value().toInt();
    if (fixtureId < 0 || fixtureId >= NUM_FIXTURES) { request->redirect("/"); return; }

    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\"><base target='_self'>");
    response->print("<style>");
    response->print("body { background-color: #1a1a1a; color: #e0e0e0; font-family: Helvetica, Arial, sans-serif; text-align: center; font-size: 20px; }");
    response->print("h1 { color: #2196F3; }");
    response->print(".row { display: flex; align-items: center; justify-content: center; margin: 8px; gap: 10px; }");
    response->print(".ch-num { color: #aaa; width: 50px; text-align: right; font-size: 16px; }");
    response->print("input { font-size: 18px; padding: 8px; width: 200px; background-color: #333; color: #e0e0e0; border: 1px solid #555; border-radius: 4px; }");
    response->print(".button { border: none; color: white; padding: 12px 25px; font-size: 20px; margin: 10px; cursor: pointer; border-radius: 4px; }");
    response->print(".button-save { background-color: #4CAF50; }");
    response->print(".button-reset { background-color: #f44336; }");
    response->print("a { color: #4CAF50; text-decoration: none; }");
    response->print(".button-preset { background-color: #555; padding: 8px 14px; font-size: 16px; margin: 4px; }");
    response->print(".button-preset:hover { background-color: #777; }");
    response->print("</style>");
    response->print("<script>");
    response->print("const presets={");
    response->print("'SharpyPlus':['Cyna','Magenta','Yellow','Colour1','Colour2','Colour3','Strobe','Dimmer','Dimmer Fine','Gobo','Animation','Ani Rot','Gobo2','Gobo2 Rot','Rot Fine','4Prism'],");
    response->print("'Beam330':['Color','Strobe','Dimmer','Gobo','Gobo Rot','Prism','Prism Rot','Zoom1','Zoom2','Pan','Pan Fine','Tilt','Tilt Fine','Focus','Frost','Lamp Ctrl'],");
    response->print("'Beam230':['Pan','Tilt','Pan Fine','Tilt Fine','Dimmer','Strobe','Frost','Focus','Gobo','Gobo Shake','Color','??','Prism','Prism Rot','??','??'],");
    response->print("'Beam230neu':['Color','Strobe','Dimmer','Gobo','Prism','Prism Rot','PrismRot2','Bunt','Focus','Pan','Pan Fine','Tilt','Tilt Fine','PT Time','Reset','Lamp Ctrl'],");
    response->print("'Beam230v21':['Pan','Tilt','Pan Fine','Tilt Fine','XY Speed','Zoom','Strobe','Dimmer','Color','Gobo','Gobo2','Gobo Rot','Focus','Prism','Prism Rot','Reset']");
    response->print("};");
    response->print("function applyPreset(name){const p=presets[name];for(let i=0;i<16;i++){const el=document.querySelector('input[name=\"n'+i+'\"]');if(el)el.value=p[i]||'';}};");
    response->print("</script>");
    response->print("</head>");
    response->printf("<body><h1>%s – Channel Names</h1>", fixtures[fixtureId].name);
    response->printf("<p><a href='/fixture?id=%d'>← Back</a></p>", fixtureId);

    response->print("<div style='margin:10px;'>");
    response->print("<span style='color:#aaa;font-size:16px;'>Preset: </span>");
    response->print("<button class='button button-preset' onclick='applyPreset(\"SharpyPlus\")'>SharpyPlus</button>");
    response->print("<button class='button button-preset' onclick='applyPreset(\"Beam330\")'>Beam330</button>");
    response->print("<button class='button button-preset' onclick='applyPreset(\"Beam230\")'>Beam230</button>");
    response->print("<button class='button button-preset' onclick='applyPreset(\"Beam230neu\")'>Beam230neu</button>");
    response->print("<button class='button button-preset' onclick='applyPreset(\"Beam230v21\")'>Beam230v21</button>");
    response->print("</div>");

    response->printf("<form action='/savechannelnames' method='get'>");
    response->printf("<input type='hidden' name='id' value='%d'>", fixtureId);
    for (int i = 0; i < CHANNELS_PER_FIXTURE; i++) {
      response->printf("<div class='row'><span class='ch-num'>Ch %d</span>", i + 1);
      response->printf("<input type='text' name='n%d' value='%s' maxlength='%d'></div>",
                       i, fixtureChannelNames[fixtureId][i], FIXTURE_CHANNEL_NAME_LEN - 1);
    }
    response->print("<br><button class='button button-save' type='submit'>Save Names</button>");
    response->print("</form>");
    response->printf("<a href='/resetchannelnames?id=%d'><button class='button button-reset'>Reset to Defaults</button></a>", fixtureId);
    response->print("</body></html>");
    request->send(response);
  });

  server.on("/savechannelnames", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("id")) { request->redirect("/"); return; }
    int fixtureId = request->getParam("id")->value().toInt();
    if (fixtureId < 0 || fixtureId >= NUM_FIXTURES) { request->redirect("/"); return; }

    for (int i = 0; i < CHANNELS_PER_FIXTURE; i++) {
      String key = "n" + String(i);
      if (request->hasParam(key)) {
        String val = request->getParam(key)->value();
        strncpy(fixtureChannelNames[fixtureId][i], val.c_str(), FIXTURE_CHANNEL_NAME_LEN - 1);
        fixtureChannelNames[fixtureId][i][FIXTURE_CHANNEL_NAME_LEN - 1] = 0;
      }
    }
    saveChannelNames(fixtureId);
    request->redirect("/fixture?id=" + String(fixtureId));
  });

  server.on("/resetchannelnames", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("id")) { request->redirect("/"); return; }
    int fixtureId = request->getParam("id")->value().toInt();
    if (fixtureId < 0 || fixtureId >= NUM_FIXTURES) { request->redirect("/"); return; }

    resetChannelNames(fixtureId);
    saveChannelNames(fixtureId);
    request->redirect("/fixture?id=" + String(fixtureId));
  });

  // Keep old routes for backward compatibility
  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request){
    saveEEPROM();
    request->redirect("/");
  });
  
  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("HTTP_GET /style.css");
    request->send(SPIFFS, "/style.css", "text/css");
  });

  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP

  server.begin();
  Serial.println("=== Setup Complete - Web Server Started ===\n");
  
  esp_task_wdt_reset();  // Final reset after setup
}

void loop(){
  static unsigned long loopCounter = 0;
  static unsigned long lastLoopDebug = 0;
  static unsigned long lastTimeSync = 0;
  
  // Reset watchdog at start of loop
  esp_task_wdt_reset();
  
  processOTA();

  // Process DNS only in AP mode
  if (apModeActive) {
    dnsServer.processNextRequest();
  }
  
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
