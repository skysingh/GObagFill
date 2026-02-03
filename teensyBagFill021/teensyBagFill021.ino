/* 4-Pump Batch Fill Controller for Teensy LC
   Version 2.1 - Serial HMI Communication
   
   1/5/26 remapped pins for teensy LC
   1/5/26 use opto IO
   1/22/26 converted to M5Stack 4-Relay I2C module
   1/22/26 added Serial1 TX/RX for RPI4 HMI communication
   
   Hardware:
   - Teensy LC
   - M5Stack 4-Relay I2C Module (0x26)
   - Serial1: TX1(pin1) RX1(pin0) -> RPI4 HMI
   
   Protocol (to RPI):
   - JSON status every 500ms
   
   Protocol (from RPI):
   - SP=xxx    : Set setpoint
   - STARTn    : Start channel n (1-4)
   - STOPn     : Stop channel n (1-4)
*/

#include <FreqMeasureMulti.h>
#include <Wire.h>

const float ver = 2.1;

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
    
    bool getRelayState(uint8_t index) {
        if (index > 3) return false;
        return (_relayState >> index) & 0x01;
    }
};

Module4Relay RELAY;

// Frequency measurement
FreqMeasureMulti freq0;
FreqMeasureMulti freq1;
FreqMeasureMulti freq2;
FreqMeasureMulti freq3;

// Pin assignments
const int ledPin = 13;
const int SWpin[] = {7, 8, 11, 12};
const int FMpin[] = {23, 22, 20, 17};

// Calibration
float count2ml[] = {0.235, 0.24, 0.215, 0.245};

// State variables
int SWState[4];
int lastSWState[4];
float PVml[4];
int count[4];
int sum[4];
int cum_count[4];
int pumpStatus[4] = {0, 0, 0, 0};  // 0=IDLE, 1=RUNNING, 2=COMPLETE, 3=FAILED
bool latched[4] = {false, false, false, false};  // HMI start latch
int SPml = 375;  // Setpoint in mL

// Timing
unsigned long onesecMillis = millis();
unsigned long loopMillis = millis();
unsigned long jsonMillis = millis();
const int fastmS = 100;
const int jsonInterval = 500;  // Send JSON every 500ms

// Serial RX buffer
String rxBuffer = "";

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);   // Debug USB
  Serial1.begin(115200);  // RPI HMI on TX1/RX1
  delay(500);
  
  Serial.println();
  Serial.print("4-Pump Batch Controller v"); Serial.println(ver);
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
  RELAY.setAllRelay(false);
  
  // Initialize frequency measurement
  freq0.begin(FMpin[0]);
  freq1.begin(FMpin[1]);
  freq2.begin(FMpin[2]);
  freq3.begin(FMpin[3]);
  
  // Initialize pins
  for (int i = 0; i < 4; i++) {
    pinMode(SWpin[i], INPUT_PULLUP);
    pumpStatus[i] = 0;
    PVml[i] = 0;
    latched[i] = false;
  }
  
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  
  Serial.println("Serial1 HMI link active");
  Serial.println("READY");
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  // Read frequency counters
  if (freq0.available()) {
    sum[0] += freq0.read();
    count[0]++;
    cum_count[0]++;
    PVml[0] = cum_count[0] * count2ml[0];
  }
  if (freq1.available()) {
    sum[1] += freq1.read();
    count[1]++;
    cum_count[1]++;
    PVml[1] = cum_count[1] * count2ml[1];
  }
  if (freq2.available()) {
    sum[2] += freq2.read();
    count[2]++;
    cum_count[2]++;
    PVml[2] = cum_count[2] * count2ml[2];
  }
  if (freq3.available()) {
    sum[3] += freq3.read();
    count[3]++;
    cum_count[3]++;
    PVml[3] = cum_count[3] * count2ml[3];
  }
  
  // Check pumps that are running
  for (uint8_t i = 0; i < 4; i++) {
    if (pumpStatus[i] == 1) checkPump(i);
  }
  
  // Fast loop - switch and state management
  if (millis() - loopMillis > fastmS) {
    loopMillis = millis();
    
    for (uint8_t i = 0; i < 4; i++) {
      checkSwitch(i);
      
      switch (pumpStatus[i]) {
        case 0:  // IDLE - waiting for start
          if (SWState[i] == 0 && latched[i]) {
            pumpStatus[i] = 1;  // Start pump
          }
          break;
          
        case 2:  // COMPLETE - waiting for reset
        case 3:  // FAILED - waiting for reset
          if (SWState[i] != 0 && !latched[i]) {
            pumpStatus[i] = 0;  // Reset to IDLE
            PVml[i] = 0.0;
            cum_count[i] = 0;
          }
          break;
      }
    }
  }
  
  // Process incoming serial commands from RPI
  processSerial();
  
  // Send JSON status to RPI
  if (millis() - jsonMillis > jsonInterval) {
    jsonMillis = millis();
    sendJSON();
  }
  
  // Debug output every second
  if (millis() - onesecMillis > 1000) {
    onesecMillis = millis();
    
    for (int i = 0; i < 4; i++) {
      count[i] = 0;
      sum[i] = 0;
    }
    
    // Debug to USB serial
    Serial.print("SP:"); Serial.print(SPml);
    Serial.print(" Status:");
    for (int i = 0; i < 4; i++) {
      Serial.print(pumpStatus[i]); Serial.print(",");
    }
    Serial.print(" mL:");
    for (int i = 0; i < 4; i++) {
      Serial.print(PVml[i], 1); Serial.print(",");
    }
    Serial.println();
  }
}

