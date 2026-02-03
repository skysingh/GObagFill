/* LED Blink, Teensyduino Tutorial #1
   http://www.pjrc.com/teensy/tutorial.html
 
   1/5/26 remapped pins for teensy LC
   1/5/26 use opto IO
   1/22/26 converted to M5Stack 4-Relay I2C module
*/
#include <FreqMeasureMulti.h>
#include <Wire.h>

const float ver = 1.0;

// I2C 4-Relay Module
#define MODULE_4RELAY_ADDR 0x26
#define RELAY_REG_MODE      0x10
#define RELAY_REG_RELAY     0x11

// ============================================================================
// 4-Relay Module Class
// ============================================================================
class Module4Relay {
private:
    uint8_t _addr;
    uint8_t _relayState;
    
    bool writeReg(uint8_t reg, uint8_t data) {
        Wire.beginTransmission(_addr);
        Wire.write(reg);
        Wire.write(data);
        return (Wire.endTransmission() == 0);
    }

public:
    Module4Relay() : _addr(MODULE_4RELAY_ADDR), _relayState(0) {}
    
    bool begin(uint8_t addr = MODULE_4RELAY_ADDR) {
        _addr = addr;
        Wire.begin();
        
        Wire.beginTransmission(_addr);
        if (Wire.endTransmission() != 0) return false;
        
        writeReg(RELAY_REG_MODE, 0x01);  // SYNC mode - LEDs follow relays
        _relayState = 0x00;
        writeReg(RELAY_REG_RELAY, _relayState);
        return true;
    }
    
    bool setRelay(uint8_t index, bool state) {
        if (index > 3) return false;
        if (state) _relayState |= (1 << index);
        else _relayState &= ~(1 << index);
        return writeReg(RELAY_REG_RELAY, _relayState);
    }
    
    bool setAllRelay(bool state) {
        _relayState = state ? 0x0F : 0x00;
        return writeReg(RELAY_REG_RELAY, _relayState);
    }
};

Module4Relay RELAY;

// Measure 4 frequencies at the same time! :-)
FreqMeasureMulti freq0;
FreqMeasureMulti freq1;
FreqMeasureMulti freq2;
FreqMeasureMulti freq3;

const int ledPin = 13;
const int SWpin[] = {7,8,11,12};
const int FMpin[] = {23,22,20,17};     // these are pins supported by freqmeasuremulti
float count2ml[] = {0.235,0.24,0.215,0.245};    // calibration constants

int SWState[4];                   // start switchs
int lastSWState[4];               // for debounce
float PVml[4];                      // ml dispensed
int count[4];                     // all zero based arrays
int sum[4];
int cum_count[4];
int pumpStatus[4] = {-1,-1,-1,-1};  // status 0= ready, 1= dispensing, 2= complete
boolean all_empty = false;
int SPml = 375;                   // setpont in mL


unsigned long onesecMillis = millis();
unsigned long loopMillis = millis();
const int fastmS = 100;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println();
  Serial.print("4-Pump Dispenser v"); Serial.println(ver);
  Serial.println("==========================");
  
  // Initialize I2C Relay Module
  Serial.print("Init 4-Relay Module... ");
  if (!RELAY.begin(MODULE_4RELAY_ADDR)) {
    Serial.println("FAILED! Check I2C: SDA=18, SCL=19");
    while (1) {
      digitalWrite(ledPin, !digitalRead(ledPin));
      delay(100);
    }
  }
  Serial.println("OK!");
  RELAY.setAllRelay(false);  // all relays off
  
  freq0.begin(FMpin[0]);
  freq1.begin(FMpin[1]);
  freq2.begin(FMpin[2]);
  freq3.begin(FMpin[3]);
  
  // initialize the digital pin as an output.
  for (int i=0;i<4;i++) {
    pinMode(SWpin[i],INPUT_PULLUP);
    pumpStatus[i]=0;                      // set all to ready  
    PVml[i] = 0;   
  }
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);   // set the LED on
  for (int i=0;i<4;i++) {                   // fake flow meter
      Serial.print(i);Serial.print(",");Serial.print(digitalRead(SWpin[i]));
      delay(1000);
        
  }
  Serial.println();Serial.println("READY");
}

