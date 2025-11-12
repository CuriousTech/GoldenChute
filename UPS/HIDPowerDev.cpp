#include "Arduino.h"
#include "USB.h"
#include "USBHID.h"

#if CONFIG_TINYUSB_HID_ENABLED
#include "HIDPowerDev.h"

static const uint8_t report_descriptor[] PROGMEM = {
    0x05, 0x84, // USAGE_PAGE (Power Device)
    0x09, 0x04, // USAGE (UPS)
    0xA1, 0x01, // COLLECTION (Application)
    0x09, 0x24, //   USAGE (Sink)
    0xA1, 0x02, //   COLLECTION (Logical)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00, //     LOGICAL_MAXIMUM (255)
    0x85, HID_PD_IPRODUCT, //     REPORT_ID (1)
    0x09, 0xFE, //     USAGE (iProduct)
    0x79, IPRODUCT, //     STRING INDEX (2)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_SERIAL, //     REPORT_ID (2)
    0x09, 0xFF, //     USAGE (iSerialNumber)
    0x79, ISERIAL, //  STRING INDEX (3)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_MANUFACTURER, //     REPORT_ID (3)
    0x09, 0xFD, //     USAGE (iManufacturer)
    0x79, IMANUFACTURER, //     STRING INDEX (1)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x05, 0x85, //     USAGE_PAGE (Battery System) ====================
    0x85, HID_PD_RECHARGEABLE, //     REPORT_ID (6)
    0x09, 0x8B, //     USAGE (Rechargable)                  
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_IDEVICECHEMISTRY, //     REPORT_ID (31)
    0x09, 0x89, //     USAGE (iDeviceChemistry)
    0x79, IDEVICECHEMISTRY, //     STRING INDEX (4)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_IOEMINFORMATION,  //     REPORT_ID (32)
    0x09, 0x8F, //     USAGE (iOEMInformation)
    0x79, IOEMVENDOR, //     STRING INDEX (5)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_CAPACITYMODE, //     REPORT_ID (22)
    0x09, 0x2C, //     USAGE (CapacityMode)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_CPCTYGRANULARITY1, //     REPORT_ID (16)
    0x09, 0x8D, //     USAGE (CapacityGranularity1)
    0x26, 0x64,0x00, //     LOGICAL_MAXIMUM (100)    
    0xB1, 0x22, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_CPCTYGRANULARITY2, //     REPORT_ID (24)
    0x09, 0x8E, //     USAGE (CapacityGranularity2)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_FULLCHRGECAPACITY, //     REPORT_ID (14)        
    0x09, 0x67, //     USAGE (FullChargeCapacity)
    0xB1, 0x83, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_DESIGNCAPACITY, //     REPORT_ID (23)
    0x09, 0x83, //     USAGE (DesignCapacity)
    0xB1, 0x83, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_REMAININGCAPACITY, //     REPORT_ID (12)
    0x09, 0x66, //     USAGE (RemainingCapacity)
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x66, //     USAGE (RemainingCapacity)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_WARNCAPACITYLIMIT, //     REPORT_ID (15)
    0x09, 0x8C, //     USAGE (WarningCapacityLimit)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_REMNCAPACITYLIMIT, //     REPORT_ID (17)
    0x09, 0x29, //     USAGE (RemainingCapacityLimit)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_MANUFACTUREDATE, //     REPORT_ID (9)
    0x09, 0x85, //     USAGE (ManufacturerDate)
    0x75, 0x10, //     REPORT_SIZE (16)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     LOGICAL_MAXIMUM (65534)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_AVERAGETIME2FULL, //     REPORT_ID (26)
    0x09, 0x6A, //     USAGE (AverageTimeToFull)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     LOGICAL_MAXIMUM (65534)
    0x66, 0x01, 0x10, //     UNIT (Seconds)
    0x55, 0x00, //     UNIT_EXPONENT (0)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield) 
    0x85, HID_PD_AVERAGETIME2EMPTY, //     REPORT_ID (28)
    0x09, 0x69, //     USAGE (AverageTimeToEmpty)  
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x69, //     USAGE (AverageTimeToEmpty)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_RUNTIMETOEMPTY, //     REPORT_ID (13)    
    0x09, 0x68, //     USAGE (RunTimeToEmpty)  
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x68, //     USAGE (RunTimeToEmpty)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)      
    0x85, HID_PD_REMAINTIMELIMIT, //     REPORT_ID (8)
    0x09, 0x2A, //     USAGE (RemainingTimeLimit)
    0x75, 0x10, //     REPORT_SIZE (16)
    0x27, 0x64, 0x05, 0x00, 0x00, //     LOGICAL_MAXIMUM (1380)
    0x16, 0x78, 0x00, //     LOGICAL_MINIMUM (120)
    0x81, 0x22, //     INPUT (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x2A, //     USAGE (RemainingTimeLimit)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x05, 0x84, //     USAGE_PAGE (Power Device) ====================
    0x85, HID_PD_DELAYBE4SHUTDOWN, //     REPORT_ID (18)
    0x09, 0x57, //     USAGE (DelayBeforeShutdown)
    0x16, 0x00, 0x80, //     LOGICAL_MINIMUM (-32768)
    0x27, 0xFF, 0x7F, 0x00, 0x00, //     LOGICAL_MAXIMUM (32767)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_DELAYBE4REBOOT, //     REPORT_ID (19)
    0x09, 0x55, //     USAGE (DelayBeforeReboot)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_CONFIGVOLTAGE, //     REPORT_ID (10)
    0x09, 0x40, //     USAGE (ConfigVoltage)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     LOGICAL_MAXIMUM (65535)
    0x67, 0x21, 0xD1, 0xF0, 0x00, //     UNIT (Centivolts)
    0x55, 0x05, //     UNIT_EXPONENT (5)
    0xB1, 0x23, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Nonvolatile, Bitfield)
    0x85, HID_PD_VOLTAGE, //     REPORT_ID (11)
    0x09, 0x30, //     USAGE (Voltage)
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x30, //     USAGE (Voltage)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x85, HID_PD_CYCLECOUNT, //     REPORT_ID (1D)
    0x09, 0x6B, //     USAGE (CycleCount)  
    0x81, 0xA3, //     INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x6B, //     USAGE (CycleCount)
    0xB1, 0xA3, //     FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)  
    0x85, HID_PD_AUDIBLEALARMCTRL, //     REPORT_ID (20)
    0x09, 0x5A, //     USAGE (AudibleAlarmControl)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x15, 0x01, //     LOGICAL_MINIMUM (1)
    0x25, 0x03, //     LOGICAL_MAXIMUM (3)
    0x65, 0x00, //     UNIT (0)
    0x55, 0x00, //     UNIT_EXPONENT (0)
    0x81, 0x22, //     INPUT (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x5A, //     USAGE (AudibleAlarmControl)
    0xB1, 0xA2, //     FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x02, //     USAGE (PresentStatus)
    0xA1, 0x02, //     COLLECTION (Logical)
    0x85, HID_PD_PRESENTSTATUS, //       REPORT_ID (7)
    0x05, 0x85, //       USAGE_PAGE (Battery System) =================
    0x09, 0x44, //       USAGE (Charging)
    0x75, 0x01, //       REPORT_SIZE (1)
    0x15, 0x00, //       LOGICAL_MINIMUM (0)
    0x25, 0x01, //       LOGICAL_MAXIMUM (1)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x44, //       USAGE (Charging)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x45, //       USAGE (Discharging)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x45, //       USAGE (Discharging)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0xD0, //       USAGE (ACPresent)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0xD0, //       USAGE (ACPresent)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0xD1, //       USAGE (BatteryPresent)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0xD1, //       USAGE (BatteryPresent)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x42, //       USAGE (BelowRemainingCapacityLimit)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x42, //       USAGE (BelowRemainingCapacityLimit)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x43, //       USAGE (RemainingTimeLimitExpired)
    0x81, 0xA2, //       INPUT (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x43, //       USAGE (RemainingTimeLimitExpired)
    0xB1, 0xA2, //       FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)  
    0x09, 0x4B, //       USAGE (NeedReplacement)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x4B, //       USAGE (NeedReplacement)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)    
    0x09, 0xDB, //       USAGE (VoltageNotRegulated)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0xDB, //       USAGE (VoltageNotRegulated)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x46, //       USAGE (FullyCharged)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x46, //       USAGE (FullyCharged)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x47, //       USAGE (FullyDischarged)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x47, //       USAGE (FullyDischarged)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)    
    0x05, 0x84, //       USAGE_PAGE (Power Device) =================
    0x09, 0x68, //       USAGE (ShutdownRequested)
    0x81, 0xA2, //       INPUT (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x68, //       USAGE (ShutdownRequested)
    0xB1, 0xA2, //       FEATURE (Data, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x69, //       USAGE (ShutdownImminent)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x69, //       USAGE (ShutdownImminent)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x73, //       USAGE (CommunicationLost)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x73, //       USAGE (CommunicationLost)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x09, 0x65, //       USAGE (Overload)
    0x81, 0xA3, //       INPUT (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Bitfield)
    0x09, 0x65, //       USAGE (Overload)
    0xB1, 0xA3, //       FEATURE (Constant, Variable, Absolute, No Wrap, Linear, No Preferred, No Null Position, Volatile, Bitfield)
    0x95, 0x02, //       REPORT_COUNT (2) // padding bits to make the report byte aligned
    0x81, 0x01, //       INPUT (Constant, Array, Absolute)
    0xB1, 0x01, //       FEATURE (Constant, Array, Absolute, No Wrap, Linear, Preferred State, No Null Position, Nonvolatile, Bitfield)
    0xC0,       //     END_COLLECTION
    0xC0,       //   END_COLLECTION
    0xC0        // END_COLLECTION
};

