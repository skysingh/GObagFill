/*
 * 4-Relay Module Auto Sequence Test for Teensy 3.1/LC
 * M5Stack 4-Relay Module (I2C address 0x26)
 * 
 * Wiring:
 * - SDA: Pin 18
 * - SCL: Pin 19
 * 
 * Sequences through each relay for 5 seconds
 * LEDs sync with relay states
 */

#include <Arduino.h>
#include <Wire.h>

#define MODULE_4RELAY_ADDR 0x26
#define RELAY_REG_MODE      0x10
#define RELAY_REG_RELAY     0x11
#define LED_PIN    13
#define RELAY_ON_TIME  5000

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
};

Module4Relay RELAY;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    Serial.println("4-Relay Auto Sequence Test");
    
    if (!RELAY.begin(MODULE_4RELAY_ADDR)) {
        Serial.println("FAILED! Check I2C: SDA=18, SCL=19");
        while (1) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(100);
        }
    }
    
    Serial.println("OK - 5 sec per relay");
    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    for (uint8_t i = 0; i < 4; i++) {
        Serial.print("Relay "); Serial.print(i + 1); Serial.println(" ON");
        RELAY.setRelay(i, true);
        delay(RELAY_ON_TIME);
        RELAY.setRelay(i, false);
    }
    delay(1000);
}
