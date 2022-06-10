// Power Module with voltage and current read out and automatic switch-off in case of over-current
// Whizzbizz.com - Arnoud van Delden - June 2022
//
// Module build with a Arduino Pro Mini Atmega328P 5V 16Mhz board in a fischertechnik Silberling case
// Sketch to be uploaded with FTDI FT232RL USB To TTL Serial IC Adapter Converter
// or other USB to TTL interface (e.g. Arduino Uno with chip removed)
//
// Display 128x64 SSD1306 OLED - AdaFruit outputr: https://learn.adafruit.com/adafruit-gfx-graphics-library/
// INA3221 Library based on version 1.2 from SwitchDoc Labs (September 2019) www.switchdoc.com

#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_SSD1306.h>
#include "SDL_Arduino_INA3221.h"
#include "button.h"

// Initializing TINA3221 and SSD1306 screen
Adafruit_SSD1306 display(128, 64, &Wire, -1); // 0x3C on I2C bus
SDL_Arduino_INA3221 ina3221;  // Default address 0x40 on I2C bus

// The three channels/busses of the INA3221
#define VOLTAGE_MAIN_12V 1
#define VOLTAGE_9V 2
#define VOLTAGE_5V 3

// Static calibration offset for current per bus, will be exemplaric...
#define CAL1 10.4 // 12v bus: Compensate for 9, 5 and 3.3 voltages that are being produced from the fuse-switched measured 12v
#define CAL2 1.2  // 9v
#define CAL3 0.8  // 5v
#define DEFAULT_LIMIT 1500

// Define I/O pins
#define BUS1_LED       7
#define BUS2_LED       6
#define BUS3_LED       5
#define READ_CRITICAL  8
#define AUTO_FUSE      9

#define BUTTON_DECR    10
#define BUTTON_INCR    11
#define TOGGLE_SETTING 12 // Toggle to and from current limit setting
#define RESET_STORE    A0 // Digital 13 is connected LED on Pro Mini, so can't be used...

Button buttonDecrease; // Screen mode cycle left and current limit down
Button buttonIncrease; // Screen mode cycle right and current limit up
Button limitSetting;   // Toggle between live read out and current limit setting mode for this bus
Button resetStore;     // Reset current peak hold in live read out, or store setting to EEPROM in limit setting mode
unsigned long relaseWait;
#define holdTime 1000

bool logging = true;      // Serial logging during development...
bool ext_logging = false; // Show all bus values during debugging...
uint8_t displayMode = 1;  // 0=Off, 1=Main, 2=9v bus, 3=5v bus, 4=All busses
char displayVal[20];
char floatVal[10];

uint16_t binValue;
//uint16_t currentLimit = 1500;
//uint16_t currentLimitSaved;
bool autoFuse = false;

struct INA3221_Bus {
  char* Label;
  uint8_t Channel;
  float shuntvoltage = 0;
  float busvoltage = 0;
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
  if (logging) Serial.println(F("Auto-fusing power module with display"));

  // Init display and draw input-/output boxes...
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();

  // Set pin-modes...
  pinMode(READ_CRITICAL, INPUT);
  pinMode(AUTO_FUSE, OUTPUT);

  buttonDecrease.begin(BUTTON_DECR);
  buttonIncrease.begin(BUTTON_INCR);
  limitSetting.begin(TOGGLE_SETTING);
  resetStore.begin(RESET_STORE);
  
  pinMode(BUS1_LED, OUTPUT);
  pinMode(BUS2_LED, OUTPUT);
  pinMode(BUS3_LED, OUTPUT);
  digitalWrite(BUS1_LED, HIGH);
  digitalWrite(BUS2_LED, HIGH);
  digitalWrite(BUS3_LED, HIGH);

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
  delay(200);