HIDPowerDevice::HIDPowerDevice(void)
{
  static bool initialized = false;
  if(!initialized)
  {
    initialized = true;
    HID.addDevice(this, sizeof(report_descriptor));
    USB.productName("USB-UPS-GM");
    USB.serialNumber("UPS01234");
    USB.manufacturerName("Goldenmate");
    USB.usbPower(70);
  }

  USB.begin();
}

uint16_t HIDPowerDevice::_onGetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len)
{
  switch(report_id)
  {
    case HID_PD_PRESENTSTATUS:
      buffer[0] = _PresentStatus[0];
      buffer[1] = _PresentStatus[1];
      return 2;
    case HID_PD_RECHARGEABLE:
      buffer[0] = 1;
      return 1;
    case HID_PD_CAPACITYMODE:
      buffer[0] = 2; // percent mode
      return 1;
    case HID_PD_FULLCHRGECAPACITY:
      buffer[0] = 100;
      return 1;
    case HID_PD_REMAININGCAPACITY:
      buffer[0] = _RemainingCap;
      return 1;
    case HID_PD_DESIGNCAPACITY:
      buffer[0] = 100;
      return 1;
    case HID_PD_CPCTYGRANULARITY1:
      buffer[0] = 1; // 1% granularity
      return 1;
    case HID_PD_CPCTYGRANULARITY2:
      buffer[0] = 1;
      return 1;
    case HID_PD_REMNCAPACITYLIMIT:
      buffer[0] = 5;
      return 1;
    case HID_PD_WARNCAPACITYLIMIT:
      buffer[0] = 10;
      return 1;
    case HID_PD_CONFIGVOLTAGE:
      {
        uint16_t nConfigVoltage = 1380;
        buffer[0] = (nConfigVoltage & 0xFF);
        buffer[1] = (nConfigVoltage >> 8);
      }
      return 2;
    case HID_PD_VOLTAGE:
      buffer[0] = (_nVolts & 0xFF);
      buffer[1] = (_nVolts >> 8);
      return 2;
    case HID_PD_IDEVICECHEMISTRY:
      buffer[0] = IDEVICECHEMISTRY; // 10 byte string. Undefined
      return 1;
    case HID_PD_MANUFACTUREDATE:
      {
        uint16_t *pDate = (uint16_t*)&_mfgDate;
        buffer[0] = (*pDate & 0xFF);
        buffer[1] = (*pDate >> 8);
      }
      return 2;
    case HID_PD_CYCLECOUNT:
      buffer[0] = (_nCycleCount & 0xFF);
      buffer[1] = (_nCycleCount >> 8);
      return 2;

    case HID_PD_AVERAGETIME2FULL:
      buffer[0] = (_nTimeToFull & 0xFF);
      buffer[1] = (_nTimeToFull >> 8);
      return 2;
    case HID_PD_AVERAGETIME2EMPTY:
      buffer[0] = (_nTimeToEmpty & 0xFF);
      buffer[1] = (_nTimeToEmpty >> 8);
      return 2;
    case HID_PD_RUNTIMETOEMPTY:
      buffer[0] = (_nTimeRemain & 0xFF);
      buffer[1] = (_nTimeRemain >> 8);
      return 2;
/*
    case HID_PD_DELAYBE4SHUTDOWN:
      return 2;
    case HID_PD_DELAYBE4REBOOT:
      return 2;
    case HID_PD_IOEMINFORMATION:
      break;
    case HID_PD_IPRODUCT:
      break;
*/
  }

  return 0;
}

