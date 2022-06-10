// Test INA3221
// Whizzbizz.com - Arnoud van Delden - June 2022
// See https://www.whizzbizz.com/en/power-module-part2
//

#include <Wire.h>
#include "SDL_Arduino_INA3221.h"

// Initializing INA3221 
SDL_Arduino_INA3221 ina3221;  // Default address 0x40 on I2C bus

// The three channels/busses of the INA3221
#define VOLTAGE_MAIN_12V 1
#define VOLTAGE_9V 2
#define VOLTAGE_5V 3

// Static calibration offset for current per bus, will be exemplaric...
#define CAL1 0 // 12v bus: Compensate for 9, 5 and 3.3 voltages that are being produced from the fuse-switched measured 12v
#define CAL2 0  // 9v
#define CAL3 0  // 5v

// Define I/O pins
#define BUS1_LED       7
#define BUS2_LED       6
#define BUS3_LED       5
#define READ_CRITICAL  8
#define AUTO_FUSE      9

bool logging = true;      // Serial logging during development...
bool ext_logging = true; // Show all bus values during debugging...

uint16_t binValue;
bool autoFuse = false;

struct INA3221_Bus {
  char* Label;
  uint8_t Channel;
  float shuntvoltage = 0;
  uint16_t shuntvoltage_raw = 0xFF;
  float busvoltage = 0;
  uint16_t busvoltage_raw = 0xFF;
  float current_mA = 0;
  float current_mAMax = 0;
  float currentOffset;
  float loadvoltage = 0;
  uint16_t currentLimit;
  uint16_t currentLimitSaved;
};

INA3221_Bus Bus1, Bus2, Bus3;
INA3221_Bus *currentBus = &Bus1;

void setup(void) {
  if (logging) Serial.begin(115200);
  if (logging) Serial.println(F("Test INA3221"));

  // Set pin-modes...
  pinMode(READ_CRITICAL, INPUT);
  pinMode(AUTO_FUSE, OUTPUT);

  // Initialize busses and INA3221...
  Bus1.Label = "12V";
  Bus1.Channel = VOLTAGE_MAIN_12V;
  Bus1.currentOffset = CAL1;
  Bus2.Label = "9V";
  Bus2.Channel = VOLTAGE_9V;
  Bus2.currentOffset = CAL2;
  Bus3.Label = "5V";
  Bus3.Channel = VOLTAGE_5V;
  Bus3.currentOffset = CAL3;
  ina3221.begin();
  if (logging) {
    Serial.print(F("INA3221 getManufID="));
    Serial.println(ina3221.getManufID());
  }
  delay(200);
}

void loop(void) {
  // Read individual busses
  readBus(Bus1);
  delay(200);
  readBus(Bus2);
  delay(200);
  readBus(Bus3);
  delay(200);

  if (ext_logging) {
    logBus(Bus1);
    logBus(Bus2);
    logBus(Bus3);
  }
  
  delay(1000);
}

void readBus(INA3221_Bus &Bus) {
  Bus.busvoltage = fabs(ina3221.getBusVoltage_V(Bus.Channel));
  Bus.busvoltage_raw = ina3221.getBusVoltage_raw(Bus.Channel);
  Bus.shuntvoltage = fabs(ina3221.getShuntVoltage_mV(Bus.Channel));
  Bus.shuntvoltage_raw = ina3221.getShuntVoltage_raw(Bus.Channel);
  Bus.current_mA = fabs(ina3221.getCurrent_mA(Bus.Channel)-Bus.currentOffset);
  if (Bus.current_mA > Bus.current_mAMax) Bus.current_mAMax = Bus.current_mA;
  Bus.loadvoltage = fabs(Bus.busvoltage + (Bus.shuntvoltage / 1000));
}

void logBus(INA3221_Bus Bus) {
  Serial.print(F("   "));
  Serial.println(Bus.Label); 
  Serial.print(F("   Bus Voltage:       ")); Serial.print(Bus.busvoltage); Serial.println(" V");
  Serial.print(F("   Bus Voltage HEX:   0x")); Serial.println(Bus.busvoltage_raw, HEX);
  Serial.print(F("   Shunt Voltage:     ")); Serial.print(Bus.shuntvoltage); Serial.println(" mV");
  Serial.print(F("   Shunt Voltage HEX: 0x")); Serial.println(Bus.shuntvoltage_raw, HEX);
  Serial.print(F("   Load Voltage:      ")); Serial.print(Bus.loadvoltage); Serial.println(" V");
  Serial.print(F("   Current:           ")); Serial.print(Bus.current_mA); Serial.println(" mA");
  Serial.println("");
}

uint8_t readCriticalStatus() {
  // Reads the mask/enable register of the INA3221 after Critical Warning Alert
  uint16_t binValue;
  ina3221.wireReadRegister(INA3221_MASK_ENABLE, &binValue);
  binValue = (binValue>>7);
  if (binValue==4) {
    currentBus = &Bus1;
    return 1; // Bit2=Bus1
  }
  if (binValue==2) {
    currentBus = &Bus2;
    return 2; // Bit1=Bus2
  }
  if (binValue==1) {
    currentBus = &Bus3;
    return 3; // Bit0=Bus3
  }
  return 0;
}