  // Get (or init) critial values for the bus(ses)...
  EEPROM.get(0, Bus1.currentLimit);
  if (Bus1.currentLimit==65535) {
    Bus1.currentLimit = 1500;
    EEPROM.put(0, Bus1.currentLimit); // Init, only once...
    if (logging) Serial.println(F("Bus1 current limit had to be initialised")); 
  }
  Bus1.currentLimitSaved = Bus1.currentLimit;
  ina3221.setCriticalAlertValue(Bus1.currentLimit+Bus1.currentOffset, Bus1.Channel); // Summed bus current in mA, default = 1500 (1.5A)
  EEPROM.get(2, Bus2.currentLimit);
  if (Bus2.currentLimit==65535) {
    Bus2.currentLimit = 1000;
    EEPROM.put(2, Bus2.currentLimit); // Init, only once...
    if (logging) Serial.println(F("Bus2 current limit had to be initialised")); 
  }
  Bus2.currentLimitSaved = Bus2.currentLimit;
  ina3221.setCriticalAlertValue(Bus2.currentLimit+Bus2.currentOffset, Bus2.Channel); // Part of max current set above
  EEPROM.get(4, Bus3.currentLimit);
  if (Bus3.currentLimit==65535) {
    Bus3.currentLimit = 1000;
    EEPROM.put(4, Bus3.currentLimit); // Init, only once...
    if (logging) Serial.println(F("Bus3 current limit had to be initialised")); 
  }
  Bus3.currentLimitSaved = Bus3.currentLimit;
  ina3221.setCriticalAlertValue(Bus3.currentLimit+Bus3.currentOffset, Bus3.Channel); // Part of max current set above
  
  // Just click relais to signal we're alive...
  delay(1000);                     // Wait for a second
  digitalWrite(AUTO_FUSE, HIGH);   // Activate relais
  delay(1000);                     // Waits for a second
  digitalWrite(AUTO_FUSE, LOW);    // Deactivate relais
  delay(1000);                     // Waits for a second
}

void loop(void) {
  // Read individual busses and check if 11 < Bus1 < 13 volt
  readBus(Bus1);
  readBus(Bus2);
  readBus(Bus3);
  checkInputVoltage();

  if (ext_logging) {
    logBus(Bus1);
    logBus(Bus2);
    logBus(Bus3);
  }

  checkAutoFuse(); // Check over current protection with current setting of 'currentLimit'

  if (resetStore.debounce()) // Reset peak holds...
    currentBus->current_mAMax = 0;

  // Cycle through screen modes...
  if (buttonDecrease.debounce()) {
    displayMode--;
    if (displayMode==255) displayMode = 4;
    if (logging) Serial.println(displayMode);
  }
  if (buttonIncrease.debounce()) {
    displayMode++;
    if (displayMode>4) displayMode = 0;
    if (logging) Serial.println(displayMode);
  }
  displayValues(displayMode); // Display and set currentBus...

  if ((displayMode==1 || displayMode==2 || displayMode==3) && limitSetting.debounce()) {
    // Enter current limit setting...
    display.clearDisplay();
    display.setTextSize(2);

    display.drawRect(0, 0, 128, 25, WHITE);
    sprintf(displayVal, "%s LIMIT", currentBus->Label);
    display.setCursor(65-(strlen(displayVal)*6),5);
    display.print(displayVal);

    while (!limitSetting.debounce()) {
      display.setTextSize(1);      
      if (currentBus->currentLimitSaved==currentBus->currentLimit) {
        display.setCursor(65-(12*6)/2,32);
        display.print(F("-- STORED --"));
      } else {
        display.setCursor(65-(14*6)/2,32);
        display.print(F("-- TEMPORARY --"));      
      }

      if (buttonDecrease.debounce()) {
        if (currentBus->currentLimit>10) currentBus->currentLimit -= 10;
        if (logging) Serial.println("-10");
        relaseWait = millis();
        while (digitalRead(BUTTON_DECR)==LOW) {
          if (millis()>relaseWait+holdTime) { // Go repeating, button was hold...
            while (digitalRead(BUTTON_DECR)==LOW) {
              if (currentBus->currentLimit>110) currentBus->currentLimit -= 100;
              displayCurrentLimit();
              delay(200);
            }          
          }
        }
      }
      if (buttonIncrease.debounce()) {
        if (currentBus->currentLimit<DEFAULT_LIMIT) currentBus->currentLimit += 10;
        if (logging) Serial.println("+10");
        relaseWait = millis();
        while (digitalRead(BUTTON_INCR)==LOW) {
          if (millis()>relaseWait+holdTime) { // Go repeating, button was hold...
            while (digitalRead(BUTTON_INCR)==LOW) {
              if (currentBus->currentLimit<DEFAULT_LIMIT-100) currentBus->currentLimit += 100;
              displayCurrentLimit();
              delay(200);
            }          
          }
        }
      }
      ina3221.setCriticalAlertValue(currentBus->currentLimit+currentBus->currentOffset, currentBus->Channel);
      displayCurrentLimit();
      checkAutoFuse(); // Check over current protection with current setting of 'currentLimit'
      checkSaveLimitSetting();
    }
  }
}