uint16_t HIDPowerDevice::_onSetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len)
{
/*  switch(report_id)
  {
    case HID_PD_DELAYBE4SHUTDOWN:
      return 2;
    case HID_PD_DELAYBE4REBOOT:
      return 2;
  }
*/
  return 0;
}

void HIDPowerDevice::SetPresentStatus(uint16_t status, uint8_t cap, uint16_t v)
{
  _PresentStatus[0] = (uint8_t)(status & 0xFF);
  _PresentStatus[1] = (uint8_t)(status >> 8);
  _RemainingCap = cap;
  _nVolts = v * 10; // centivolts
}

void HIDPowerDevice::setMfgDate(const manufactDate& mfd)
{
  _mfgDate = mfd;
}

void HIDPowerDevice::setTimes(uint16_t nFullTime, uint16_t nSecondsRemaining, uint16_t nSecsToCharge)
{
  _nTimeToEmpty = nFullTime;
  _nTimeRemain = nSecondsRemaining;
  _nTimeToFull = nSecsToCharge;
}

void HIDPowerDevice::setCycleCnt(uint16_t nCyc)
{
  _nCycleCount = nCyc;
}

uint16_t HIDPowerDevice::_onGetDescriptor(uint8_t* buffer)
{
  memcpy(buffer, report_descriptor, sizeof(report_descriptor));
  return sizeof(report_descriptor);
}

#endif
