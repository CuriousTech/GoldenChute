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

// Goldenmate UPS with ESP32-C3-super mini or ESP32-S3-super mini

// Build with Arduino IDE 1.8.19, ESP32 2.0.14 or 3.2.0
// CPU Speed: 160MHz (WiFi)
// Partition: Default 4MB with anything
// USB CDC On Boot: Enabled - for serial output over USB

///////////////////////////////////////////////////
//  Model      Volts Amps    Wh      ~98% integer
// 1000VA/600W 25.6V 6Ah   = 153.6Wh  150
// 1000VA/800W 25.6V 9Ah   = 230.4Wh  226
// 1500VA/1000W 51.2V 5.8Ah= 297Wh    291
// 1500VA/1200W 51.2V 6Ah  = 307Wh    301   (manual is incorrect, so I'm guessing)
// 2000VA/1600W 51.2V 9Ah  = 460.8Wh  451
#define BATTERY_WH 226

#define TZ  "EST5EDT,M3.2.0,M11.1.0"  // https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv


#include <ESPAsyncWebServer.h> // https://github.com/ESP32Async/ESPAsyncWebServer (3.7.2) and AsyncTCP (3.4.4)
#include <time.h>
#include <JsonParse.h> //https://github.com/CuriousTech/ESP-HVAC/tree/master/Libraries/JsonParse
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "Prefs.h"
#include "pages.h"
#include "jsonstring.h"

// ESP32-C3 on r2 PCB
#if CONFIG_IDF_TARGET_ESP32C3
#define CS_PIN  GPIO_NUM_7
#define DIN_PIN GPIO_NUM_6 // GPIO_NUM_8 // PCB r3
#define SCK_PIN GPIO_NUM_4
#define LED     8
#define SSR     3
// ESP32-S3 on r3 PCB
#elif CONFIG_IDF_TARGET_ESP32S3
#define CS_PIN  GPIO_NUM_1
#define DIN_PIN GPIO_NUM_2
#define SCK_PIN GPIO_NUM_13
#define LED     48
#define SSR     12
//#define USE_HID 1  // Uncomment for HID device
#endif

// Auto-enabled when building for ESP32-S3 and uses HID
#if CONFIG_TINYUSB_HID_ENABLED && USE_HID
#include "HIDPowerDev.h"
HIDPowerDevice Device;
#endif

bool bKeyGood;
IPAddress lastIP;
int nWrongPass;

int8_t nWsConnected;

// The battery levels for each bar and blinking bar
const byte battLevels[] = { 4, 9, 19, 39, 59, 79, 89, 100 };

AsyncWebServer server( 80 );
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
AsyncWebSocket wsb("/bin"); // binary websocket for Windows app
//AsyncWebSocket wsb2("/other"); // Add some other WebSocket for alternate needs. Duplicate the wsb references for your format

uint32_t WsClientID;
uint32_t binClientCnt;
tm lTime;

void jsonCallback(int16_t iName, int iValue, char *psValue);
JsonParse jsonParse(jsonCallback);

Prefs prefs;

bool bConfigDone; // EspTouch config
bool bStarted; // WiFi started
bool bRequestSD = false; // request PC shutdown
uint32_t nLongPress = 0; // Long press the power button
bool bPushSSR = false;

bool bGMFormatSerial;
uint32_t spiMS = 10000000;
bool bNeedRestart; // display may need fix after switching from battery to AC
uint16_t nShutoffDelay;

// percent calculator
uint8_t nLevelPercent;
uint8_t nBarPercent;
uint32_t nWattsAccum;
uint32_t nWattsPerBar;

uint32_t g_nSecondsRemaining;
uint32_t nWattHrArr[24];
uint32_t nWattsAccumHr;
uint16_t nWhCnt;
uint16_t nWattMin[24], nWattMax[24];

struct flagBits{
  uint16_t OnUPS : 1;
  uint16_t OnAC : 1;
  uint16_t error : 1; // WattsIn will be error #
  uint16_t charging : 1;
  uint16_t noData : 1; // display timeout indicator
  uint16_t battDisplay : 3; // 0-5 (1 and 5 blink)
  uint16_t battLevel : 3; // 0-7 (used by the app for shutdown)
  uint16_t needCycle : 1; // if it's been over 90 days with no 100% cycle
  uint16_t shuttingOff : 1; // shutoff timer is running
  uint16_t reserved : 3;
};

