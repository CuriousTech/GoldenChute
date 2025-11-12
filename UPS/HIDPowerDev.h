#ifndef HIDPWRDEV_H
#define HIDPWRDEV_H

#include "Arduino.h"
#include "USB.h"
#include "USBHID.h"

//================================================================================

#define IPRODUCT 2
#define ISERIAL 3
#define IMANUFACTURER 1

#define HID_PD_IPRODUCT              0x01 // FEATURE ONLY
#define HID_PD_SERIAL                0x02 // FEATURE ONLY
#define HID_PD_MANUFACTURER          0x03 // FEATURE ONLY
#define IDEVICECHEMISTRY             0x04
#define IOEMVENDOR                   0x05

#define HID_PD_RECHARGEABLE          0x06 // FEATURE ONLY
#define HID_PD_PRESENTSTATUS         0x07 // INPUT OR FEATURE(required by Windows)
#define HID_PD_REMAINTIMELIMIT       0x08
#define HID_PD_MANUFACTUREDATE       0x09 //
#define HID_PD_CONFIGVOLTAGE         0x0A // 10 FEATURE ONLY
#define HID_PD_VOLTAGE               0x0B // 11 INPUT (NA) OR FEATURE(implemented)
#define HID_PD_REMAININGCAPACITY     0x0C // 12 INPUT OR FEATURE(required by Windows)
#define HID_PD_RUNTIMETOEMPTY        0x0D // 13
#define HID_PD_FULLCHRGECAPACITY     0x0E // 14 FEATURE ONLY. Last Full Charge Capacity 
#define HID_PD_WARNCAPACITYLIMIT     0x0F //
#define HID_PD_CPCTYGRANULARITY1     0x10
#define HID_PD_REMNCAPACITYLIMIT     0x11
#define HID_PD_DELAYBE4SHUTDOWN      0x12 // 18 FEATURE ONLY
#define HID_PD_DELAYBE4REBOOT        0x13
#define HID_PD_AUDIBLEALARMCTRL      0x14 // 20 INPUT OR FEATURE
#define HID_PD_CURRENT               0x15 // 21 FEATURE ONLY
#define HID_PD_CAPACITYMODE          0x16
#define HID_PD_DESIGNCAPACITY        0x17 // 23
#define HID_PD_CPCTYGRANULARITY2     0x18
#define HID_PD_AVERAGETIME2FULL      0x1A
#define HID_PD_AVERAGECURRENT        0x1B
#define HID_PD_AVERAGETIME2EMPTY     0x1C
#define HID_PD_CYCLECOUNT            0x1D

#define HID_PD_IDEVICECHEMISTRY      0x1F // 31 FEATURE
#define HID_PD_IOEMINFORMATION       0x20 // 32 FEATURE
#define HID_PD_PRIMARYBATTERY        0x2E //    FEATURE

struct PresentStatusBits
{
  uint8_t Charging : 1;                   // bit 0x00
  uint8_t Discharging : 1;                // bit 0x01
  uint8_t ACPresent : 1;                  // bit 0x02
  uint8_t BatteryPresent : 1;             // bit 0x03
  uint8_t BelowRemainingCapacityLimit : 1;// bit 0x04
  uint8_t RemainingTimeLimitExpired : 1;  // bit 0x05
  uint8_t NeedReplacement : 1;            // bit 0x06
  uint8_t VoltageNotRegulated : 1;        // bit 0x07
  
  uint8_t FullyCharged : 1;               // bit 0x08
  uint8_t FullyDischarged : 1;            // bit 0x09
  uint8_t ShutdownRequested : 1;          // bit 0x0A
  uint8_t ShutdownImminent : 1;           // bit 0x0B
  uint8_t CommunicationLost : 1;          // bit 0x0C
  uint8_t Overload : 1;                   // bit 0x0D
  uint8_t PrimaryBattery : 1;
  uint8_t unused : 1;
};

union PresentStatus
{
    uint16_t w;
    PresentStatusBits b;
};

struct manufactDate
{
  uint16_t day : 5; // 1 = Jan
  uint16_t month : 4; // 1 = 1st dom
  uint16_t year : 7; // 2025 - 1980
};

class HIDPowerDevice: public USBHIDDevice
{
public:
  HIDPowerDevice(void);
  
  void begin(void)
  {
    HID.begin();
  }

  void SetPresentStatus(uint16_t status, uint8_t cap, uint16_t v);
  void setMfgDate(const manufactDate& mfd);
  void setCycleCnt(uint16_t nCyc);
  void setTimes(uint16_t nFullTime, uint16_t nSecondsRemaining, uint16_t nSecsToCharge);

private:
  uint16_t _onGetDescriptor(uint8_t* buffer);
  uint16_t _onGetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len);
  uint16_t _onSetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len);

  uint8_t _PresentStatus[2];
  uint8_t _RemainingCap = 100;
  uint16_t _nCycleCount = 1;
  uint16_t _nVolts;
  uint16_t _nTimeToFull = 60*60;
  uint16_t _nTimeToEmpty = 60*60;
  uint16_t _nTimeRemain = 60*60;
  manufactDate _mfgDate;
  USBHID HID;
};

#endif // HIDPWRDEV_H
