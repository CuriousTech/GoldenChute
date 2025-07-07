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
// Partition: Default 4MB with anything

#include <ESPAsyncWebServer.h> // https://github.com/ESP32Async/ESPAsyncWebServer (3.7.2) and AsyncTCP (3.4.4)
#include <TimeLib.h> // https://github.com/PaulStoffregen/Time
#include <UdpTime.h> // https://github.com/CuriousTech/ESP07_WiFiGarageDoor/tree/master/libraries/UdpTime
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

int serverPort = 80;

bool bKeyGood;
IPAddress lastIP;
int nWrongPass;

int8_t nWsConnected;

AsyncWebServer server( serverPort );
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
AsyncWebSocket wsb("/bin"); // binary websocket for Windows app
uint32_t WsClientID;
uint32_t binClientID;

void jsonCallback(int16_t iName, int iValue, char *psValue);
JsonParse jsonParse(jsonCallback);

Prefs prefs;
UdpTime udpTime;

bool bConfigDone = false;
bool bStarted = false;
uint32_t connectTimer;

struct upsData
{
  uint8_t VoltsIn, VoltsOut;
  uint16_t WattsIn, WattsOut;
  bool bOnUPS;
  bool bOnAC;
  uint8_t battDisplay; // 0-5
  uint8_t battLevel; // 0-10
  uint8_t error; // 01-99 probably
  uint16_t res[5];
};

upsData binPayload;

volatile uint16_t ups_value;
volatile uint16_t bitCnt;
volatile uint8_t ups_nibble[32];
volatile bool bReady;

String dataJson()
{
  jsonString js("state");

  js.Var("t", (uint32_t)now());
  int sig = WiFi.RSSI();
  js.Var("rssi", sig);
  return js.Close();
}

String statusJson()
{
  jsonString js("data");
  js.Var("AC", binPayload.bOnAC);
  js.Var("UPS", binPayload.bOnUPS);
  js.Var("voltsIn", binPayload.VoltsIn);
  js.Var("wattsIn", binPayload.WattsIn);
  js.Var("voltsOut", binPayload.VoltsOut);
  js.Var("wattsOut", binPayload.WattsOut);
  js.Var("BATT", binPayload.battDisplay);
  js.Var("LVL", binPayload.battLevel);
  js.Var("error", binPayload.error);
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
      if(!prefs.tzo) prefs.tzo = iValue; // trick to get TZ
      break;
    case 2: // hibernate
      {
        static char data[] = "HIBR";
        wsb.binary(binClientID, data, 4);
      }
      break;
    case 3: // shutdown
      {
        static char data[] = "SHDN";
        wsb.binary(binClientID, data, 4);
      }
      break;
  }
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
      {
        String s = "bin client ";
        s += binClientID;
        WsSend(s);
      }
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
      WsSend("bin disc");
      binClientID = 0;
      break;
    case WS_EVT_ERROR:    //error was received from the other end
      WsSend("bin err");
      binClientID = 0;
      break;
    case WS_EVT_PONG:    //pong message was received (in response to a ping request maybe)
      break;
    case WS_EVT_DATA:  //data packet
      break;
  }
}