struct upsData
{
  uint8_t  head[2];  // should be 16 bit aligned
  flagBits b;
  uint8_t  VoltsIn;
  uint8_t  VoltsOut;
  uint16_t WattsIn;  // AC=UPS+outputs, Batt=pre-inverter, error=error #
  uint16_t WattsOut; // AC=just output, Batt=post-inverter (out-in=efficiency)
  uint16_t Capacity;  // Battery capacity in Wh
  uint8_t  battPercent;
  uint8_t  Health;
  uint8_t  reserved;
  uint8_t  sum;
}; // 16 bytes

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

bool decodeSegments(upsData& udata);
void calcPercent(void);
void checkSerial(void);
uint8_t battHealth(void);
void calcMinMax(void);

String dataJson()
{
  jsonString js("state");

  js.Var("t", (uint32_t)time(nullptr));
  js.Var("rssi", WiFi.RSSI() );
  js.Var("connected", binClientCnt);
  js.Var("nodata", binPayload.b.noData);
  js.Var("ppkwh", prefs.ppkw);
  js.Var("wh", nWattsAccumHr / 3621); // usage this hour
  js.Array("wattArr", nWattHrArr, 24);
  js.Array("wmin", nWattMin, 24);
  js.Array("wmax", nWattMax, 24);
  uint8_t nPerc = prefs.nPercentUsage;
  if(binPayload.b.OnUPS) // add the live percent used
    nPerc += 100 - binPayload.battPercent;
  js.Var("percuse", nPerc);
  js.Var("cycles", prefs.nCycles);
  js.Var("initdate", prefs.initialDate);
  js.Var("cycledate", prefs.lastCycleDate);
  js.Var("health", battHealth());
  js.Var("nc", binPayload.b.needCycle);
  js.Var("so", nShutoffDelay);
  return js.Close();
}

String statusJson()
{
  jsonString js("data");
  js.Var("t", (uint32_t)time(nullptr));
  js.Var("rssi", WiFi.RSSI());
  js.Var("connected", binClientCnt);
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
  js.Var("nodata", binPayload.b.noData);
  js.Var("secsrem", g_nSecondsRemaining);
  return js.Close();
}

const char *jsonList1[] = {
  "key",
  "hibernate",
  "shutdown",
  "restart",
  "ppkwh",
  "power",
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
    case 1: // hibernate
      {
        static char data[] = "HIBR";
        wsb.binaryAll(data, 4);
#if !(CONFIG_TINYUSB_HID_ENABLED && USE_HID)
        static uint8_t data2[] = {0xAB, 'H','I','B','R'};
        Serial.write(data2, 5);
#endif
      }
      break;
    case 2: // shutdown
      {
        static char data[] = "SHDN";
        wsb.binaryAll(data, 4);
#if CONFIG_TINYUSB_HID_ENABLED && USE_HID
        bRequestSD = true;
#else
        static uint8_t data2[] = {0xAB, 'S','H','D','N'};
        Serial.write(data2, 5);
#endif
      }
      break;
    case 3: // restart
      bNeedRestart = true;
      break;
    case 4: // ppkwh
      prefs.ppkw = iValue;
      break;
    case 5: // power
      nShutoffDelay = nLongPress & 0xFFFF;
      if(nShutoffDelay == 0)
         nLongPress = 0; // cancel
      else
         nLongPress = iValue >> 16; // rando num to make it safer
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
      binClientCnt++;
      client->binary((uint8_t*)&binPayload, sizeof(binPayload));
      nShutoffDelay = 0; // cancel shut down if there is one
      nLongPress = 0;
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
      if(binClientCnt)
        binClientCnt--;
      break;
    case WS_EVT_ERROR:    //error was received from the other end
      break;
    case WS_EVT_PONG:    //pong message was received (in response to a ping request maybe)
      break;
    case WS_EVT_DATA:  //data packet
      AwsFrameInfo * info = (AwsFrameInfo*)arg;
      if(info->final && info->index == 0 && info->len == len){
        switch(data[0])
        {
          case 'W': // send wh array
            {
              uint32_t binData[26];
              binData[0] = 0x00DEBCAA;
              binData[1] = nWattsAccumHr / 3621; // current this hour
              memcpy( (uint8_t*)&binData[2], &nWattHrArr, sizeof(nWattHrArr));
              client->binary((uint8_t*)&binData, sizeof(binData));
            }
            break;
          case 0xAB: // shutoff delay
            if(data[1] == 0xC2)
            {
              nShutoffDelay = (data[2] << 8) | data[3];
              if(nShutoffDelay == 0) // cancel poweroff
                nLongPress = 0;
              else
                nLongPress = (data[0] << 8) || data[1];
            }
            break;
        }
      }
      break;
  }
}

