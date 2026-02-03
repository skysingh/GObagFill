/* LED Blink, Teensyduino Tutorial #1
   http://www.pjrc.com/teensy/tutorial.html
 
   This example code is in the public domain.
*/


const int ledPin = 13;
const int SWpin[] = {9,10,11,12};
const int PMPpin[] = {14,15,16,17};
const int FMpin[] = {2,3,4,5};
const float count2ml = 10.0;

int SWState[4];                   // start switchs
int lastSWState[4];               // for debounce
int PVml[4];                      // ml dispensed
int count[4];
int pumpStatus[4] = {-1,-1,-1,-1};  // status 0= ready, 1= dispensing, 2= complete
boolean all_empty = false;
int SPml = 375;                   // setpont in mL


unsigned long onesecMillis = millis();
unsigned long loopMillis = millis();
const int fastmS = 100;

void setup() {
  Serial.begin(115200);
  
  // initialize the digital pin as an output.
  for (int i=0;i<4;i++) {
    pinMode(SWpin[i],INPUT_PULLUP);
    pinMode(PMPpin[i],OUTPUT);
    pinMode(FMpin[i],INPUT);
    digitalWrite(PMPpin[i],LOW);          // all relays off
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
/*
  while (true) {
    delay(1000);
  }
  */
}

void loop() {
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
            case 1:                                // pump is running          
              getPV(i);
              break;
            case 2 ... 3:                             // complete or abort  - waiting for reset   
              if (!SWState[i] == 0) {
                pumpStatus[i] = 0;                // reset the dispense
                PVml[i] = 0;
                count[i] = 0;
              } 
              break;     
          }   // end switch      
     }
   }  
   if ((millis() - onesecMillis) > 1000) {
    onesecMillis = millis();
    for (int i=0;i<4;i++) {                   // fake flow meter
      if (pumpStatus[i] ==1 ) {
        PVml[i] = PVml[i] + 10;
        Serial.print("pump: ");Serial.print(i);Serial.print(" ");Serial.print(PVml[i]);Serial.print(" / "); Serial.print(SPml);
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

boolean getPV(int pumpIndex) {
  boolean done = false;
 // PVml[pumpIndex]=count[pumpIndex];
  if (SWState[pumpIndex] == 0) {                         // bag is on holder  
    if (PVml[pumpIndex] >= SPml) {                        // reached the dispense volume
      digitalWrite(PMPpin[pumpIndex],LOW);              // pump Off
      pumpStatus[pumpIndex] = 2;                         // complete  
      done = true;  
    }
    else {
     digitalWrite(PMPpin[pumpIndex],HIGH);              // keep pump running
    }
  }
  else {
     digitalWrite(PMPpin[pumpIndex],LOW);              // pump Off
     pumpStatus[pumpIndex] = 3;                         // aborted     
  }
  return done;
}
