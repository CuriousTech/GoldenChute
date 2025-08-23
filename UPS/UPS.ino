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

#define CS_PIN  7
#define DIN_PIN 6
#define SCK_PIN 4
#define LED     8
#define SSR     3

#define UPS_MODEL 0 // 0 = 1000VA, 1 = 1500VA, 2 = 2000VA

bool bKeyGood;
IPAddress lastIP;
int nWrongPass;

int8_t nWsConnected;

const byte battLevels[] = {4, 9, 19, 39, 59, 79, 89, 100};

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

// percent calculator
uint8_t nLevelPercent;
uint8_t nBarPercent;
uint32_t nWattsAccum;
uint32_t nWattsPerBar;

struct flagBits{
  uint16_t OnUPS : 1;
  uint16_t OnAC : 1;
  uint16_t error : 1; // WattsIn will be error #
  uint16_t model : 3; // 0 = 1000VA, 1 = 1500VA, 2 = 2000VA 
  uint16_t reserved1 : 4;
  uint16_t battDisplay : 3; // 0-5
  uint16_t battLevel : 3; // 0-7 (will be phased out)
};

struct upsData
{
  uint8_t  head[2]; // should be 16 bit aligned
  flagBits b;
  uint8_t  VoltsIn;
  uint8_t  VoltsOut;
  uint16_t WattsIn;
  uint16_t WattsOut;
  uint8_t  battPercent;
  uint8_t  sum;
}; // 12 bytes

upsData binPayload;

// cccaaaaaadddd
struct holtekBits{
  uint16_t data : 4;
  uint16_t addr : 6;
  uint16_t cmd : 3;
};

union holtek{
    uint16_t w;
    holtekBits h;
};

// ISR working mem
volatile holtek hx;
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
  js.Var("t", (uint32_t)time(nullptr));
  int sig = WiFi.RSSI();
  js.Var("rssi", sig);
  js.Var("connected", binClientID);
  js.Var("AC", binPayload.b.OnAC);
  js.Var("UPS", binPayload.b.OnUPS);
  js.Var("voltsIn", binPayload.VoltsIn);
  js.Var("wattsIn", binPayload.WattsIn);
  js.Var("voltsOut", binPayload.VoltsOut);
  js.Var("wattsOut", binPayload.WattsOut);
  js.Var("BATT", binPayload.b.battDisplay);
  js.Var("LVL", binPayload.b.battLevel);
  js.Var("battPercent", binPayload.battPercent );
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
  hx.w <<= 1UL; // shift the bits in
  hx.w |= digitalRead(DIN_PIN);
}

void ICACHE_RAM_ATTR CS_ISR() // CS raises after 13th bit
{
  if(hx.h.cmd != 5) // write to address
    return;
  if(hx.h.addr & 0x20) // block addr over 31
    return;

  ups_nibble[hx.h.addr] = hx.h.data;
  if(hx.h.addr == 29) // last value complete
    bReady = true;
} 

// Do a simple checksum and set the head value
void checksumData()
{
  binPayload.head[0] = 0xAB;
  binPayload.head[1] = 0xCD;

  uint8_t *pData = (uint8_t *)&binPayload + 2;
  uint8_t sum = 0;
  for(uint8_t i = 0; i < sizeof(upsData) - 3; i++)
    sum += pData[i];
  binPayload.sum = sum;
}

void setup()
{
  Serial.begin(115200); // USB serial data rate (9600 is probably more common)

  pinMode(DIN_PIN, INPUT);
  pinMode(SCK_PIN, INPUT);
  pinMode(CS_PIN, INPUT);
  pinMode(SSR, OUTPUT);
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
    digitalWrite(SSR, LOW); // ensure button isn't being pressed
    prefs.update();
    alert("OTA Update Started");
    ws.closeAll();
  });

  jsonParse.setList(jsonList1);
  attachInterrupt(digitalPinToInterrupt(SCK_PIN), CLK_ISR, RISING);
  attachInterrupt(digitalPinToInterrupt(CS_PIN), CS_ISR, RISING);
}