void alert(String txt)
{
  jsonString js("alert");
  js.Var("text", txt);
  ws.textAll(js.Close());
}

static void CLK_ISR(void *arg)
{
  hx.w <<= 1UL; // shift the bits in
  hx.w |= digitalRead(DIN_PIN);
}

static void CS_ISR(void *arg) // CS raises after 13th bit
{
  if(hx.h.cmd != 5) // write to address
  {
    hx.w = 0; // clear if different command or display is off (high-Z state)
    return;
  }
  if(hx.h.addr & 0x20) // block addr over 31
  {
    hx.w = 0;
    return;
  }

  ups_nibble[hx.h.addr] = hx.h.data;
  if(hx.h.addr == 29) // last value complete
    bReady = true;
  hx.w = 0; // clear just in case
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
  Serial.begin(115200); // USB serial data rate (9600 is probably more common) Ignnored if HID is enabled
  pinMode(DIN_PIN, INPUT);
  pinMode(SCK_PIN, INPUT);
  pinMode(SSR, OUTPUT);

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

  gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);

  gpio_set_intr_type(SCK_PIN, GPIO_INTR_POSEDGE);
  gpio_isr_handler_add(SCK_PIN, CLK_ISR, NULL);
  gpio_intr_enable(SCK_PIN);

  gpio_set_intr_type(CS_PIN, GPIO_INTR_POSEDGE);
  gpio_isr_handler_add(CS_PIN, CS_ISR, NULL);
  gpio_intr_enable(CS_PIN);

  // Adjust init date and cycles manually (firmware updates erase Preferences)
  if(prefs.initialDate == 0)
  {
    tm initDate = {0};
    // set first used date
    initDate.tm_year = 2025 - 1900; // 2025
    initDate.tm_mon = 5; // June
    initDate.tm_mday = 1; // day of month
    initDate.tm_hour = 12; // offset for most TZ
    prefs.initialDate = mktime(&initDate);
  
    // set last cycle date
    initDate.tm_year = 2025 - 1900; // 2025
    initDate.tm_mon = 6; // July
    initDate.tm_mday = 1; // day of month
    prefs.lastCycleDate = mktime(&initDate);
  
    prefs.nCycles = 1; // if you know how many cycles so far
    prefs.nPercentUsage = 0;  // copy these last 3 values from web page to preserve them before flashing
    prefs.update();
  }

  binPayload.b.noData = 1; // start out with a fail

#if CONFIG_TINYUSB_HID_ENABLED && USE_HID
  const manufactDate mfd ={
    .day = 1,
    .month = 5,
    .year = 2025 - 1980
  };
  Device.setMfgDate(mfd); // Manufactured date: June 1, 2025
  Device.begin();
#endif
}