void checkSaveLimitSetting() {
  if (resetStore.debounce()) {
    EEPROM.put((displayMode-1)*2, currentBus->currentLimit);
    currentBus->currentLimitSaved = currentBus->currentLimit;
  }
}

void checkAutoFuse() {
  if (digitalRead(READ_CRITICAL)==LOW) {
    // Current too high, activate automatic fuse!
    displayMode = readCriticalStatus(); // Set mode to the Bus that was critial
    digitalWrite(AUTO_FUSE, HIGH); 
    autoFuse = true;
    
    // Display Warning!
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE,BLACK);
    display.setCursor(65-(11*6)/2,32);
    display.print(F("Please RESET")); 
    
    display.setTextSize(2);
    display.drawRect(0, 0, 128, 25, WHITE);
    sprintf(displayVal, "%s FUSED", currentBus->Label);
    display.setCursor(65-(strlen(displayVal)*6),6);
    display.print(displayVal);
    
    while(autoFuse) { // Display until reset...
      if (resetStore.debounce()) {
        digitalWrite(AUTO_FUSE, LOW);
        autoFuse = false;
        continue;
      }
      displayCurrentLimit();
      delay(500);
      if (resetStore.debounce()) {
        digitalWrite(AUTO_FUSE, LOW);
        autoFuse = false;
        continue;
      }      
      display.fillRect(0, 48, 128, 14, BLACK);
      display.display();
      delay(500);
    }    
  }
}

void displayCurrentLimit() {
  display.setTextSize(2);
  display.fillRect(0, 48, 128, 14, BLACK);
  sprintf(displayVal, "%s mA", dtostrf(currentBus->currentLimit, 2, 2, floatVal));
  display.setCursor(65-(strlen(displayVal)*6),48);
  display.print(displayVal);
  display.display();
}

void displayValues(uint8_t mode) { // Also sets the currentBus based on the displayMode!
  digitalWrite(BUS1_LED, LOW);
  digitalWrite(BUS2_LED, LOW);
  digitalWrite(BUS3_LED, LOW);

  display.clearDisplay();
  switch (mode) { // Mode=0 is blanked screen
    case 1: // Main bus (12v)
      digitalWrite(BUS1_LED, HIGH);
      currentBus = &Bus1;
      showOneBus(*currentBus);
      break;
    case 2: // 9v bus
      digitalWrite(BUS2_LED, HIGH);
      //showOneBus(Bus2);
      currentBus = &Bus2;
      showOneBus(*currentBus);
      break;      
    case 3: // 5v bus
      digitalWrite(BUS3_LED, HIGH);
      //showOneBus(Bus3);
      currentBus = &Bus3;      
      showOneBus(*currentBus);
      break;
    case 4: // All busses
      digitalWrite(BUS1_LED, HIGH);
      digitalWrite(BUS2_LED, HIGH);
      digitalWrite(BUS3_LED, HIGH);
      showAllBusses();
      currentBus = &Bus1; // Safety catch...
  }
  display.display();
}
void showAllBusses() {
  display.setTextSize(1);
  display.setTextColor(WHITE,BLACK);

  sprintf(displayVal, "%s OUT", Bus1.Label);
  display.setCursor(60-(strlen(displayVal)*6),1);
  display.print(displayVal);
  display.setCursor(65,1);
  display.print(Bus1.loadvoltage);                
  display.print(" V");
  display.setCursor(65,11);
  display.print(Bus1.current_mA);                
  display.print(" mA");

  sprintf(displayVal, "%s OUT", Bus2.Label);
  display.setCursor(60-(strlen(displayVal)*6),23);
  display.print(displayVal);
  display.setCursor(65,23);
  display.print(Bus2.loadvoltage);                
  display.print(" V");
  display.setCursor(65,33);
  display.print(Bus2.current_mA);                
  display.print(" mA");

  sprintf(displayVal, "%s OUT", Bus3.Label);
  display.setCursor(60-(strlen(displayVal)*6),45);
  display.print(displayVal);
  display.setCursor(65,45);
  display.print(Bus3.loadvoltage);                
  display.print(" V");
  display.setCursor(65,55);
  display.print(Bus3.current_mA);                
  display.print(" mA");
}