void loop()
{
  static uint8_t hour_save, sec_save;
  static uint32_t sentMS;

  ArduinoOTA.handle();

  static uint32_t lastMSbtn;
  static bool bPushSSR;
  static uint32_t lastMS;

  // button press simulator
  if(lastMSbtn) // release button SSR after 400ms
  {
    if(millis() - lastMSbtn > 500)
    {
      digitalWrite(SSR, LOW); // release button after 500ms
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

    if( decodeSegments(binPayload) )
    {
      checksumData(); // prepare it for transmit
      sentMS = millis();
      calcPercent();
      ws.textAll( statusJson() ); // send to web page or other websocket clients

      if(binClientID) // send to Windows Goldenchute client
      {
        wsb.binary(binClientID, (uint8_t*)&binPayload, sizeof(binPayload));
      }

      if(bGMFormatSerial)
      {
        Serial.write((uint8_t*)&binPayload, sizeof(binPayload) );        
      }
      else if(binPayload.b.error)
      {
        String s = "ERROR ";
        s += binPayload.WattsIn; // Send error code text
        Serial.println(s);        
      }
      else
      {
        // default: text "0,250,100" for not on battery, 250 watts, battery level 100%
        // or make your own format, or emulate another UPS
        String s = "";
        s += (binPayload.b.OnUPS) ? 1:0;
        s += ",";
        s += binPayload.WattsOut;
        s += ",";
        s += binPayload.battPercent;
        Serial.println(s);
      }
    }
    else
    {
      // todo: serial keepalive
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

    // send some basic info to web page (keepalive) if no other data sent recently
    if(millis() - sentMS > 1000)
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

// Called when the battery level changes
void levelChange()
{
  binPayload.battPercent = nLevelPercent = battLevels[binPayload.b.battLevel]; // current percent at new bar display
  nBarPercent = 0; // percent for current bar

  if (binPayload.b.OnUPS) // bar dropped
  {
    if (binPayload.b.battLevel)
    {
      nBarPercent = nLevelPercent - battLevels[binPayload.b.battLevel - 1];
    }
    else nBarPercent = battLevels[0]; // no bars (last 5%)
  }
  else if(binPayload.b.battLevel != 7) // bar incremented
  {
    nBarPercent = battLevels[binPayload.b.battLevel + 1] - nLevelPercent;
  }
  else
    nBarPercent = 10; // level 7 = 10%

  uint16_t nWhTotal = 900; // 90% efficiency
  switch(binPayload.b.model)
  {
    case 1: nWhTotal = 1350; break; // 1500
    case 2: nWhTotal = 1800; break; // 2000
  }

  nWattsPerBar = nWhTotal * nBarPercent / 100 * 3600; // watt hours per current bar to watt seconds
  nWattsAccum = 0; // reset accumulator on level change
}

void calcPercent()
{
  static uint8_t nCnt = 0;
  static uint8_t lvl = 0;

  if (binPayload.b.battLevel != lvl) // skip first second of change
  {
    nCnt = 0;
    lvl = binPayload.b.battLevel;
  }
  else if(nCnt < 10) nCnt++;

  if (nCnt == 2)
  {
    levelChange();
  }

  if (binPayload.b.OnUPS)
    nWattsAccum += binPayload.WattsIn; // discharge watts (battery is likely WattsIn)
  else
    nWattsAccum += binPayload.WattsIn - binPayload.WattsOut - 1; // charge watts + 1W

  if (nBarPercent)
  {
    int percDiff = nWattsAccum * nBarPercent / nWattsPerBar;

    if (binPayload.b.OnUPS)
    {
      if (nLevelPercent - percDiff > 0)
        binPayload.battPercent = nLevelPercent - percDiff;
    }
    else if (binPayload.b.battLevel < 7)
      binPayload.battPercent = nLevelPercent + percDiff;
  }
}

// Check for incoming serial data
void checkSerial()
{
  static uint8_t buffer[8];
  static uint8_t bufIdx = 0;

  while(Serial.available() > 0)
  {
    buffer[bufIdx++] = Serial.read();

    // The Goldenamte Windows app will send this:
    if(bufIdx >= 4)
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

bool decodeSegments(upsData& udata)
{
  uint8_t vIn[3];
  uint8_t vOut[3];
  uint8_t wIn[4];
  uint8_t wOut[4];
  static uint8_t lastBattDisp;

  if(convertWDig(2) == 10) // U## error display
  {
    udata.b.error = 1;
    udata.WattsIn = (convertWDig(4) * 10) + convertWDig(6);
    return true;
  }

  udata.b.model = UPS_MODEL;
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
  {
    return false;
  }

  udata.VoltsIn = vIn[0] + ( vIn[1] * 10) + (vIn[2] * 100);
  udata.VoltsOut = vOut[0] + ( vOut[1] * 10) + (vOut[2] * 100);
  udata.WattsIn = (wIn[0] * 1000) + (wIn[1] * 100) + (wIn[2] * 10) + wIn[3];
  udata.WattsOut = (wOut[0] * 1000) + (wOut[1] * 100) + (wOut[2] * 10) + wOut[3];

  if(udata.WattsIn < udata.WattsOut) // impossible
  {
    return false;
  }

  if(udata.VoltsIn > 125 || udata.VoltsOut > 125) // US only
  {
    return false;
  }

  if(udata.VoltsIn == 0 && udata.VoltsOut == 0) // blank display glitch
  {
    return false;
  }

  uint8_t battBits = (ups_nibble[8] << 1) | (ups_nibble[9] & 1);

  switch(battBits)
  {
    case 0b00000: udata.b.battDisplay = 0; udata.b.battLevel = 0; break; // <= 4%
    case 0b10000: udata.b.battDisplay = 1; udata.b.battLevel = 2; break; // 11-19% blinking = 5-9%
    case 0b11000: udata.b.battDisplay = 2; udata.b.battLevel = 3; break; // 20-39%
    case 0b11100: udata.b.battDisplay = 3; udata.b.battLevel = 4; break; // 40-59%
    case 0b11110: udata.b.battDisplay = 4; udata.b.battLevel = 5; break; // 60-79%
    case 0b11111: udata.b.battDisplay = 5; udata.b.battLevel = 7; break; // 90-100% blinking = 80-89%
    default: return false; // anything else
  }

  // alternating display 1
  if((lastBattDisp == 1 && udata.b.battDisplay == 0) || (lastBattDisp == 0 && udata.b.battDisplay == 1))
      udata.b.battLevel = 1;
  // alternating display 5
  if((lastBattDisp == 5 && udata.b.battDisplay == 4) || (lastBattDisp == 4 && udata.b.battDisplay == 5))
      udata.b.battLevel = 6;

  lastBattDisp = udata.b.battDisplay;

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
 https://datasheet.octopart.com/HT1621B-Holtek-datasheet-180022271.pdf

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