void loop()
{
  static uint8_t hour_save, sec_save;
  static uint32_t sentMS;

  ArduinoOTA.handle();

  static uint32_t lastMSbtn;
  static uint32_t lastMS;

  // button press simulator
  if(lastMSbtn)
  {
    uint32_t ms = (nLongPress == 0xABC2) ? 5100:500;
    if(millis() - lastMSbtn > ms)
    {
      digitalWrite(SSR, LOW); // release button after 500ms
      nLongPress = 0;
    }
  }
  if(bPushSSR) // press button, start ms timer
  {
    bPushSSR = false;
    digitalWrite(SSR, HIGH);
    lastMSbtn = millis();
  }

  // Last UPS LCD segment bits captured
  if(bReady)
  {
    bReady = false;
    spiMS = millis(); // time between each frame is ~980ms including the 14ms of 30 values

    if( decodeSegments(binPayload) )
    {
      sentMS = millis();
      calcPercent();
      calcTimeRemaining();
      calcMinMax();

      binPayload.b.shuttingOff = (nLongPress) ? 1:0;

      checksumData(); // prepare it for transmit

      ws.textAll( statusJson() ); // send to web page or other websocket clients

      if(binClientCnt) // send to Windows Goldenchute client(s) over WiFi
      {
        wsb.binaryAll((uint8_t*)&binPayload, sizeof(binPayload));
      }

#if CONFIG_TINYUSB_HID_ENABLED && USE_HID
      PresentStatus ps;
  
      ps.w = 0;
  
      // just in case it's not installed properly
      uint8_t percent = (binPayload.b.noData) ? 100 : binPayload.battPercent;
  
      ps.b.Charging = binPayload.b.charging;
      ps.b.ACPresent = binPayload.b.OnAC;
      ps.b.Discharging = binPayload.b.OnUPS;
      ps.b.BatteryPresent = 1;
      ps.b.FullyCharged = (binPayload.b.charging == 0 && binPayload.b.OnAC == 1);
      ps.b.BelowRemainingCapacityLimit = (percent <= 4);
      ps.b.ShutdownImminent = (percent <= 2 || nLongPress);
      ps.b.CommunicationLost = binPayload.b.noData;
      ps.b.ShutdownRequested = bRequestSD;
      bRequestSD = false;
      Device.SetPresentStatus(ps.w, percent, binPayload.VoltsOut);
#else
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
#endif
    }
  }

  if(millis() - lastMS >= 1000) // only do stuff once per second
  {
    lastMS = millis();

    getLocalTime(&lTime); // used globally

    if(binPayload.b.OnUPS && g_nSecondsRemaining) // make it count down
      g_nSecondsRemaining--;

    static uint8_t nSSRsecs = 1;
    static bool bRestartingDisplay = false;

    if(nLongPress == 0xABC2 && nShutoffDelay)
    {
      if(--nShutoffDelay == 0) // Shutoff delay from PC or web
      {
        if( binPayload.b.OnUPS == 0) // Power has resumed (assuming it will continue)
        {
          nShutoffDelay = 0;
          nLongPress = 0;
        }
        else
        {
          bPushSSR = true;
        }
      }
    }

    // Fix for display going wonky after returning to AC power
    if(binPayload.b.OnUPS == 0)
    {
      if( bNeedRestart )
      {
        bNeedRestart = false;
        bRestartingDisplay = true; // ignore upcoming SPI timeout
        nSSRsecs = 65; // Set to higher than display timeout
      }

      if( bRestartingDisplay && millis() - spiMS > 1200) // catch the poweroff
      {
        nSSRsecs = 1; // and ready to turn back on
      }
    }

    // simulate button press every ~60s or less (anything under will reset the timeout)
    if(--nSSRsecs == 0)
    {
      if(binPayload.b.OnUPS == 0 || millis() - spiMS > 1200 ) // display stays on when on backup, but check for data anyway (could glitch)
      {
        if(millis() - spiMS > 1200 && bRestartingDisplay == false) // no data. Maybe disconnected or button press failed
        {
          binPayload.b.noData = 1;
          spiMS = millis();
          checksumData(); // prepare it for transmit

          if(binClientCnt) // send to Windows Goldenchute client(s)
          {
            wsb.binaryAll((uint8_t*)&binPayload, sizeof(binPayload));
          }

#if !(CONFIG_TINYUSB_HID_ENABLED && USE_HID)
          if(bGMFormatSerial) // binary over serial
          {
            Serial.write((uint8_t*)&binPayload, sizeof(binPayload) );
          }
          else // generic text
          {
            Serial.println("NODATA");          
          }
#endif
        }
        bRestartingDisplay = false;
        bPushSSR = true;
      }
      nSSRsecs = 58;
    }

    // send some basic info to web page (keepalive) if no other data sent recently
    if(millis() - sentMS > 1000 || lTime.tm_sec == 0)
    {
      sendState();
    }

    // WiFi async connect stuff
    if(!bConfigDone)
    {
      if( WiFi.smartConfigDone())
      {
        Serial.println("SmartConfig set");
        bConfigDone = true;
        WiFi.SSID().toCharArray(prefs.szSSID, sizeof(prefs.szSSID)); // Get the SSID from SmartConfig or last used
        WiFi.psk().toCharArray(prefs.szSSIDPassword, sizeof(prefs.szSSIDPassword) );
        prefs.update();
      }
    }

    if(bConfigDone)
    {
      switch(WiFi.status())
      {
        case WL_CONNECTED:
          if(bStarted)
            break;
          Serial.println("WiFi Connected"); // first connect setup
          WiFi.mode(WIFI_STA);
          MDNS.begin( prefs.szName );
          MDNS.addService("iot", "tcp", 80);
          bStarted = true;
          configTime(0, 0, "pool.ntp.org");
          setenv("TZ", TZ, 1);
          tzset();

          if(prefs.initialDate == 0)
          {
            prefs.initialDate = time(nullptr); // set date of first use
          }
          if(prefs.lastCycleDate == 0)
          {
            prefs.lastCycleDate = time(nullptr); // set date of first use
          }
          break;
        case WL_CONNECTION_LOST:
          break;
        case WL_CONNECT_FAILED: // failed to connect
          Serial.println("Connect failed. Starting SmartConfig");
          WiFi.mode(WIFI_AP_STA);
          WiFi.beginSmartConfig();
          bConfigDone = false;
          bStarted = false;
          break;
        case WL_DISCONNECTED:
          WiFi.begin(prefs.szSSID, prefs.szSSIDPassword);
          Serial.print("reconnect:");
          Serial.print(prefs.szSSID);
          Serial.print(" ");
          Serial.println(prefs.szSSIDPassword);
          break;
      }
    }
  
    if(hour_save != lTime.tm_hour)
    {
      hour_save = lTime.tm_hour;

      if(nWhCnt > 2)
      {
        uint16_t nDiv = 3621;
        if(nWhCnt > 3618) nDiv = nWhCnt; // Usually 3621-3622, but starting mid-hour is lower
        int8_t h = lTime.tm_hour - 1; // last hour
        if(h < 0) h = 23;
        nWattHrArr[h] = nWattsAccumHr / nDiv;
        nWattsAccumHr = 0;
        nWhCnt = 0;
      }

      // reset min/max for the hour
      nWattMin[hour_save] = 0;
      nWattMax[hour_save] = 0;

      if(hour_save == 2)
      {
        configTime(0, 0, "pool.ntp.org");
        setenv("TZ", TZ, 1);
        tzset();
      }

      prefs.update(); // check for any prefs changes and save
#if CONFIG_TINYUSB_HID_ENABLED && USE_HID
      Device.setCycleCnt(prefs.nCycles);
#endif
    }

    // wrong password reject counter
    if (nWrongPass)
      nWrongPass--;
  }

  checkSerial();
}

