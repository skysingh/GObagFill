/*
*******************************************************************************
* fill 4 bags independant start switch and flowmeters
* uses EXTIO2 - 8 inputs
* uases 4RELAY - 4 outputs
* Date: 2022/7/22
*******************************************************************************
*/ 

#include <M5StickCPlus.h>
#include "Unit_4RELAY.h"
#include "M5_EXTIO2.h"

UNIT_4RELAY relay;
M5_EXTIO2 extio;

extio_io_mode_t mode = DIGITAL_INPUT_MODE;



const float count2ml = 10;             // flowmeter converion factor  count to mL
const int SWPIn[] = {0,1,2,3};         // corresponding switch pin extIO
const int flowPin[] = {4,5,6,7};       // pumps pins on extIO 

int SWState[4];                   // start switchs
int Flowcount[4];                // individual cumulative pump counts
int startDelay = 2;               // secs after switch is pressed before pump starts
int SPchoices[3] = {50,125,375};
int SPcommon = 375;
int SPml[4];                      // individual ml setpoint
int PVml[4];                      // ml dispensed
int pumpStatus[4] = {-1,-1,-1,-1};  // status 0= ready, 1= dispensing, 2= complete
int count[4];
int lastPinState[4];
int currentPinState[4];

boolean not_empty = true;

unsigned long onesecMillis = millis();
unsigned long loopMillis = millis();
const int fastmS = 100;


void setup() {
    delay(100);
    M5.begin();             // Init M5StickCPlus.  初始化 M5StickCPlus
    M5.Lcd.setRotation(4);  // Rotate the screen.  旋转屏幕
    M5.Lcd.setTextSize(2);  // Set the text size.  设置文字大小
    M5.Lcd.setCursor(15, 20);  // Set the cursor position to (50,5).  将光标位置设置为(50,5)
    M5.Lcd.print("GOFILL-BG");
    M5.Lcd.setCursor(0, 25);  
    while (!relay.begin(&Wire, 32, 33)){
        Serial.println("4relay Connect Error");
        M5.Lcd.setCursor(15, 20);
        M5.Lcd.print("4relay ERR");
        delay(100);
    }
    relay.Init(1);          // Set the lamp and relay to synchronous mode(Async =0,Sync = 1)
    relay.relayAll(0);      // all relays OFF
     
    while (!extio.begin(&Wire, 32, 33, 0x45)) {
        Serial.println("extio Connect Error");
        M5.Lcd.setCursor(15, 20);
        M5.Lcd.print("extio ERR");
        delay(100);
    }
    extio.setAllPinMode(DIGITAL_INPUT_MODE);
    while (not_empty) {
      not_empty = false;
      for (uint8_t i = 0; i < 4; i++) {                // check all bag holder must be empty on startup
         SWState[i] = 1 - extio.getDigitalInput(i);    // invert GND for 1
         Serial.print(SWState[i]);Serial.print(",");
         if (SWState[i] !=0) not_empty = true;      
      }
      Serial.println();
      delay(1000);
      M5.Lcd.print("REMOVE BAGS !");
      Serial.println(" -------   REMOVE ALL BAGS ----------- ");
    }
    for (int i=0;i<4;i++) {
      pumpStatus[i]=0;                      // set all to ready  
      PVml[i] = 0;
      SPml[i] = SPcommon;                   // default 375
    }
    Serial.println("READY");
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(2, 20);
    M5.Lcd.print("-- READY --");
    delay(2000);
    showDisplay();
}


