#include "Prefs.h"

Prefs::Prefs()
{
  uint8_t data[EESIZE];
  
  pfs.begin("ups", false);
  pfs.getBytes("settings", data, EESIZE );
  uint16_t *pw = (uint16_t*)data;

  if(pw[0] == EESIZE) // just make sure size is correct
  {
    pfs.getBytes("settings", (uint8_t*)this + offsetof(Prefs, size), EESIZE );
  }
  pfs.end();
}

void Prefs::update() // write the settings if changed
{
  uint16_t old_sum = sum;
  sum = 0;
  sum = Fletcher16((uint8_t*)this + offsetof(Prefs, size), EESIZE);

  if(old_sum == sum)
    return; // Nothing has changed?

  pfs.begin("ups", false);
  pfs.putBytes("settings", (uint8_t*)this + offsetof(Prefs, size), EESIZE );
  pfs.end();
}

uint16_t Prefs::Fletcher16( uint8_t* data, int count)
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