void calcTimeRemaining()
{
  static uint8_t nIdx = 0;
  static uint8_t nWattArrLen = 0;
  static uint16_t wattArr[16];

  wattArr[nIdx++] = binPayload.WattsIn;
  nIdx &= 15;
  if (nWattArrLen < 16)
    nWattArrLen++;

  uint16_t wTot = 0;
  for (uint8_t i = 0; i < nWattArrLen; i++)
    wTot += wattArr[i];

  uint16_t nWattAvg = wTot / nWattArrLen;

  static uint8_t perc = 0;
  static uint8_t nCnt = 0;
  bool bCharging = (binPayload.b.OnUPS || (binPayload.WattsIn - binPayload.WattsOut > 1));

  if(bCharging)
  {
    uint8_t nChargeWh = binPayload.WattsIn - binPayload.WattsOut - 1; // assume 1W operating + charging

    if(nChargeWh > prefs.nPeakChargeWh)
      prefs.nPeakChargeWh = nChargeWh;
  }

  if (binPayload.battPercent != perc || (!bCharging && ++nCnt >= 4))
  {
    perc = binPayload.battPercent;
    if (perc == 100) perc = 99;

    nCnt = 0;

    g_nSecondsRemaining = binPayload.Capacity * binPayload.Health * perc / nWattAvg / 3;

#if CONFIG_TINYUSB_HID_ENABLED && USE_HID
    uint32_t nSecsTotal = binPayload.Capacity * binPayload.Health * 100 / nWattAvg / 3;
    uint32_t nSecsToCharge = binPayload.Capacity * binPayload.Health * 100 / prefs.nPeakChargeWh / 3;

    if(nSecsToCharge > 0xFFFF)
      nSecsToCharge = 0xFFFF;

    Device.setTimes(nSecsTotal, g_nSecondsRemaining, nSecsToCharge);
#endif

  }
}