void readBus(INA3221_Bus &Bus) {
  Bus.busvoltage = fabs(ina3221.getBusVoltage_V(Bus.Channel));
  Bus.shuntvoltage = fabs(ina3221.getShuntVoltage_mV(Bus.Channel));
  Bus.current_mA = fabs(ina3221.getCurrent_mA(Bus.Channel)-Bus.currentOffset);
  if (Bus.current_mA > Bus.current_mAMax) Bus.current_mAMax = Bus.current_mA;
  Bus.loadvoltage = fabs(Bus.busvoltage + (Bus.shuntvoltage / 1000));
  //if (Bus.loadvoltage > Bus.loadvoltageMax) Bus.loadvoltageMax = Bus.loadvoltage;
}

void showOneBus(INA3221_Bus Bus) {
  display.setTextSize(2);
  display.setTextColor(WHITE,BLACK);
  
  sprintf(displayVal, "%s", dtostrf(Bus.loadvoltage, 2, 2, floatVal));
  display.setCursor(90-(strlen(displayVal)*12),0);
  display.print(displayVal);
  display.setCursor(95,0);
  display.print("V");
  display.drawRect(0, 16, 128, 1, WHITE);

  sprintf(displayVal, "%s", dtostrf((Bus.loadvoltage*Bus.current_mA), 2, 2, floatVal));
  display.setCursor(90-(strlen(displayVal)*12),18);
  display.print(displayVal);
  display.setCursor(95,18);
  display.print("mW");
  
  sprintf(displayVal, "%s", dtostrf(Bus.current_mA, 2, 2, floatVal));
  display.setCursor(90-(strlen(displayVal)*12),34);
  display.print(displayVal);
  display.setCursor(95,34);
  display.print("mA");

  sprintf(displayVal, "%s", dtostrf(Bus.current_mAMax, 2, 2, floatVal));
  display.setCursor(90-(strlen(displayVal)*12),50);
  display.print(displayVal);
  display.setCursor(95,56);
  display.setTextSize(1);
  display.print("PEAK");   
}

void logBus(INA3221_Bus Bus) {
  Serial.println(Bus.Label); 
  Serial.print(F("Bus Voltage:   ")); Serial.print(Bus.busvoltage); Serial.println(" V");
  Serial.print(F("Shunt Voltage: ")); Serial.print(Bus.shuntvoltage); Serial.println(" mV");
  Serial.print(F("Load Voltage:  ")); Serial.print(Bus.loadvoltage); Serial.println(" V");
  Serial.print(F("Current:       ")); Serial.print(Bus.current_mA); Serial.println(" mA");
  Serial.println("");
}

void checkInputVoltage() {
  // Test if main input voltage is within limits...
  if (Bus1.busvoltage < 11 || Bus1.busvoltage > 13) {
    if (logging) {
     Serial.print(F("MAIN voltage of ")); 
     Serial.print(Bus1.busvoltage); 
     Serial.println(F(" V not within range!")); 
    }
    digitalWrite(AUTO_FUSE, HIGH);
    autoFuse = true;
     
    // Display Warning!
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE,BLACK);
    display.setCursor(64-(15*6)/2,50);
    display.print(F("Connect ~12V DC"));
    //display.display();
    display.setCursor(65-(11*6)/2,32);
    display.print(F("Please RESET"));     
    
    display.setTextSize(2);
    display.drawRect(0, 0, 128, 43, WHITE);
    display.setCursor(64-(5*12)/2,6);
    display.print(F("INPUT"));

    while(autoFuse) { // Display until reset...
      if (resetStore.debounce()) {
        digitalWrite(AUTO_FUSE, LOW);
        autoFuse = false;
        continue;
      }
 
      sprintf(displayVal, "%s V", dtostrf(Bus1.busvoltage, 2, 2, floatVal));
      display.setCursor(64-(strlen(displayVal)*12)/2,23);
      display.print(displayVal);
      display.display();
      delay(500);

      display.setCursor(64-(8*12)/2,23);
      display.print(F("        "));
      display.display();
      delay(500);
    }
  }
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
