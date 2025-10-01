#ifndef PREFS_H
#define PREFS_H

#include <Arduino.h>
#include <Preferences.h>

#define EESIZE (offsetof(Prefs, end) - offsetof(Prefs, size) )

class Prefs
{
public:
  Prefs(){};
  void init(void);
  void update(void);

  uint16_t  size = EESIZE;          // if size changes, use defauls
  uint16_t  sum = 0xAAAA;           // if sum is diiferent from memory struct, write
  char      szSSID[32] = ""; // SSID of router (blank for EspTouch)
  char      szSSIDPassword[64] = ""; // password for router
  char      szName[32] = "UPS"; // mDNS and OTA name
  char      szPassword[32] = "password"; // password for web
  uint16_t  ppkw = 15; // cents
  uint32_t  initialDate = 0;
  uint16_t  nPercentUsage = 0;
  uint16_t  nCycles = 0;
  uint8_t   res[20]; // change the length to force overwrite
  uint8_t   end;
private:
  uint16_t Fletcher16( uint8_t* data, int count);
  Preferences pfs;
};

extern Prefs prefs;

#endif // PREFS_H