// set mix/max every reading
void calcMinMax()
{
  if(lTime.tm_year < 124) // invalid
    return;

 if(nWattMin[lTime.tm_hour] == 0)
  nWattMin[lTime.tm_hour] = 1000;
 if(nWattMin[lTime.tm_hour] > binPayload.WattsIn)
   nWattMin[lTime.tm_hour] = binPayload.WattsIn;
 if(nWattMax[lTime.tm_hour] < binPayload.WattsIn)
   nWattMax[lTime.tm_hour] = binPayload.WattsIn;
}

// Called when the battery level changes (or startup)
void levelChange()
{
  if (binPayload.b.OnUPS) // bar dropped or backup started
  {
    if(binPayload.b.battLevel)
    {
      nLevelPercent = battLevels[binPayload.b.battLevel - 1];
      nBarPercent = battLevels[binPayload.b.battLevel] - nLevelPercent;
    }
    else
    {
      nLevelPercent = 0;
      nBarPercent = battLevels[0]; // last 4%
    }
    binPayload.battPercent = battLevels[binPayload.b.battLevel]; // incorrrect but using anyway
  }
  else // bar increment (start at bottom percent of next bar)
  {
    binPayload.battPercent = nLevelPercent = battLevels[binPayload.b.battLevel - 1] + 1;
    nBarPercent = battLevels[binPayload.b.battLevel] - nLevelPercent;
  }

  // % of Wh * 3600 = watt seconds
  nWattsPerBar = nBarPercent * (BATTERY_WH * 3620) / 100; // actually 3621-3622 per hour

  if(binPayload.b.OnUPS)
    nWattsAccum = nWattsPerBar; // will be decrementing
  else
    nWattsAccum = 0; // will be incrementing
}

uint8_t battHealth()
{
  uint32_t ageDays = (time(nullptr) - prefs.initialDate) / (3600*24);
  uint16_t percDrop = (ageDays / 182); // about 2% per year

  percDrop += prefs.nCycles / 50; // 1% per 50 cycles
  return 99 - percDrop; // starting at 99%
}