// ============================================================================
// Serial Communication
// ============================================================================
void processSerial() {
  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (c == '\n' || c == '\r') {
      if (rxBuffer.length() > 0) {
        parseCommand(rxBuffer);
        rxBuffer = "";
      }
    } else {
      rxBuffer += c;
      if (rxBuffer.length() > 32) {
        rxBuffer = "";  // Overflow protection
      }
    }
  }
}

void parseCommand(String cmd) {
  cmd.trim();
  Serial.print("RX: "); Serial.println(cmd);
  
  // Setpoint command: SP=xxx
  if (cmd.startsWith("SP=")) {
    int newSP = cmd.substring(3).toInt();
    if (newSP >= 50 && newSP <= 500) {
      SPml = newSP;
      Serial.print("Setpoint: "); Serial.println(SPml);
    }
  }
  // Start command: START1, START2, START3, START4
  else if (cmd.startsWith("START")) {
    int ch = cmd.substring(5).toInt() - 1;  // Convert to 0-based
    if (ch >= 0 && ch < 4) {
      latched[ch] = true;
      Serial.print("Latched CH"); Serial.println(ch + 1);
    }
  }
  // Stop command: STOP1, STOP2, STOP3, STOP4
  else if (cmd.startsWith("STOP")) {
    int ch = cmd.substring(4).toInt() - 1;  // Convert to 0-based
    if (ch >= 0 && ch < 4) {
      latched[ch] = false;
      RELAY.setRelay(ch, false);  // Immediate stop
      if (pumpStatus[ch] == 1) {
        pumpStatus[ch] = 3;  // Mark as FAILED/aborted
      }
      Serial.print("Stopped CH"); Serial.println(ch + 1);
    }
  }
}

void sendJSON() {
  // Build JSON status for RPI HMI
  String json = "{\"setpoint\":";
  json += SPml;
  json += ",\"channels\":[";
  
  for (int i = 0; i < 4; i++) {
    if (i > 0) json += ",";
    json += "{\"state\":\"";
    
    // State string
    switch (pumpStatus[i]) {
      case 0: json += "IDLE"; break;
      case 1: json += "RUNNING"; break;
      case 2: json += "COMPLETE"; break;
      case 3: json += "FAILED"; break;
      default: json += "IDLE"; break;
    }
    
    json += "\",\"ml\":";
    json += String(PVml[i], 1);
    json += ",\"input\":";
    json += (SWState[i] == 0) ? "true" : "false";
    json += ",\"output\":";
    json += RELAY.getRelayState(i) ? "true" : "false";
    json += ",\"cycles\":0";
    json += ",\"latched\":";
    json += latched[i] ? "true" : "false";
    json += "}";
  }
  
  json += "]}";
  
  Serial1.println(json);
}

// ============================================================================
// Subroutines
// ============================================================================
void checkSwitch(int num) {
  int reading = digitalRead(SWpin[num]);
  if (reading != lastSWState[num]) {
    lastSWState[num] = reading;
    SWState[num] = (reading == 1) ? 1 : 0;
  }
}

boolean checkPump(int pumpIndex) {
  boolean done = false;
  
  if (SWState[pumpIndex] == 0 && latched[pumpIndex]) {
    // Bag on holder and latched
    if (int(PVml[pumpIndex]) >= SPml) {
      RELAY.setRelay(pumpIndex, false);
      pumpStatus[pumpIndex] = 2;  // COMPLETE
      latched[pumpIndex] = false;
      done = true;
    } else {
      RELAY.setRelay(pumpIndex, true);  // Keep running
    }
  } else {
    RELAY.setRelay(pumpIndex, false);
    pumpStatus[pumpIndex] = 3;  // FAILED/aborted
    latched[pumpIndex] = false;
  }
  
  return done;
}