void loop() {
  if (freq0.available()) {
    sum[0] = sum[0] + freq0.read();
    count[0] = count[0] + 1;
    cum_count[0] = cum_count[0] + 1;      // retained cumulative 
    PVml[0] =cum_count[0] * count2ml[0];
  }
  if (freq1.available()) {
    sum[1] = sum[1] + freq1.read();
    count[1] = count[1] + 1;
    cum_count[1] = cum_count[1] + 1;      // retained cumulative
    PVml[1] = cum_count[1] * count2ml[1];    // conevrt to mL
  }
  if (freq2.available()) {
    sum[2] = sum[0] + freq2.read();
    count[2] = count[2] + 1;
    cum_count[2] = cum_count[2] + 1;      // retained cumulative
    PVml[2] = cum_count[2] * count2ml[2];    // conevrt to mL
  }
  if (freq3.available()) {
    sum[3] = sum[3] + freq3.read();
    count[3] = count[3] + 1;
    cum_count[3] = cum_count[3] + 1;      // retained cumulative
    PVml[3] = cum_count[3] * count2ml[3];    // conevrt to mL
  }
  for (uint8_t i = 0; i < 4; i++) { 
    if (pumpStatus[i] == 1) checkPump(i);  // check sp reached only is pump running  
  }
  if (millis() - loopMillis > fastmS) {
   loopMillis = millis();
   for (uint8_t i = 0; i < 4; i++) { 
      checkSwitch(i);
      switch (pumpStatus[i]) {
          case 0:                                //pump is in idle-ready
            if (SWState[i] == 0) {
              pumpStatus[i] = 1;                // start the pump
            }
            break;
        
          case 2 ... 3:                             // complete or abort  - waiting for reset   
            if (!SWState[i] == 0) {
              pumpStatus[i] = 0;                // reset the dispense
              PVml[i] = 0.0;
              cum_count[i] = 0;                // reset the retained count
            } 
            break;     
        }   // end switch      
   }
  }  
  if ((millis() - onesecMillis) > 1000) {         // reporting only
    onesecMillis = millis();
    for (int i=0;i<4;i++) {                   // 
      count[i]=0;sum[i]=0;                    // reset flowrate counters    
      if (pumpStatus[i] ==1 ) {
         Serial.print("P: ");Serial.print(i);Serial.print(" ");Serial.print(PVml[i]);Serial.print(" / "); Serial.println(cum_count[i]);
      }  
    }
    Serial.println();      
    Serial.print("switches: ");
    for (int i=0;i<4;i++) {
      Serial.print(SWState[i]);Serial.print(",");  
    }
    Serial.println();Serial.print("pumpstatus"); 
    for (int i=0;i<4;i++) {
       Serial.print(pumpStatus[i]);Serial.print(","); 
    }
    
    Serial.println();Serial.print("mL pumped"); 
    for (int i=0;i<4;i++) {
       Serial.print(PVml[i]);Serial.print(",");
    }
    Serial.println("count-- ");
    for (int i=0;i<4;i++) {
       Serial.print(cum_count[i]);Serial.print(",");
    }
    Serial.println();
  }
}


// =======================   SUBROUTINES ========================

void checkSwitch(int num) {
  // debounce 
  int reading = digitalRead(SWpin[num]);
  //Serial.print(reading);Serial.print(",");Serial.println(lastSWState[num]);
  //has the switch changed state ?
  if (reading != lastSWState[num]) {
    lastSWState[num] = reading;
    if (reading == 1) SWState[num] = 1;else SWState[num]=0;
  }
} //END of   checkSwitches()

boolean checkPump(int pumpIndex) {
  boolean done = false;
  if (SWState[pumpIndex] == 0) {                        // bag is on holder  
    if (int(PVml[pumpIndex]) >= SPml) {                 // reached the dispense volume
      RELAY.setRelay(pumpIndex, false);                 // pump Off
      pumpStatus[pumpIndex] = 2;                        // complete  
      done = true;  
    }
    else {
      RELAY.setRelay(pumpIndex, true);                  // keep pump running
    }
  }
  else {
      RELAY.setRelay(pumpIndex, false);                 // pump Off
      pumpStatus[pumpIndex] = 3;                        // aborted     
  }
  return done;
}
