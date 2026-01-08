#include "Config.h"

const char szSettings[] = "/settings.bin";
const char szDaily[] = "/dailywh.bin";

Config::Config()
{
}

bool Config::init()
{
  if(!INTERNAL_FS.begin(true))
    return false;

  uint8_t data[EESIZE];
  uint16_t *pwTemp = (uint16_t *)data;

  File F = INTERNAL_FS.open(szSettings, "r");
  if(F)
  {
    F.read((byte*)data, EESIZE);
    F.close();
  }

  if(pwTemp[0] != EESIZE)
  {
    return true; // revert to defaults if struct size changes
  }

  uint16_t sum = pwTemp[1];
  pwTemp[1] = 0;
  pwTemp[1] = Fletcher16(data, EESIZE );
  if(pwTemp[1] != sum)
  {
    return true; // revert to defaults if sum fails
  }
  memcpy(this + offsetof(Config, size), data, EESIZE );

  F = INTERNAL_FS.open(szDaily, "r");
  if(F)
  {
    F.read((byte*)&nDailyWh, sizeof(nDailyWh));
    F.close();
    nDailySum = Fletcher16((uint8_t*)&nDailyWh, sizeof(nDailyWh));
  }

  return true;
}

void Config::update() // write the settings and dailyWh if changed
{
  uint16_t old_sum = sum;
  sum = 0;
  sum = Fletcher16((uint8_t*)this + offsetof(Config, size), EESIZE);

  if(old_sum != sum)
  {
    File F;
  
    if(F = INTERNAL_FS.open(szSettings, "w"))
    {
      F.write((byte*) this + offsetof(Config, size), EESIZE);
      F.close();
    }
  }

  uint16_t nNewSum = Fletcher16((uint8_t*)&nDailyWh, sizeof(nDailyWh));
  
  if(nNewSum != nDailySum)
  {
    File F;
  
    if(F = INTERNAL_FS.open(szDaily, "w"))
    {
      F.write((byte*)&nDailyWh, sizeof(nDailyWh));
      F.close();
      nDailySum = nNewSum;
    }
  }
}

uint16_t Config::Fletcher16( uint8_t* data, int count)
{
  uint16_t sum1 = 0;
  uint16_t sum2 = 0;
  
  for( int index = 0; index < count; ++index )
  {
    sum1 = (sum1 + data[index]) % 255;
    sum2 = (sum2 + sum1) % 255;
  }
  
  return (sum2 << 8) | sum1;
}
