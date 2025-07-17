/**The MIT License (MIT)

Copyright (c) 2025 by Greg Cunningham, CuriousTech

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Goldenmate UPS with ESP32-C3-super mini

// Build with Arduino IDE 1.8.19, ESP32 2.0.14 or 3.2.0
// CPU Speed: Anything with WiFi
// Partition: Default 4MB with anything
// USB CDC On Boot: Enabled for serial output over USB

#include <ESPAsyncWebServer.h> // https://github.com/ESP32Async/ESPAsyncWebServer (3.7.2) and AsyncTCP (3.4.4)
#include <time.h>
#include <JsonParse.h> //https://github.com/CuriousTech/ESP-HVAC/tree/master/Libraries/JsonParse
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "Prefs.h"
#include "pages.h"
#include "jsonstring.h"

#define CS_PIN  4
#define DIN_PIN 2
#define SCK_PIN 3
#define LED     8
#define SSR     6

#define UPS_HEAD_ID 0xAA  // 0xAA = 1000VA, 0xAC = 1500VA

bool bKeyGood;
IPAddress lastIP;
int nWrongPass;

int8_t nWsConnected;

AsyncWebServer server( 80 );
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
AsyncWebSocket wsb("/bin"); // binary websocket for Windows app
//AsyncWebSocket wsb2("/other"); // Add some other WebSocket for alternate needs. Duplicate the wsb references for your format

uint32_t WsClientID;
uint32_t binClientID;

void jsonCallback(int16_t iName, int iValue, char *psValue);
JsonParse jsonParse(jsonCallback);

Prefs prefs;

bool bConfigDone;
bool bStarted;
uint32_t connectTimer;
bool bGMFormatSerial;

struct flagBits{
  uint16_t OnUPS:1;
  uint16_t OnAC:1;
  uint16_t battDisplay:3; // 0-5
  uint16_t battLevel:4; // 0-10
  uint16_t error:7; // 01-99 probably
};

struct upsData
{
  uint8_t head;
  uint8_t VoltsIn;
  uint8_t VoltsOut;
  uint16_t WattsIn;
  uint16_t WattsOut;
  flagBits b;
  uint8_t sum;
}; // 10 bytes

upsData binPayload;

// ISR working mem
volatile uint16_t ups_value;
volatile uint16_t bitCnt;
volatile uint8_t ups_nibble[32]; // valid is 0-29, but the addr could hit 31, so be safe
volatile bool bReady;

String dataJson()
{
  jsonString js("state");

  js.Var("t", (uint32_t)time(nullptr));
  int sig = WiFi.RSSI();
  js.Var("rssi", sig);
  js.Var("connected", binClientID);
  return js.Close();
}

String statusJson()
{
  jsonString js("data");
  js.Var("AC", binPayload.b.OnAC);
  js.Var("UPS", binPayload.b.OnUPS);
  js.Var("voltsIn", binPayload.VoltsIn);
  js.Var("wattsIn", binPayload.WattsIn);
  js.Var("voltsOut", binPayload.VoltsOut);
  js.Var("wattsOut", binPayload.WattsOut);
  js.Var("BATT", binPayload.b.battDisplay);
  js.Var("LVL", binPayload.b.battLevel);
  js.Var("error", binPayload.b.error);
  return js.Close();
}

const char *jsonList1[] = {
  "key",
  "tzo",
  "hibernate",
  "shutdown",
  NULL
};

void parseParams(AsyncWebServerRequest *request)
{
  if (nWrongPass && request->client()->remoteIP() != lastIP) // if different IP drop it down
    nWrongPass = 10;

  lastIP = request->client()->remoteIP();

  for ( uint8_t i = 0; i < request->params(); i++ )
  {
    const AsyncWebParameter* p = request->getParam(i);
    String s = request->urlDecode(p->value());

    uint8_t idx;
    for(idx = 0; jsonList1[idx]; idx++)
      if( p->name().equals(jsonList1[idx]) )
        break;
    if(jsonList1[idx])
    {
      int iValue = s.toInt();
      if(s == "true") iValue = 1;
      jsonCallback(idx, iValue, (char *)s.c_str());
    }
  }
}

void jsonCallback(int16_t iName, int iValue, char *psValue)
{
  if (!bKeyGood && iName)
  {
    if (nWrongPass == 0)
      nWrongPass = 10;
    else if ((nWrongPass & 0xFFFFF000) == 0 ) // time doubles for every high speed wrong password attempt.  Max 1 hour
      nWrongPass <<= 1;
    return; // only allow for key
  }

  switch(iName)
  {
    case 0: // key
      if (!strcmp(psValue, prefs.szPassword)) // first item must be key
        bKeyGood = true;
      break;
    case 1: // tzo
      if(!prefs.tzo)
        prefs.tzo = iValue; // trick to get TZ
      break;
    case 2: // hibernate
      {
        static char data[] = "HIBR";
        wsb.binary(binClientID, data, 4);
        static uint8_t data2[] = {0xAB, 'H','I','B','R'};
        Serial.write(data2, 5);
      }
      break;
    case 3: // shutdown
      {
        static char data[] = "SHDN";
        wsb.binary(binClientID, data, 4);
        static uint8_t data2[] = {0xAB, 'S','H','D','N'};
        Serial.write(data2, 5);
      }
      break;
  }
}

uint8_t Hour()
{
  struct tm timeinfo;

  if(!getLocalTime(&timeinfo))
    return 0;

  return timeinfo.tm_hour;
}

void sendState()
{
  if(nWsConnected)
    ws.textAll(dataJson());
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{  //Handle WebSocket event

  switch(type)
  {
    case WS_EVT_CONNECT:      //client connected
      client->keepAlivePeriod(50);
      client->text( statusJson() );
      client->text( dataJson() );
      client->ping();
      nWsConnected++;
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
      if(nWsConnected)
        nWsConnected--;
      break;
    case WS_EVT_ERROR:    //error was received from the other end
      break;
    case WS_EVT_PONG:    //pong message was received (in response to a ping request maybe)
      break;
    case WS_EVT_DATA:  //data packet
      AwsFrameInfo * info = (AwsFrameInfo*)arg;
      if(info->final && info->index == 0 && info->len == len){
        //the whole message is in a single frame and we got all of it's data
        if(info->opcode == WS_TEXT){
          data[len] = 0;
          uint32_t ip = client->remoteIP();
          WsClientID = client->id();
          jsonParse.process((char *)data);
        }
      }
      break;
  }
}

void onBinEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{  //Handle binary WebSocket event (/bin)

  switch(type)
  {
    case WS_EVT_CONNECT:      //client connected
      client->keepAlivePeriod(50);
      binClientID = client->id();
      client->binary((uint8_t*)&binPayload, sizeof(binPayload));
      client->ping();
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
      binClientID = 0;
      break;
    case WS_EVT_ERROR:    //error was received from the other end
      binClientID = 0;
      break;
    case WS_EVT_PONG:    //pong message was received (in response to a ping request maybe)
      break;
    case WS_EVT_DATA:  //data packet
      break;
  }
}

void alert(String txt)
{
  jsonString js("alert");
  js.Var("text", txt);
  ws.textAll(js.Close());
}
 
void ICACHE_RAM_ATTR CLK_ISR()
{
  ups_value <<= 1UL; // shift the bits in
  if(digitalRead(DIN_PIN))
    ups_value |= 1;
  if(++bitCnt >= 13)
  {
    if((ups_value &0x1C00) == 0x1400) // write to address
    {
      uint8_t dataAddr = (ups_value >> 4) & 0x1F; // address

      ups_nibble[dataAddr] = ups_value & 0xF;
      if(dataAddr == 29) // last value complete
        bReady = true;
    }
  }
}

void ICACHE_RAM_ATTR CS_ISR() // CS pulls down before first clock of the 13 bits
{
  ups_value = 0; // clear next entry
  bitCnt = 0;
}

// Do a simple checksum and set the head value
void checksumData()
{
  uint8_t *pData = (uint8_t *)&binPayload;
  uint8_t sum = 0;
  for(uint8_t i = 1; i < sizeof(upsData) - 2; i++)
    sum += pData[i];
  binPayload.sum = sum;
  binPayload.head = UPS_HEAD_ID;
}

void setup()
{
  Serial.begin(115200);

  pinMode(DIN_PIN, INPUT);
  pinMode(SCK_PIN, INPUT);
  pinMode(CS_PIN, INPUT);
  prefs.init();

  strcpy(prefs.szPassword, "esp8266ct");
  WiFi.hostname(prefs.szName);
  WiFi.mode(WIFI_STA);

  if ( prefs.szSSID[0] )
  {
    WiFi.begin(prefs.szSSID, prefs.szSSIDPassword);
    WiFi.setHostname(prefs.szName);
    bConfigDone = true;
  }
  else
  {
    Serial.println("No SSID. Waiting for EspTouch");
    WiFi.beginSmartConfig();
  }
  connectTimer = time(nullptr);

  // attach AsyncWebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  wsb.onEvent(onBinEvent);
  server.addHandler(&wsb);

  server.on( "/", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    parseParams(request);
    request->send_P(200, "text/html", page_index);
  });
  server.on( "/status", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    parseParams(request);
    request->send( 200, "text/json", statusJson() );
  });
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "image/x-icon", favicon, sizeof(favicon));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });

  server.begin();

  ArduinoOTA.setHostname(prefs.szName);
  ArduinoOTA.begin();
  ArduinoOTA.onStart([]() {
    prefs.update();
    alert("OTA Update Started");
    ws.closeAll();
  });

  jsonParse.setList(jsonList1);
  attachInterrupt(digitalPinToInterrupt(SCK_PIN), CLK_ISR, RISING);
  attachInterrupt(digitalPinToInterrupt(CS_PIN), CS_ISR, FALLING);
}

void loop()
{
  static uint8_t hour_save, sec_save;

  ArduinoOTA.handle();

  static uint32_t lastMSbtn;
  static bool bPushSSR;
  static uint32_t lastMS;

  // button press simulator
  if(lastMSbtn) // release button SSR after 400ms
  {
    if(millis() - lastMSbtn > 500)
    {
      digitalWrite(SSR, LOW);
    }
  }
  if(bPushSSR) // press button, start ms timer
  {
    bPushSSR = false;
    digitalWrite(SSR, HIGH);
    lastMSbtn = millis();
  }

  // Last UPS LCD segment captured
  if(bReady)
  {
    bReady = false;

    bool bValid = decodeSegments(binPayload);

    if(bValid)
    {
      checksumData(); // prepare it for transmit
      ws.textAll( statusJson() ); // send to web page or other websocket clients
  
      if(binClientID) // send to Windows Goldenchute client
        wsb.binary(binClientID, (uint8_t*)&binPayload, sizeof(binPayload));

      if(bGMFormatSerial)
      {
        Serial.write((uint8_t*)&binPayload, sizeof(binPayload) );        
      }
      else
      {
        // Make your own format, or emulate another UPS
      }
    }
  }

  if(millis() - lastMS >= 1000) // only do stuff once per second
  {
    lastMS = millis();

    // simulate button press every ~60s or less (anything under will reset the timeout)
    static uint8_t nSSRsecs = 1;
    if(--nSSRsecs == 0)
    {
      if(binPayload.b.OnUPS == 0) // display stays on when on backup
        bPushSSR = true;
      nSSRsecs = 58;
    }

    // send some basic info to web page (keepalive)
    sendState();

    // WiFi async connect stuff
    if(!bConfigDone)
    {
      if( WiFi.smartConfigDone())
      {
        Serial.println("SmartConfig set");
        bConfigDone = true;
        connectTimer = time(nullptr);
        WiFi.SSID().toCharArray(prefs.szSSID, sizeof(prefs.szSSID)); // Get the SSID from SmartConfig or last used
        WiFi.psk().toCharArray(prefs.szSSIDPassword, sizeof(prefs.szSSIDPassword) );
        prefs.update();
      }
    }

    if(bConfigDone)
    {
      if(WiFi.status() == WL_CONNECTED)
      {
        if(!bStarted)
        {
          Serial.println("WiFi Connected");
          WiFi.mode(WIFI_STA);
          MDNS.begin( prefs.szName );
          MDNS.addService("iot", "tcp", 80);
          bStarted = true;
          configTime(0, 0, "pool.ntp.org");
        }
        connectTimer = time(nullptr);
      }
      else if(time(nullptr) - connectTimer > 10) // failed to connect or connection lost
      {
        Serial.println("Connect failed. Starting SmartConfig");
        connectTimer = time(nullptr);
        WiFi.mode(WIFI_AP_STA);
        WiFi.beginSmartConfig();
        bConfigDone = false;
        bStarted = false;
      }
    }

    if(hour_save != Hour())
    {
      hour_save = Hour();
//    if(hour_save == 2) // todo: update time daily or nah?
//      configTime(0, 0, "pool.ntp.org");

      prefs.update(); // check for any prefs changes and update
    }

    // wrong password reject counter
    if (nWrongPass)
      nWrongPass--;
  }

  checkSerial();
}

// Check for incoming serial data
void checkSerial()
{
  static uint8_t buffer[8];
  static uint8_t bufIdx = 0;

  while(Serial.available() > 0)
  {
    buffer[bufIdx] = Serial.read();
    if(bufIdx < 8) bufIdx++;

    // The Goldenamte Windows app will send this:
    if(bufIdx == 4)
    {
      if( buffer[0] == 0xAA && buffer[1] == 'G' && buffer[2] == 'M' && buffer[3] == 0)
      {
        bGMFormatSerial = true;
      }
      else
      {
      // bGMFormatSerial = false;
      // Some other app
      }
      bufIdx = 0;
    }
  }
}

bool decodeSegments(upsData& payload)
{
  upsData udata;
  uint8_t vIn[3];
  uint8_t vOut[3];
  uint8_t wIn[4];
  uint8_t wOut[4];
  static uint8_t lastBattDisp;

  if(convertWDig(2) == 10) // U## error display
  {
    udata.b.error = (convertWDig(4) * 10) + convertWDig(6);
    payload = udata;
    return true;
  }

  udata.b.error = 0;
  udata.b.OnUPS = (ups_nibble[18] & 8) ? true:false;
  udata.b.OnAC = (ups_nibble[25] & 8) ? true:false;

  vIn[0]  = convertVDig(24); vIn[1]  = convertVDig(26); vIn[2]  = convertVDig(28);
  vOut[0] = convertVDig(17); vOut[1] = convertVDig(19); vOut[2] = convertVDig(21);
  wIn[0]  = convertWDig(0);  wIn[1]  = convertWDig(2);  wIn[2]  = convertWDig(4); wIn[3] = convertWDig(6);
  wOut[0] = convertWDig(9);  wOut[1] = convertWDig(11); wOut[2] = convertWDig(13); wOut[3] = convertWDig(15);

  // invalid digit
  if(vIn[0] == 0xFF || vIn[1] == 0xFF || vIn[2] == 0xFF ||
     vOut[0] == 0xFF || vOut[1] == 0xFF || vOut[2] == 0xFF ||
     wIn[0] == 0xFF || wIn[1] == 0xFF || wIn[2] == 0xFF || wIn[3] == 0xFF ||
     wOut[0] == 0xFF || wOut[1] == 0xFF || wOut[2] == 0xFF || wOut[3] == 0xFF)
    return false;

  if(vIn[2] > 2 || vOut[2] > 2) // potential error maybe
    return false;

  udata.VoltsIn = vIn[0] + ( vIn[1] * 10) + (vIn[2] * 100);
  udata.VoltsOut = vOut[0] + ( vOut[1] * 10) + (vOut[2] * 100);
  udata.WattsIn = (wIn[0] * 1000) + (wIn[1] * 100) + (wIn[2] * 10) + wIn[3];
  udata.WattsOut = (wOut[0] * 1000) + (wOut[1] * 100) + (wOut[2] * 10) + wOut[3];

  if(udata.VoltsIn == 0 && udata.VoltsOut == 0) // blank display glitch
    return false;

  uint8_t battBits = ups_nibble[8] | ( (ups_nibble[9] & 1) << 4);

  switch(battBits)
  {
    case 0b00000: udata.b.battDisplay = 0; break;
    case 0b00001: udata.b.battDisplay = 1; break;
    case 0b00011: udata.b.battDisplay = 2; break;
    case 0b00111: udata.b.battDisplay = 3; break;
    case 0b01111: udata.b.battDisplay = 4; break;
    case 0b11111: udata.b.battDisplay = 5; break;
    default: return false; // anything else
  }

  udata.b.battLevel = udata.b.battDisplay * 2;

  if(lastBattDisp != udata.b.battDisplay) // half a level alternates (blinking)
  {
    udata.b.battLevel = max(lastBattDisp, (uint8_t)udata.b.battDisplay) * 2 - 1;
  }

  lastBattDisp = udata.b.battDisplay;

  payload = udata;
  return true;
}

uint8_t convertVDig(uint8_t n)
{
  uint8_t dig = ((ups_nibble[n+1] << 4) | ups_nibble[n]) & 0x7F;

  switch(dig)
  {    //  DFGEABC  
    case 0b0000000: return 0; // blank
    case 0b1011111: return 0;
    case 0b0000110: return 1;
    case 0b0111101: return 2;
    case 0b0101111: return 3;
    case 0b1100110: return 4;
    case 0b1101011: return 5;
    case 0b1111011: return 6;
    case 0b0001110: return 7;
    case 0b1111111: return 8;
    case 0b1101111: return 9;
  }
  return 0xFF; // invalid
}

uint8_t convertWDig(uint8_t n)
{
  uint8_t dig = ups_nibble[n+1] | ((ups_nibble[n] << 3) & 0x70);

  switch(dig)
  {     // EGFDCBA
    case 0b0000000: return 0; // blank
    case 0b1011111: return 0;
    case 0b0000110: return 1;
    case 0b1101011: return 2;
    case 0b0101111: return 3;
    case 0b0110110: return 4;
    case 0b0111101: return 5;
    case 0b1111101: return 6;
    case 0b0000111: return 7;
    case 0b1111111: return 8;
    case 0b0111111: return 9;
    case 0b1011110: return 10; // U the dredded error display
  }
  return 0xFF; // invalid
}

/*
   A
F     B
   G
E     C
   D

SEG    COM  2   1  0
00000  1E  1G  1F  XX  // watts L (XX = off on UPS?)
00001  1D  1C  1B  1A
00002  2E  2G  2F  XX  // XX = off on UPS
00003  2D  2C  2B  2A
00004  3E  3G  3F  XX  //  2 of these XX are VAC left and right
00005  3D  3C  3B  3A
00006  4E  4G  4F   W
00007  4D  4C  4B  4A

00008  B1  B2  B3  B4  // battery

00009  1E  1G  1F  B5  // watts R and B5
00010  1D  1C  1B  1A  
00011  2E  2G  2F  XX
00012  2D  2C  2B  2A
00013  3E  3G  3F  XX
00014  3D  3C  3B  3A
00015  4E  4G  4F   W
00016  4D  4C  4B  4A

00017  3A  3B  3C  3D  // volts R
00018  OUT 3F  3G  3E  // OUT = On UPS
00019  2A  2B  2C  2D
00020  US  2F  2G  2E  // US = Right underscore
00021  1A  1B  1C  1D
00022  OP  1F  1G  1E  // OP = OUTPUT label
00023  GM  00  00  00  // GM = Goldenmate logo

00024  3A  3B  3C  3D  // volts L
00025  AC  3F  3G  3E  // AC = On AC
00026  2A  2B  2C  2D
00027  US  2F  2G  2E  // US = Left underscore
00028  1A  1B  1C  1D
00029  IN  1F  1G  1E  // IN = Input label
*/
