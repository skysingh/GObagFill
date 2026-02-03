/* 
 *  M5StamPLC
 *  ver 1 1/10/26
 *  
*/
#include "Arduino.h"
#include <M5StamPLC.h>
#include <driver/pcnt.h>


const float ver=0.1;
float count2ml[] = {0.19,0.19,0.19,0.19};   // calibration constant
int SWState[4];                             // start switchs
float PVml[4];                              // ml dispensed
int16_t count[4];                               // all zero based arrays
int pumpStatus[4] = {-1,-1,-1,-1};          // status 0= ready, 1= dispensing, 2= complete
boolean all_empty = false;
int SPml = 375;                             // setpoint in mL

unsigned long onesecMillis = millis();
unsigned long loopMillis = millis();
const int fastmS = 100;

// Define GPIOs for the four inputs
#define PCNT_INPUT_1_IO 11
#define PCNT_INPUT_2_IO 7
#define PCNT_INPUT_3_IO 14
#define PCNT_INPUT_4_IO 13

// Define PCNT Units to use
#define PCNT_UNIT_1 PCNT_UNIT_0
#define PCNT_UNIT_2 PCNT_UNIT_1
#define PCNT_UNIT_3 PCNT_UNIT_2
#define PCNT_UNIT_4 PCNT_UNIT_3

// Function to initialize a single PCNT unit
void init_pcnt(pcnt_unit_t unit, int gpio_num) {
   pcnt_config_t pcnt_config = {
        .pulse_gpio_num = gpio_num,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_DIS,
        .counter_h_lim = 0,
        .counter_l_lim = 0,
        .unit = unit,       // Now in correct order
        .channel = PCNT_CHANNEL_0  // Now in correct order
    };
    
    // Configure PCNT unit
    pcnt_unit_config(&pcnt_config);

    // Enable the glitch filter to remove noise from signals
    pcnt_set_filter_value(unit, 100); // Filter out pulses shorter than 100ns
    pcnt_filter_enable(unit);

    // Clear the counter to zero
    pcnt_counter_clear(unit);

    // Resume the counter
    pcnt_counter_resume(unit);
}

void setup() {
    Serial.begin(115200);
    delay(500); 
    Serial.println("Initializing PCNT counters...");
    // Initialize all four PCNT units for their respective pins
    init_pcnt(PCNT_UNIT_1, PCNT_INPUT_1_IO);
    init_pcnt(PCNT_UNIT_2, PCNT_INPUT_2_IO);
    init_pcnt(PCNT_UNIT_3, PCNT_INPUT_3_IO);
    init_pcnt(PCNT_UNIT_4, PCNT_INPUT_4_IO);    
    Serial.println("PCNT initialization complete.");
    M5StamPLC.begin();
    delay(200);
    for (int i = 0; i < 4; i++) {                     // read switches
        SWState[i] = M5StamPLC.readPlcInput(i);
        PVml[i] = 0;  
    }
    for (int i = 0; i < 4; i++) {                     // set all relays to OFF
        M5StamPLC.writePlcRelay(i, false);
        pumpStatus[i]=0;                      // set all to ready  
    }
    Serial.println("  --  READY --");
}

void loop() {
    // Read the current counter values
    count[0] = 0;
    count[1] = 0;
    count[2] = 0;
    count[3] = 0;
    pcnt_get_counter_value(PCNT_UNIT_1, &count[0]);
    pcnt_get_counter_value(PCNT_UNIT_2, &count[1]);
    pcnt_get_counter_value(PCNT_UNIT_3, &count[2]);
    pcnt_get_counter_value(PCNT_UNIT_4, &count[3]);
    for (uint8_t i = 0; i < 4; i++) { 
      if (pumpStatus[i] == 1) checkPump(i);       // check sp reached only is pump running  
    }
    
    if (millis() - loopMillis > fastmS) {
      loopMillis = millis();
      for (int i = 0; i < 4; i++) {                     // read switches
        SWState[i] = M5StamPLC.readPlcInput(i);
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
                switch (i) {
                  case 0:
                    pcnt_counter_clear(PCNT_UNIT_1);
                    break;
                  case 1:
                    pcnt_counter_clear(PCNT_UNIT_2);
                    break;
                  case 2:
                    pcnt_counter_clear(PCNT_UNIT_3);
                    break;
                  case 3:
                    pcnt_counter_clear(PCNT_UNIT_4);
                    break;
                }               // end clear switch
              } 
              break;     
          }   // end switch      
      }
  }
  if ((millis() - onesecMillis) > 1000) {         // reporting only
    onesecMillis = millis();
    for (int i=0;i<4;i++) {                   // 
      if (pumpStatus[i] ==1 ) {
         Serial.print("P: ");Serial.print(i);Serial.print(" ");Serial.print(PVml[i]);Serial.print(" / "); Serial.println(count[i]);
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
       Serial.print(count[i]);Serial.print(",");
    }
    Serial.println();
  }
}  


    // Optional: clear counters periodically if you need to measure frequency/rate
    // pcnt_counter_clear(PCNT_UNIT_1);
    // pcnt_counter_clear(PCNT_UNIT_2);
    // pcnt_counter_clear(PCNT_UNIT_3);
    // pcnt_counter_clear(PCNT_UNIT_4);





/*



#include <FreqMeasureMulti.h>

const float ver=0.4;

// Measure 4 frequencies at the same time! :-)
FreqMeasureMulti freq0;
FreqMeasureMulti freq1;
FreqMeasureMulti freq2;
FreqMeasureMulti freq3;

const int ledPin = 13;
const int SWpin[] = {7,8,11,12};
const int PMPpin[] = {3,4,6,9};        // support by PWM if needed
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




void setup() {
  Serial.begin(115200);
  delay(500);
  freq0.begin(FMpin[0]);
  freq1.begin(FMpin[1]);
  freq2.begin(FMpin[2]);
  freq3.begin(FMpin[3]);
  // initialize the digital pin as an output.
  for (int i=0;i<4;i++) {
    pinMode(SWpin[i],INPUT_PULLUP);
    pinMode(PMPpin[i],OUTPUT);
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
*/

// =======================   SUBROUTINES ========================

boolean checkPump(int pumpIndex) {
  boolean done = false;
  if (SWState[pumpIndex] == 0) {                        // bag is on holder  
    if (int(PVml[pumpIndex]) >= SPml) {                 // reached the dispense volume
      M5StamPLC.writePlcRelay(pumpIndex, false);
      pumpStatus[pumpIndex] = 2;                         // complete  
      done = true;  
    }
    else {
     M5StamPLC.writePlcRelay(pumpIndex, true);
    }
  }
  else {
     M5StamPLC.writePlcRelay(pumpIndex, false);
     pumpStatus[pumpIndex] = 3;                         // aborted     
  }
  return done;
}
