#include "Arduino.h"
// #include <LXESP32DMX.h>
// #include "esp_task_wdt.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include <esp_dmx.h>
#include <OTAWrapper.h>


#define DMX_DIRECTION_PIN 15 
// #define DMX_DIRECTION_PIN 2 //pappbecher
#define DMX_SERIAL_OUTPUT_PIN 17
#define DMX_SERIAL_INPUT_PIN 16

// int transmitPin = 17;
// int receivePin = 16;
// int enablePin = 21;
dmx_port_t dmxPort = 1;
byte data[DMX_PACKET_SIZE];
byte dmxBuffer[DMX_PACKET_SIZE];
unsigned long lastUpdate = millis();

long lastLampToggle = millis();

// uint8_t data[DMX_MAX_FRAME];

// Replace with your network credentials
const char* password = "12345678";

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
const char* ssid     = "nutantPlus23";
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


/*
//Channels
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


/* AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain", [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
  //Write up to "maxLen" bytes into "buffer" and return the amount written.
  //index equals the amount of bytes that have been already sent
  //You will be asked for more data until 0 is returned
  //Keep in mind that you can not delay or yield waiting for more data!
  return mySource.read(buffer, maxLen);
}, processor);
response->addHeader("Server","ESP Async Web Server");
request->send(response);
 */
class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    //request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {
    Serial.println("CaptiveRequestHandler->handleRequest()");
    request->send(SPIFFS, "/index.html", String(), false);//, processor);   // mit dem template-processor kommts nicht hoch als captive-portal-page (maybe weil es "chunked" wird)
  }
};

void copyDataToDMXBuffer() {
  for (int i=0; i<=CHANNELS; i++) {  
    dmxBuffer[i+1] = data[i+1];
  }
  // if data[ctrlChannel] == lampOnValue then toggle dmxBuffer[ctrlChannel] every 6 seconds between 0 and lampOnValue
  if (data[controlChannel] == lampOnValue) {
    if (millis() - lastLampToggle > 6000) {
      lastLampToggle = millis();
      if (dmxBuffer[controlChannel] == 0) {
        dmxBuffer[controlChannel] = lampOnValue;
      } else {
        dmxBuffer[controlChannel] = 0;
      }
    }
  }
}

void saveEEPROM() {
  for (int i=0; i<=CHANNELS; i++) {  
    EEPROM.write(i+1, data[i+1]);
  }
  EEPROM.commit();
  Serial.println("State saved in flash memory"); 
}