void loop() { 
    int state;
    for (uint8_t i = 0; i < 4; i++) {      
       SWState[i] = 1 - extio.getDigitalInput(i);    // invert GND for 1
    }
    for (uint8_t i = 0; i < 4; i++) {  
      if (pumpStatus[i] == 1) {                            // particular pump is running  
      currentPinState[i]=extio.getDigitalInput(i+4);       // read the flowmeter pin
        if (currentPinState[i] == HIGH && lastPinState[i] == LOW) { // Check if the state changed from LOW to HIGH (rising edge)
          count[i]++; // Increment the count
          Serial.print("Pulse detected! Count: ");
          Serial.println(count[i]);
        }
        lastPinState[i] = currentPinState[i]; // Save the current state for the next loop iteration
      }  
    }
    if (millis() - loopMillis > fastmS) {
      for (int i=0;i<4;i++) {
        switch (pumpStatus[i]) {
          case 0:                                //pump is in idle-ready
            if (SWState[i]) {
              pumpStatus[i] = 1;                // start the pump
            }
            break;
          case 1:                                // pump is running          
            getPV(i);
            break;
          case 2 ... 3:                             // complete or abort  - waiting for reset   
            if (!SWState[i]) {
              pumpStatus[i] = 0;                // reset the dispense
              PVml[i] = 0;
              count[i] = 0;
            } 
            break;     
        }   // end switch
      }
      loopMillis - millis();
    }
    if ((millis() - onesecMillis) > 1000) {
      onesecMillis = millis();
      showSwitches();
      showPumps();
      for (int i=0;i<4;i++) {                   // fake flow meter
        if (pumpStatus[i] ==1 ) {
          PVml[i] = PVml[i] + 10;
          Serial.print("pump: ");Serial.print(i);Serial.print(" ");Serial.print(PVml[i]);Serial.print(" / "); Serial.print(SPml[i]);
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
      Serial.println();
    }
    M5.update();
}   // ------------------------   end loop -----

void showSwitches() {
   for (int i=0;i<4;i++) {
      if (SWState[i]) {
        M5.Lcd.fillCircle(26, 37 + i*35,8,GREEN);
      }  
      else {
        M5.Lcd.fillCircle(26, 37 + i*35,8,RED); 
      }
   }
}

void showPumps() {
  char buffer[10];
   int xoff = 10; int yoff = 10;
   for (int i=0;i<4;i++) {
     if (pumpStatus[i] ==1 ) {
        sprintf(buffer, "%3d", PVml[i]);
        M5.Lcd.fillTriangle(44,37 + i*35-yoff,44+xoff,37 + i*35,44,37 + i*35+yoff,GREEN);
        M5.Lcd.fillRect(55,28 + i*35,50,25,BLACK);      // blanking rectangle
        M5.Lcd.setCursor(60,35 + i*35);
        M5.Lcd.print(buffer);                           // formatted
     }  
     else if (pumpStatus[i] ==2) {                      // keep showing the PV
        sprintf(buffer, "%3d", PVml[i]);
        M5.Lcd.fillTriangle(44,37 + i*35-yoff,44+xoff,37 + i*35,44,37 + i*35+yoff,RED);
        M5.Lcd.fillRect(55,28 + i*35,50,25,BLACK);
        M5.Lcd.setCursor(60,30 + i*35);
        M5.Lcd.print(buffer);
     }
     else {                                             // bag removed
        M5.Lcd.fillTriangle(44,37 + i*35-yoff,44+xoff,37 + i*35,44,37 + i*35+yoff,RED);
        M5.Lcd.fillRect(55,28 + i*35,50,25,BLACK);      
     }
   }
}

void showDisplay() {
   M5.Lcd.fillScreen(BLACK);
   M5.Lcd.setCursor(2, 5);
   M5.Lcd.print(" - FILLER -");
   for (int i=0;i<4;i++) {
       M5.Lcd.setCursor(2, 30 + i*35);
       M5.Lcd.print(i+1); 
       M5.Lcd.setCursor(120, 30 + i*35);
       M5.Lcd.setTextSize(1);  // Set the text size
       M5.Lcd.print("mL");   
       M5.Lcd.setTextSize(2);  // Set the text size
   }
   M5.Lcd.setCursor(10,180);
   M5.Lcd.print("FILL: ");M5.Lcd.print(SPcommon);
}



boolean getPV(int pumpIndex) {
  boolean done = false;
 // PVml[pumpIndex]=count[pumpIndex];
  if (SWState[pumpIndex] > 0) {                         // bag is on holder  
    if (PVml[pumpIndex] >= SPml[pumpIndex]) {             // reached the dispense volume
      relay.relayWrite(pumpIndex, 0);                    // pump Off
      pumpStatus[pumpIndex] = 2;                          // complete    
    }
    else {
      relay.relayWrite(pumpIndex, 1);                    // keep pump running
    }
  }
  else {
     relay.relayWrite(pumpIndex, 0);                    // pump Off
     pumpStatus[pumpIndex] = 3;                         // aborted     
  }
}



/*
void getPVcount() {
//  Serial.print("gPV");Serial.println(numberOfInterrupts);
  PVcount = numberOfInterrupts;
//  Serial.print("pv=");Serial.println(PVcount);
  PVliter = PVcount * ML_PER_COUNT/1000.0;
}
*/



    
    /*
    for (uint8_t i = 4; i < 8; i++) {
       pumpStatus[i-4]=extio.getDigitalInput(i)
       Serial.print(pumpStatus[i-4]) ;
       Serial.print(",");
    }
    Serial.println();
    */




/*
   if (M5.BtnA.wasPressed()) {  // If button A is pressed.  如果按键A按下
        M5.Lcd.fillRect(0, 50, 40, 20, BLACK);
        M5.Lcd.setCursor(0, 50);
        if ((count_i < 4)) {  // Control relays turn on/off in sequence.
                              // 控制继电器依次接通/断开
            M5.Lcd.printf("%d ON", count_i);
            relay.relayWrite(count_i, 1);
        } else if ((count_i >= 4)) {
            M5.Lcd.printf("%d OFF", (count_i - 4));
            relay.relayWrite((count_i - 4), 0);
        }
        count_i++;
        if (count_i >= 8) count_i = 0;
    }
    if (M5.BtnB.wasPressed()) {
        M5.Lcd.fillRect(0, 75, 80, 20, BLACK);
        M5.Lcd.setCursor(0, 75);
        if (flag_all) {  // Control all relays on/off.  控制所有继电器接通/断开
            M5.Lcd.printf("Realy All ON \n");
            relay.relayAll(1);
        } else {
            M5.Lcd.printf("Realy All OFF\n");
            relay.relayAll(0);
        }
        flag_all = !flag_all;
    }
    */