void calcPercent()
{
  static uint8_t lvl = 0;
  static uint8_t cnt = 0;
  static bool lastOnUPS;
  static uint8_t nDrainStartPercent = 0;

  if (binPayload.b.battLevel != lvl || (binPayload.b.OnUPS && !lastOnUPS) ) // Switching to backup triggers level change to start accumulator
  {
    lvl = binPayload.b.battLevel;
    nDrainStartPercent = binPayload.battPercent; // beginning of discharge
    levelChange();
  }
  else if (binPayload.b.OnUPS == 0 && lastOnUPS) // Switching off battery
  {
    bNeedRestart = true; // sometimes the display glitches. Allowing it to timeout and restart causes inititialize commands to be resent
    
    int8_t nPercUsed = (nDrainStartPercent - binPayload.battPercent);
    // TODO: add an exponent - 100% = 2x 50% = 5x 30% ish
    if(nPercUsed > 0)
      prefs.nPercentUsage += nPercUsed;

    while(prefs.nPercentUsage > 100)
    {
      prefs.lastCycleDate = time(nullptr); // reset date of last full cycle
      prefs.nCycles++; // Add 1 cycle for every 100% use
      prefs.nPercentUsage -= 100;
    }
    prefs.update();
  }

  int16_t nDays = (time(nullptr) - prefs.lastCycleDate) / (3600*24);
  binPayload.b.needCycle = (nDays >= 90);

  if (binPayload.b.OnUPS)
  {
    cnt  = 0;
    if(nWattsAccum > binPayload.WattsIn)
      nWattsAccum -= binPayload.WattsIn; // discharge watts (WattsIn appears to be pre-inverter, WattsOut is post-inverter. in - out = loss)
  }
  else
  {
    if(cnt < 10) cnt++; // needs 2-3 seconds to start charge after switching to AC

    if(binPayload.WattsIn - binPayload.WattsOut <= 2 && binPayload.b.battLevel == 7) // not charging + level 7 = full
    {
      if(cnt > 2)
      {
        binPayload.battPercent = 100;
        binPayload.b.charging = 0;
        nBarPercent = 0;
      }
    }
    else // must be charging
    {
      binPayload.b.charging = 1;
      nWattsAccum += binPayload.WattsIn - binPayload.WattsOut - 1; // charge watts - 1W running power
      cnt  = 0;
    }
  
    // Usage stats (in = UPS + outputs)
    nWattsAccumHr += binPayload.WattsIn;
    nWhCnt++;
  }

  lastOnUPS = binPayload.b.OnUPS;

  // mid-bar calc
  if (nBarPercent)
  {
    int percDiff = min(nWattsAccum * nBarPercent / nWattsPerBar, (uint32_t)nBarPercent);

    binPayload.battPercent = nLevelPercent + percDiff;
  }
}

// Check for incoming serial data
void checkSerial()
{
#if !(CONFIG_TINYUSB_HID_ENABLED && USE_HID)
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
        nShutoffDelay = 0; // cancel shut down if there is one
        nLongPress = 0;
      }
      else if( buffer[0] == 0xAA && buffer[1] == 'W' && buffer[2] == 'H' && buffer[3] == 0)
      {
        uint32_t binData[26];
        binData[0] = 0x00DEBCAA;
        binData[1] = nWattsAccumHr / 3621; // current this hour
        memcpy( (uint8_t*)&binData[2], &nWattHrArr, sizeof(nWattHrArr)); // last 24hrs of data
        Serial.write((uint8_t*)&binData, sizeof(binData));
      }
      else if( buffer[0] == 0xAB && buffer[1] == 0xC2) // Delay power off from PC
      {
        nShutoffDelay = (buffer[2] << 8) | buffer[3];
        if(nShutoffDelay == 0) // cancel poweroff
            nLongPress = 0;
        else
            nLongPress = (buffer[0] << 8) || buffer[1];
      }
      else
      {
        bGMFormatSerial = false;
      // Some other app
      }
      bufIdx = 0;
    }
  }
#endif
}