void setup(){
  // initialize digital pin LED_BUILTIN as an output.
  Serial.begin(115200);
  Serial.print("setup");
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  setupOTA("dmxbox", "12345678");

  // initialize EEPROM with predefined size
  EEPROM.begin(CHANNELS+1);

  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  dmx_set_pin(dmxPort, DMX_SERIAL_OUTPUT_PIN, DMX_SERIAL_INPUT_PIN, DMX_DIRECTION_PIN);
  dmx_driver_install(dmxPort, DMX_DEFAULT_INTR_FLAGS);

  // from old library:
  // pinMode(DMX_DIRECTION_PIN, OUTPUT);
  // digitalWrite(DMX_DIRECTION_PIN, HIGH);
  // pinMode(DMX_SERIAL_OUTPUT_PIN, OUTPUT);
  // ESP32DMX.startOutput(DMX_SERIAL_OUTPUT_PIN);

  Serial.println("setup dmx complete");

  pinMode(2, OUTPUT);

  for (int i=0; i<=CHANNELS; i++) {  
    data[i+1] = EEPROM.read(i+1);
  }

  // Connect to Wi-Fi network with SSID and password
  Serial.print("Setting AP (Access Point)…");
  // Remove the password parameter, if you want the AP (Access Point) to be open

  // config führt zum abstürzen beim hochfahren
  //varyWiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  dnsServer.start(DNS_PORT, "*", IP);

  Serial.println("setup ap complete");

  //more handlers...
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request){
    saveEEPROM();
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    data[controlChannel]=lampOnValue;
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
   
  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    data[controlChannel]=lampOffValue;
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  server.on("/left", HTTP_GET, [](AsyncWebServerRequest *request){
    data[panFineChannel]+=10;
    if (data[panFineChannel] < 10) {
      data[panChannel]+=1;
    }
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  server.on("/right", HTTP_GET, [](AsyncWebServerRequest *request){
    data[panFineChannel]-=10;
    if (data[panFineChannel] > 245) {
      data[panChannel]-=1;
    }
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  server.on("/down", HTTP_GET, [](AsyncWebServerRequest *request){
    data[tiltFineChannel]-=10;
    if (data[tiltFineChannel] > 245) {
      data[tiltChannel]-=1;
    }
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  server.on("/up", HTTP_GET, [](AsyncWebServerRequest *request){
    data[tiltFineChannel]+=10;
    if (data[tiltFineChannel] < 10) {
      data[tiltChannel]+=1;
    }
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  server.on("/channels", HTTP_GET, [](AsyncWebServerRequest *request){
    int chan = 0;
    int delta = 0;
    if (request->hasParam(PARAM_CHAN)) {
      chan = request->getParam(PARAM_CHAN)->value().toInt();
      Serial.print("Channel");
      Serial.println(chan);
    } 
    if (request->hasParam(PARAM_DELTA)) {
      delta = request->getParam(PARAM_DELTA)->value().toInt();
      Serial.print("Delta");
      Serial.println(delta);
    }
    if (request->hasParam(PARAM_ACTION)) {
      String action = request->getParam(PARAM_ACTION)->value();
      Serial.print("Action");
      Serial.println(action);
      saveEEPROM();
    }
    if (chan != 0) {
      data[chan]+=delta;
    }
     
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    // Display the HTML web page
    response->print("<!DOCTYPE html><html>");
    response->print("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    response->print("<link rel=\"icon\" href=\"data:,\">");
    // CSS to style the on/off buttons 
    // Feel free to change the background-color and font-size attributes to fit your preferences
    response->print("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: right; font-size: 20px; line-height: 30px;}");
    response->print(".button { background-color: #4CAF50; border: none; color: white; padding: 11px 15px;");
    response->print("text-decoration: none; font-size: 20px; margin: 8px; cursor: pointer;}");
    response->print(".button2 {background-color: #555555;}</style></head>");
    int i = 1000;
    response->printf("<body>");
 
    // Display all the channels  
    for (int i = 1; i<=CHANNELS; i++) {
      response->printf("<p id='%i'>%i-%s : %i<br />",i,i,channel_names[i-1],data[i]);
    
      // Show +/-Buttons       
      response->printf("<a onclick=\"location.reload();\" href=\"/channels?chan=%i&delta=-50#%i\"><button class=\"button\">-50</button></a>",i,i);
      response->printf("<a onclick=\"location.reload();\" href=\"/channels?chan=%i&delta=-10#%i\"><button class=\"button\">-10</button></a>",i,i);
      response->printf("<a onclick=\"location.reload();\"  href=\"/channels?chan=%i&delta=-1#%i\"><button class=\"button\">-1</button></a><br />",i,i);
      response->printf("<a onclick=\"location.reload();\"  href=\"/channels?chan=%i&delta=+1#%i\"><button class=\"button\">+1</button></a>",i,i);
      response->printf("<a onclick=\"location.reload();\" href=\"/channels?chan=%i&delta=+10#%i\"><button class=\"button\">+10</button></a>",i,i);
      response->printf("<a onclick=\"location.reload();\" href=\"/channels?chan=%i&delta=+50#%i\"><button class=\"button\">+50</button></a>",i,i);
      response->print("</p>");
    }
    response->print("<p><a href=\"/save\"><button class=\"button\">SAVE is SAFE</button></a></p>");
    response->print("<div style=\"min-height: 600px\"></div>");
    response->print("</body></html>");
    request->send(response);
  });
  
  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("HTTP_GET /style.css");
    request->send(SPIFFS, "/style.css", "text/css");
  });

  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP

  server.begin();
}

// void copyDMXToOutput(void) {
//   xSemaphoreTake( ESP32DMX.lxDataLock, portMAX_DELAY );
//   for (int i=1; i<DMX_MAX_FRAME; i++) {
//       ESP32DMX.setSlot(i , data[i]);
//    }
//    xSemaphoreGive( ESP32DMX.lxDataLock );
// }

void loop(){
  processOTA();
  dnsServer.processNextRequest();
  copyDataToDMXBuffer();
  dmx_write(dmxPort, dmxBuffer, DMX_PACKET_SIZE);
  dmx_send(dmxPort, DMX_PACKET_SIZE);

  // copyDMXToOutput(); 
  // esp_task_wdt_reset();
  // vTaskDelay(100);
}
