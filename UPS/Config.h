#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define EESIZE (offsetof(Config, end) - offsetof(Config, size) )

#include <SPIFFS.h> // SPIFFS has no subdirs but takes less code space

#define INTERNAL_FS SPIFFS // No need to upload or format SPIFFS

class Config
{
public:
  Config();
  bool init(void);
  void update(void);

  uint16_t  size = EESIZE;          // if size changes, use defauls
  uint16_t  sum = 0xAAAA;           // if sum is diiferent from memory struct, write
  char      szSSID[32] = ""; // SSID of router (blank for EspTouch)
  char      szSSIDPassword[64] = ""; // password for router
  char      szName[32] = "UPS"; // mDNS and OTA name
  char      szPassword[32] = "passwword"; // password for web
  uint16_t  ppkw = 160; // 1/10th cents per kwh
  uint32_t  initialDate = 0; // first use or mfg date
  uint16_t  nPercentUsage = 0;
  uint16_t  nCycles = 0;
  uint32_t  lastCycleDate = 0; // last cycle of 100% - should be cycled every 3 months
  uint8_t   reserved1;
  uint8_t   RemainCapLimit = 5;
  uint8_t   WarnCapLimit = 10;
  uint8_t   res[13]; // change the length to force overwrite
  uint8_t   end;

  uint16_t  nDailyWh[31];
  uint16_t  nDailySum;
private:
  uint16_t Fletcher16( uint8_t* data, int count);
};

extern Config cfg;

#endif // PREFS_H