bool decodeSegments(upsData& udata)
{
  uint8_t vIn[3];
  uint8_t vOut[3];
  uint8_t wIn[4];
  uint8_t wOut[4];
  static uint8_t lastBattDisp[4];

  udata.b.noData = 0;

  if(convertWDig(2) == 10) // U## error display
  {
    udata.b.error = 1;
    udata.WattsIn = (convertWDig(4) * 10) + convertWDig(6);
    return true;
  }

  udata.Capacity = BATTERY_WH;
  udata.Health = battHealth();
  udata.b.error = 0;
  udata.b.OnUPS = (ups_nibble[18] & 8) ? true:false;
  udata.b.OnAC = (ups_nibble[25] & 8) ? true:false;

  if(udata.b.OnAC && udata.b.OnUPS) // bad data
    udata.b.OnUPS = 0;

  vIn[0]  = convertVDig(24); vIn[1]  = convertVDig(26); vIn[2]  = convertVDig(28);
  vOut[0] = convertVDig(17); vOut[1] = convertVDig(19); vOut[2] = convertVDig(21);
  wIn[0]  = convertWDig(0);  wIn[1]  = convertWDig(2);  wIn[2]  = convertWDig(4);  wIn[3]  = convertWDig(6);
  wOut[0] = convertWDig(9);  wOut[1] = convertWDig(11); wOut[2] = convertWDig(13); wOut[3] = convertWDig(15);

  // check for valid digits
  uint16_t voltsIn = 0;
  uint16_t voltsOut = 0;

  if(vIn[0] != 0xFF && vIn[1] != 0xFF && vIn[2] != 0xFF)
    voltsIn = vIn[0] + ( vIn[1] * 10) + (vIn[2] * 100);

  if(vOut[0] != 0xFF && vOut[1] != 0xFF && vOut[2] != 0xFF)
    voltsOut = vOut[0] + ( vOut[1] * 10) + (vOut[2] * 100);

  if(wIn[0] != 0xFF && wIn[1] != 0xFF && wIn[2] != 0xFF && wIn[3] != 0xFF)
    udata.WattsIn = (wIn[0] * 1000) + (wIn[1] * 100) + (wIn[2] * 10) + wIn[3];

  if(wOut[0] != 0xFF && wOut[1] != 0xFF && wOut[2] != 0xFF && wOut[3] != 0xFF)
    udata.WattsOut = (wOut[0] * 1000) + (wOut[1] * 100) + (wOut[2] * 10) + wOut[3];

  if(voltsIn < 125) // change if 240V
  {
    if(voltsIn == 0 && udata.b.OnAC); // another glitch
    else udata.VoltsIn = voltsIn;
  }
  if(voltsOut && voltsOut < 125) // should never be 0
    udata.VoltsOut = voltsOut;

  switch( (ups_nibble[8] << 1) | (ups_nibble[9] & 1) )
  {
    case 0b00000: udata.b.battDisplay = 0; break;
    case 0b10000: udata.b.battDisplay = 1; break;
    case 0b11000: udata.b.battDisplay = 2; break;
    case 0b11100: udata.b.battDisplay = 3; break;
    case 0b11110: udata.b.battDisplay = 4; break;
    case 0b11111: udata.b.battDisplay = 5; break;
  }

  switch(udata.b.battDisplay)
  {
    case 0: udata.b.battLevel = 0; break; // <= 4%
    case 1: udata.b.battLevel = 2; break; // blinking = 5-9%  solid = 10-19%
    case 2: udata.b.battLevel = 3; break; // 30-39%
    case 3: udata.b.battLevel = 4; break; // 40-59%
    case 4: udata.b.battLevel = 5; break; // 60-79%
    case 5: udata.b.battLevel = 7; break; // blinking = 80-89%  solid = 90-100%
  }

  // alternating display 1 (sometimes skips, probably not timed with output)
  if(udata.b.battDisplay == 0 && (lastBattDisp[0] == 1 || lastBattDisp[1] == 1 || lastBattDisp[2] == 1 || lastBattDisp[3] == 1))
      udata.b.battLevel = 1;
  else if(udata.b.battDisplay == 1 && (lastBattDisp[0] == 0 || lastBattDisp[1] == 0 || lastBattDisp[2] == 0 || lastBattDisp[3] == 0))
      udata.b.battLevel = 1;
  // fix odd ...4 4 4 5 4... (this may occur on other levels)
  else if(udata.b.battDisplay == 5 && lastBattDisp[0] == 4 && lastBattDisp[1] == 4 && lastBattDisp[2] == 4 && lastBattDisp[3] == 4)
      udata.b.battLevel = 5;
  // alternating display 5
  else if(udata.b.battDisplay == 5 && (lastBattDisp[0] == 4 || lastBattDisp[1] == 4 || lastBattDisp[2] == 4 || lastBattDisp[3] == 4))
      udata.b.battLevel = 6;
  else if(udata.b.battDisplay == 4 && (lastBattDisp[0] == 5 || lastBattDisp[1] == 5 || lastBattDisp[2] == 5 || lastBattDisp[3] == 5))
      udata.b.battLevel = 6;

  static uint8_t idx = 0;
  lastBattDisp[idx] = udata.b.battDisplay;
  idx++;
  idx &= 3;
  
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