void WsSend(String s)
{
  ws.textAll(s);
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

void ICACHE_RAM_ATTR CS_ISR() // CS pulls down before first clock, each 13 bits of 30 values
{
  ups_value = 0; // clear next entry
  bitCnt = 0;
}

void setup()
{
  ets_printf("Starting\r\n");
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
    ets_printf("No SSID. Waiting for EspTouch\r\n");
    WiFi.beginSmartConfig();
  }
  connectTimer = now();

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

  // button press simulator
  if(lastMSbtn) // release button SSR after 400ms
  {
    if(millis() - lastMSbtn > 400)
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

    bool bValid = decodeSegments();

    if(bValid)
    {
      WsSend( statusJson() ); // send to web page or other websocket clients
  
      if(binClientID) // send to Windows Goldenchute client
        wsb.binary(binClientID, (uint8_t*)&binPayload, sizeof(binPayload));
    }
  }

  if(sec_save != second()) // only do stuff once per second
  {
    sec_save = second();

    // simulate button press every ~60s or less (anything under will reset the timeout)
    static uint8_t nSSRsecs = 1;
    if(--nSSRsecs == 0)
    {
      if(binPayload.bOnUPS == false) // display stays on when on backup
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
        ets_printf("SmartConfig set\r\n");
        bConfigDone = true;
        connectTimer = now();
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
          ets_printf("WiFi Connected\r\n");
          WiFi.mode(WIFI_STA);
          MDNS.begin( prefs.szName );
          MDNS.addService("iot", "tcp", serverPort);
          bStarted = true;
          udpTime.start();
        }
        if(udpTime.check(0)) // get GMT time, no TZ
        {
        }
        connectTimer = now();
      }
      else if(now() - connectTimer > 10) // failed to connect or connection lost
      {
        ets_printf("Connect failed. Starting SmartConfig\r\n");
        connectTimer = now();
        WiFi.mode(WIFI_AP_STA);
        WiFi.beginSmartConfig();
        bConfigDone = false;
        bStarted = false;
      }
    }

    if(hour_save != hour())
    {
      hour_save = hour();

      prefs.update(); // check for any prefs changes and update
      if(hour() == 2 && WiFi.status() == WL_CONNECTED)
        udpTime.start(); // update time daily at DST change
    }

    // wrong password reject counter
    if (nWrongPass)
      nWrongPass--;
  }
}

bool decodeSegments()
{
  static uint8_t lastBattDisp;

  if(convertWDig(2) == 10) // U## error display
  {
    binPayload.error = (convertWDig(4) * 10) + convertWDig(6);
    return true;
  }

  binPayload.error = 0;

  binPayload.bOnUPS = (ups_nibble[18] & 8) ? true:false;
  binPayload.bOnAC = (ups_nibble[25] & 8) ? true:false;

  binPayload.VoltsIn = convertVDig(24) + ( convertVDig(26) * 10) + (convertVDig(28) * 100);
  binPayload.VoltsOut = convertVDig(17) + ( convertVDig(19) * 10) + (convertVDig(21) * 100);
  
  binPayload.WattsIn = (convertWDig(0) * 1000) + (convertWDig(2) * 100) + (convertWDig(4) * 10) + convertWDig(6);
  binPayload.WattsOut = (convertWDig(9) * 1000) + (convertWDig(11) * 100) + (convertWDig(13) * 10) + convertWDig(15);

  if(ups_nibble[9] & 1) binPayload.battDisplay = 5;
  else if(ups_nibble[8] & 1) binPayload.battDisplay = 4;
  else if(ups_nibble[8] & 2) binPayload.battDisplay = 3;
  else if(ups_nibble[8] & 4) binPayload.battDisplay = 2;
  else if(ups_nibble[8] & 8) binPayload.battDisplay = 1;
  else binPayload.battDisplay = 0;

  binPayload.battLevel = binPayload.battDisplay * 2;

  if(lastBattDisp != binPayload.battDisplay) // half a level alternates (blinking)
  {
    binPayload.battLevel = max(lastBattDisp, binPayload.battDisplay) * 2 - 1;
  }
    
  lastBattDisp = binPayload.battDisplay;

  if(binPayload.VoltsIn == 0 && binPayload.VoltsOut == 0) // blank display glitch
    return false;

  return true;
}

uint8_t convertVDig(uint8_t n)
{
  uint8_t dig = ((ups_nibble[n+1] << 4) | ups_nibble[n]) & 0x7F;

  switch(dig)
  {    //  DFGEABC  
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
  return 0;
}

uint8_t convertWDig(uint8_t n)
{
  uint8_t dig = ups_nibble[n+1] | ((ups_nibble[n] << 3) & 0x70);

  switch(dig)
  {     // EGFDCBA
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
    case 0b1011110: return 10; // U
  }
  return 0;
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
00004  3E  3G  3F  XX
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
