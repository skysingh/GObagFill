/*
*******************************************************************************
* Copyright (c) 2021 by M5Stack
*                  Equipped with M5Core sample source code
*                          配套  M5Core 示例源代码
* Visit for more information: https://docs.m5stack.com/en/module/4relay
* 获取更多资料请访问: https://docs.m5stack.com/zh_CN/module/4relay
*
* Describe: Module 4Relay.
* Date: 2022/07/22
*******************************************************************************
*/


#include <M5StickCPlus.h>
// #include <Wire.h>
#include "Unit_4RELAY.h"
UNIT_4RELAY relay;

unsigned long lastmillis = millis();


void setup() {
  uint16_t state;
  //auto cfg = M5.config();
  //AtomS3.begin(cfg);
  Serial.begin(115200);
  delay(500);
  while (!relay.begin(&Wire, 32, 33)){
        Serial.println("4relay Connect Error");
        M5.Lcd.setCursor(15, 20);
        M5.Lcd.print("4relay ERR");
        delay(100);
    }
  relay.Init(1);          // Set the lamp and relay to synchronous mode(Async =0,Sync = 1)
  relay.relayAll(0);      // all relays OFF
  Serial.println("RELAY4");
/* 
  AtomS3.Display.setTextColor(GREEN);
  AtomS3.Display.setTextDatum(middle_center);
  AtomS3.Display.setTextFont(&fonts::Orbitron_Light_24);
  AtomS3.Display.setTextSize(1);
  M5.Lcd.printf("RELAY4:\r\n");
  Relay4.begin(Wire1,2,1); //initiate I2C on Wire1, SDA=32, SCL=33
  Relay4.SyncMode(true); //light the LEDs where relays is ON state
  Relay4.AllOff(); //All relays to OFF
  while (true) {
    state=Relay4.ReadState(); //read 0x10 & 0x11 registers
    Serial.printf("State1= %x\n",state);
   // M5.Lcd.println(state);
    delay(1000);
  }  
  */
}

void loop() {
  
 
  for (int i=0;i<4;i++) {
    relay.relayWrite(i, 1);                    // pump Off
    delay(2000);
    relay.relayWrite(i, 0);                   //  OFF
  }
  /*
  relay.relayAll(0);      // all relays OFF
  delay(2000);
  relay.relayAll(1);      // all relays OFF
  delay(2000);
  */
  if ((millis() - lastmillis) > 1000) {
    lastmillis = millis();
  }
 
  
}  

/*  
    switch (mode) {
        case 0:
            RELAY.setAllRelay(true);
            drwaRect();
            if (delayBtn(millis())) {
                RELAY.setAllRelay(false);
                break;
            };
            RELAY.setAllRelay(false);
            drwaRect();
            if (delayBtn(millis())) {
                RELAY.setAllRelay(false);
                break;
            };
            break;
        case 1:
            for (uint8_t i = 0; i < 4; i++) {
                while (!delayBtn(millis()))
                    ;
                if (mode != 1) break;
                RELAY.setRelay(i, true);
                drwaRect();
            }
            for (uint8_t i = 0; i < 4; i++) {
                while (!delayBtn(millis()))
                    ;
                if (mode != 1) break;
                RELAY.setRelay(i, false);
                drwaRect();
            }
            break;
        case 2:
            for (uint8_t i = 0; i < 4; i++) {
                RELAY.setRelay(i, true);
                drwaRect();
                if (delayBtn(millis())) {
                    break;
                };
            }
            for (uint8_t i = 0; i < 4; i++) {
                RELAY.setRelay(i, false);
                drwaRect();
                if (delayBtn(millis())) {
                    break;
                };
            }
            break;
    }
}
*/
