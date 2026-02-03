/*
 * M5StampPLC Batch Fill Controller
 * Version 0.16 - Fixed GPIO setup (reverted to v0.12 pattern)
 * 
 * IN1-4: Start buttons (hold ON during fill, OFF cancels)
 * OUT1-4: Valves/pumps
 * GPIO 1,2,4,5: Flowmeter pulse counters
 * 
 * Sequence: Input ON -> Output ON -> Count pulses -> 
 *           Reach setpoint -> Output OFF -> Wait for Input OFF then ON
 *           Input OFF early -> FAILED (shows ml at fail point) -> 
 *           Input ON restarts cycle automatically
 * 
 * Remote Control:
 *   START1-4: Latches channel ON until COMPLETE or STOP
 *   STOP1-4: Unlatches channel, causes FAILED if running
 * 
 * Settings stored in Preferences (non-volatile)
 * 
 * Changes from v0.15:
 *   - Fixed GPIO setup: removed delay(10) and digitalRead() between
 *     pinMode() and attachInterrupt() which was causing CH3 false counts
 *     and CH4 not counting (reverted to clean v0.12 pattern)
 */

#include <Arduino.h>
#include <M5StamPLC.h>
#include <Preferences.h>

// Program version
const char* VERSION = "0.16";

// Function prototypes
void drawStaticGUI();
void updateDisplay();
void drawChannelStatus(int ch);
void drawSettingsMenu();
void drawSetpointScreen();
void drawKFactorScreen();
void drawResetCyclesScreen();
void drawButtonLegends();
void handleButtons();
void sendStatusJSON();
void handleRPiCommands();
unsigned long getCounter(int index);
void resetCounter(int index);
float countsToMl(int ch, unsigned long counts);
void loadSettings();
void saveSettings();
void saveSetpoint();
void saveKFactor(int ch);
void saveCycleCounts();
void loadCycleCounts();
void resetAllCycleCounts();

// Preferences object
Preferences prefs;

// Display mode
enum DisplayMode {
  MAIN_SCREEN,
  SETTINGS_MENU,
  SETPOINT_ADJUST,
  KFACTOR_ADJUST,
  RESET_CYCLES
};

DisplayMode displayMode = MAIN_SCREEN;

// Menu selection
int menuSelection = 0;
const int MENU_ITEMS = 7; // Exit + Setpoint + 4 K-factors + Reset Cycles

// Setpoint in ml (same for all channels) - DEFAULT 375ml
float setpointMl = 375.0;
float tempSetpointMl = 375.0; // Temporary value while adjusting

// K-factor editing
int kFactorChannel = 0; // Which channel K-factor is being edited (0-3)
float tempKFactor = 5.0; // Temporary K-factor value while adjusting

// K-factors: pulses per ml (calibrate for each flowmeter) - DEFAULT 5.0
float kFactor[4] = {5.0, 5.0, 5.0, 5.0};

// Cycle counters - track total completed cycles per channel (NOW NON-VOLATILE)
unsigned long cycleCount[4] = {0, 0, 0, 0};
unsigned long lastCycleCount[4] = {0, 0, 0, 0}; // For display update tracking

// Channel states
enum ChannelState {
  IDLE,
  RUNNING,
  COMPLETE,
  FAILED
};

ChannelState channelState[4] = {IDLE, IDLE, IDLE, IDLE};

// Input states
bool input_list[8] = {false};
bool lastInput[4] = {false, false, false, false};

// Remote control flags for RPI-initiated cycles
bool remoteStartRequest[4] = {false, false, false, false};
bool remoteStopRequest[4] = {false, false, false, false};
bool remoteLatched[4] = {false, false, false, false};  // Latch state for remote control

// Counter values (volatile for ISR access)
volatile unsigned long counter[4] = {0, 0, 0, 0};

// Display update tracking
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 250;
bool forceDisplayUpdate = true;

// Previous values for flicker reduction
float lastMl[4] = {0, 0, 0, 0};
ChannelState lastState[4] = {IDLE, IDLE, IDLE, IDLE};
bool lastInputDisplay[4] = {false, false, false, false};

// Update interval
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 50;

unsigned long lastDebug = 0;
const unsigned long DEBUG_INTERVAL = 500;

// Button handling
unsigned long lastButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE = 200;

// Secondary serial port for Raspberry Pi communication
#define RPI_RX_PIN 40
#define RPI_TX_PIN 41
unsigned long lastStatusReport = 0;
const unsigned long STATUS_REPORT_INTERVAL = 2000; // Send status every 2 seconds

// GPIO pins for flowmeters - for reference/debug
const int FLOW_GPIO[4] = {1, 2, 4, 5};  // CH1=GPIO1, CH2=GPIO2, CH3=GPIO4, CH4=GPIO5

// Interrupt Service Routines
void IRAM_ATTR isr_counter1() {
  counter[0]++;
}

void IRAM_ATTR isr_counter2() {
  counter[1]++;
}

void IRAM_ATTR isr_counter3() {
  counter[2]++;
}

void IRAM_ATTR isr_counter4() {
  counter[3]++;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize Serial2 for Raspberry Pi communication (GPIO40=RX, GPIO41=TX)
  Serial2.begin(115200, SERIAL_8N1, RPI_RX_PIN, RPI_TX_PIN);
  delay(100);
  
  M5StamPLC.begin();
  
  // Explicitly initialize all channel states to IDLE (defensive)
  for (int i = 0; i < 4; i++) {
    channelState[i] = IDLE;
    remoteLatched[i] = false;
    remoteStartRequest[i] = false;
    remoteStopRequest[i] = false;
    lastInput[i] = false;
    lastMl[i] = 0;
    lastState[i] = IDLE;
    lastInputDisplay[i] = false;
  }
  
  // Load settings from preferences (includes cycle counts now)
  loadSettings();
  loadCycleCounts();
  
  Serial.println("M5StampPLC Batch Fill Controller");
  Serial.printf("Version: %s\n", VERSION);
  Serial.printf("Setpoint: %.0f ml\n", setpointMl);
  Serial.printf("K-Factors: %.2f, %.2f, %.2f, %.2f\n", 
                kFactor[0], kFactor[1], kFactor[2], kFactor[3]);
  Serial.printf("Cycle Counts: %lu, %lu, %lu, %lu (restored from NVM)\n",
                cycleCount[0], cycleCount[1], cycleCount[2], cycleCount[3]);
  
  // Setup counter pins with internal pull-up and attach interrupts immediately
  // NOTE: Do NOT add delays or digitalReads between pinMode and attachInterrupt!
  pinMode(GPIO_NUM_1, INPUT_PULLUP);
  pinMode(GPIO_NUM_2, INPUT_PULLUP);
  pinMode(GPIO_NUM_4, INPUT_PULLUP);
  pinMode(GPIO_NUM_5, INPUT_PULLUP);
  
  // Attach interrupts on FALLING edge immediately after pinMode
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_1), isr_counter1, FALLING);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_2), isr_counter2, FALLING);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_4), isr_counter3, FALLING);
  attachInterrupt(digitalPinToInterrupt(GPIO_NUM_5), isr_counter4, FALLING);
  
  Serial.println("Flowmeter GPIO configured: G1, G2, G4, G5 (FALLING edge)");
  
  // Initialize all outputs OFF
  for (int i = 0; i < 4; i++) {
    M5StamPLC.writePlcRelay(i, false);
  }
  
  Serial.println("Ready!");
  Serial.println("Commands: SP=xxxx (setpoint ml), K1=xx.xx K2=xx.xx K3=xx.xx K4=xx.xx (k-factors)");
  
  // Send startup message to Raspberry Pi
  Serial2.println("{\"status\":\"ready\",\"version\":\"" + String(VERSION) + "\",\"device\":\"M5StampPLC Batch Fill Controller\"}");
  
  // Draw initial GUI
  forceDisplayUpdate = true;
  drawStaticGUI();
  
  // Explicitly draw config button after GUI setup
  drawButtonLegends();
  
  Serial.println("Initial states: All channels IDLE");
}

void loop() {
  // Handle button presses
  handleButtons();
  
  // Handle Raspberry Pi commands
  handleRPiCommands();
  
  // Send status to Raspberry Pi periodically
  if (millis() - lastStatusReport >= STATUS_REPORT_INTERVAL) {
    lastStatusReport = millis();
    sendStatusJSON();
  }
  
  // Check for serial commands to change settings
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if (cmd.startsWith("SP=")) {
      float newSp = cmd.substring(3).toFloat();
      if (newSp > 0 && newSp < 100000) {
        setpointMl = newSp;
        saveSetpoint();
        Serial.printf("Setpoint changed to %.0f ml\n", setpointMl);
        forceDisplayUpdate = true;
        drawStaticGUI();
      }
    } else if (cmd.startsWith("K1=")) {
      float newK = cmd.substring(3).toFloat();
      if (newK > 0) {
        kFactor[0] = newK;
        saveKFactor(0);
        Serial.printf("K-Factor 1 changed to %.2f\n", kFactor[0]);
      }
    } else if (cmd.startsWith("K2=")) {
      float newK = cmd.substring(3).toFloat();
      if (newK > 0) {
        kFactor[1] = newK;
        saveKFactor(1);
        Serial.printf("K-Factor 2 changed to %.2f\n", kFactor[1]);
      }
    } else if (cmd.startsWith("K3=")) {
      float newK = cmd.substring(3).toFloat();
      if (newK > 0) {
        kFactor[2] = newK;
        saveKFactor(2);
        Serial.printf("K-Factor 3 changed to %.2f\n", kFactor[2]);
      }
    } else if (cmd.startsWith("K4=")) {
      float newK = cmd.substring(3).toFloat();
      if (newK > 0) {
        kFactor[3] = newK;
        saveKFactor(3);
        Serial.printf("K-Factor 4 changed to %.2f\n", kFactor[3]);
      }
    } else if (cmd == "STATUS") {
      Serial.printf("Setpoint: %.0f ml\n", setpointMl);
      Serial.printf("K-Factors: K1=%.2f K2=%.2f K3=%.2f K4=%.2f\n", 
                    kFactor[0], kFactor[1], kFactor[2], kFactor[3]);
      Serial.printf("Cycle Counts: C1=%lu C2=%lu C3=%lu C4=%lu\n",
                    cycleCount[0], cycleCount[1], cycleCount[2], cycleCount[3]);
      for (int i = 0; i < 4; i++) {
        const char* stateStr;
        switch (channelState[i]) {
          case IDLE: stateStr = "IDLE"; break;
          case RUNNING: stateStr = "RUNNING"; break;
          case COMPLETE: stateStr = "COMPLETE"; break;
          case FAILED: stateStr = "FAILED"; break;
          default: stateStr = "UNKNOWN"; break;
        }
        Serial.printf("CH%d: %s, Latched:%d\n", i + 1, stateStr, remoteLatched[i]);
      }
    } else if (cmd == "DEBUG") {
      // Debug command to check GPIO states and counters
      Serial.println("=== DEBUG INFO ===");
      Serial.printf("GPIO states: G1=%d G2=%d G4=%d G5=%d\n",
                    digitalRead(GPIO_NUM_1), digitalRead(GPIO_NUM_2),
                    digitalRead(GPIO_NUM_4), digitalRead(GPIO_NUM_5));
      Serial.printf("Raw counters: C1=%lu C2=%lu C3=%lu C4=%lu\n",
                    getCounter(0), getCounter(1), getCounter(2), getCounter(3));
      Serial.printf("Interrupt numbers: I1=%d I2=%d I4=%d I5=%d\n",
                    digitalPinToInterrupt(GPIO_NUM_1), digitalPinToInterrupt(GPIO_NUM_2),
                    digitalPinToInterrupt(GPIO_NUM_4), digitalPinToInterrupt(GPIO_NUM_5));
    } else if (cmd == "HELP") {
      Serial.println("Commands:");
      Serial.println("  SP=xxxx    - Set setpoint in ml");
      Serial.println("  K1=xx.xx   - Set K-factor channel 1");
      Serial.println("  K2=xx.xx   - Set K-factor channel 2");
      Serial.println("  K3=xx.xx   - Set K-factor channel 3");
      Serial.println("  K4=xx.xx   - Set K-factor channel 4");
      Serial.println("  STATUS     - Show current settings");
      Serial.println("  DEBUG      - Show GPIO and counter debug info");
      Serial.println("  HELP       - Show this help");
    }
  }
  
  // Only process channels when on main screen
  if (displayMode == MAIN_SCREEN) {
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
      lastUpdate = millis();
      
      // Read PLC inputs
      for (int i = 0; i < 8; i++) {
        input_list[i] = M5StamPLC.readPlcInput(i);
      }
      
      // Process each channel independently
      for (int ch = 0; ch < 4; ch++) {
        // Get physical input state
        bool physicalInput = input_list[ch];
        
        // Handle remote start request - LATCH ON
        if (remoteStartRequest[ch]) {
          remoteLatched[ch] = true;
          remoteStartRequest[ch] = false;
          Serial.printf("CH%d: Remote latched ON\n", ch + 1);
        }
        
        // Handle remote stop request - UNLATCH
        if (remoteStopRequest[ch]) {
          remoteLatched[ch] = false;
          remoteStopRequest[ch] = false;
          Serial.printf("CH%d: Remote latched OFF\n", ch + 1);
        }
        
        // Effective input: physical OR remote latch
        bool inputNow = physicalInput || remoteLatched[ch];
        
        unsigned long counts = getCounter(ch);
        float ml = countsToMl(ch, counts);
        
        switch (channelState[ch]) {
          
          case IDLE:
            if (inputNow && !lastInput[ch]) {
              resetCounter(ch);
              M5StamPLC.writePlcRelay(ch, true);
              channelState[ch] = RUNNING;
              forceDisplayUpdate = true;
              Serial.printf("CH%d: Started%s\n", ch + 1, remoteLatched[ch] ? " (remote)" : "");
            }
            break;
            
          case RUNNING:
            if (ml >= setpointMl) {
              M5StamPLC.writePlcRelay(ch, false);
              channelState[ch] = COMPLETE;
              cycleCount[ch]++;  // Increment cycle counter
              saveCycleCounts();  // Save to NVM
              forceDisplayUpdate = true;
              Serial.printf("CH%d: Complete (%.0f ml) - Total cycles: %lu (saved)\n", ch + 1, ml, cycleCount[ch]);
              // Auto-unlatch remote when cycle completes
              if (remoteLatched[ch]) {
                remoteLatched[ch] = false;
                Serial.printf("CH%d: Remote auto-unlatched (complete)\n", ch + 1);
              }
            } else if (!inputNow) {
              M5StamPLC.writePlcRelay(ch, false);
              channelState[ch] = FAILED;
              forceDisplayUpdate = true;
              Serial.printf("CH%d: FAILED at %.0f ml (counter frozen, input ON to restart)\n", ch + 1, ml);
            }
            break;
            
          case COMPLETE:
            // Wait for input to be released before returning to IDLE
            if (!inputNow) {
              channelState[ch] = IDLE;
              forceDisplayUpdate = true;
              Serial.printf("CH%d: Ready\n", ch + 1);
            }
            break;
            
          case FAILED:
            // Counter stays frozen showing fail point
            // Input ON directly restarts cycle (no clear button needed)
            if (inputNow && !lastInput[ch]) {
              resetCounter(ch);  // Zero the counter
              M5StamPLC.writePlcRelay(ch, true);
              channelState[ch] = RUNNING;
              forceDisplayUpdate = true;
              Serial.printf("CH%d: Restarted from FAILED (counter zeroed)\n", ch + 1);
            }
            break;
        }
        
        lastInput[ch] = inputNow;
      }
      
      // Update display at slower rate
      if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL || forceDisplayUpdate) {
        lastDisplayUpdate = millis();
        updateDisplay();
        forceDisplayUpdate = false;
      }
    }
    
    // Debug output - includes raw counts for CH4 debugging
    if (millis() - lastDebug >= DEBUG_INTERVAL) {
      lastDebug = millis();
      for (int i = 0; i < 4; i++) {
        const char* stateStr;
        switch (channelState[i]) {
          case IDLE: stateStr = "IDLE"; break;
          case RUNNING: stateStr = "RUN "; break;
          case COMPLETE: stateStr = "DONE"; break;
          case FAILED: stateStr = "FAIL"; break;
          default: stateStr = "????"; break;
        }
        unsigned long cnt = getCounter(i);
        Serial.printf("CH%d:%s %luml(%lu) ", i + 1, stateStr, (unsigned long)countsToMl(i, cnt), cnt);
      }
      Serial.println();
    }
  }
}

void loadSettings() {
  prefs.begin("batchfill", true);  // Read-only mode
  
  // Defaults: 375ml setpoint, 5.0 k-factor
  setpointMl = prefs.getFloat("setpoint", 375.0);
  kFactor[0] = prefs.getFloat("kfactor1", 5.0);
  kFactor[1] = prefs.getFloat("kfactor2", 5.0);
  kFactor[2] = prefs.getFloat("kfactor3", 5.0);
  kFactor[3] = prefs.getFloat("kfactor4", 5.0);
  
  prefs.end();
  
  Serial.println("Settings loaded from preferences");
}

void loadCycleCounts() {
  prefs.begin("batchfill", true);  // Read-only mode
  
  // Load cycle counts (default 0 if not found)
  cycleCount[0] = prefs.getULong("cycles1", 0);
  cycleCount[1] = prefs.getULong("cycles2", 0);
  cycleCount[2] = prefs.getULong("cycles3", 0);
  cycleCount[3] = prefs.getULong("cycles4", 0);
  
  // Initialize lastCycleCount for display tracking
  for (int i = 0; i < 4; i++) {
    lastCycleCount[i] = cycleCount[i];
  }
  
  prefs.end();
  
  Serial.println("Cycle counts loaded from NVM");
}

void saveSettings() {
  prefs.begin("batchfill", false);  // Read-write mode
  
  prefs.putFloat("setpoint", setpointMl);
  prefs.putFloat("kfactor1", kFactor[0]);
  prefs.putFloat("kfactor2", kFactor[1]);
  prefs.putFloat("kfactor3", kFactor[2]);
  prefs.putFloat("kfactor4", kFactor[3]);
  
  prefs.end();
  
  Serial.println("All settings saved");
}

void saveSetpoint() {
  prefs.begin("batchfill", false);
  prefs.putFloat("setpoint", setpointMl);
  prefs.end();
  Serial.println("Setpoint saved");
}

void saveKFactor(int ch) {
  prefs.begin("batchfill", false);
  char key[12];
  sprintf(key, "kfactor%d", ch + 1);
  prefs.putFloat(key, kFactor[ch]);
  prefs.end();
  Serial.printf("K-Factor %d saved\n", ch + 1);
}

void saveCycleCounts() {
  prefs.begin("batchfill", false);  // Read-write mode
  
  prefs.putULong("cycles1", cycleCount[0]);
  prefs.putULong("cycles2", cycleCount[1]);
  prefs.putULong("cycles3", cycleCount[2]);
  prefs.putULong("cycles4", cycleCount[3]);
  
  prefs.end();
  
  // Don't print every save to reduce serial spam
}

void resetAllCycleCounts() {
  for (int i = 0; i < 4; i++) {
    cycleCount[i] = 0;
    lastCycleCount[i] = 0;
  }
  saveCycleCounts();
  Serial.println("All cycle counts reset to zero and saved to NVM");
}

float countsToMl(int ch, unsigned long counts) {
  if (kFactor[ch] <= 0) return 0;  // Prevent divide by zero
  return (float)counts / kFactor[ch];
}

unsigned long getCounter(int index) {
  unsigned long value;
  noInterrupts();
  value = counter[index];
  interrupts();
  return value;
}

void resetCounter(int index) {
  if (index >= 0 && index < 4) {
    noInterrupts();
    counter[index] = 0;
    interrupts();
  }
}

void drawStaticGUI() {
  M5StamPLC.Display.fillScreen(TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  
  // Title (with 5px left margin)
  M5StamPLC.Display.setTextSize(1);
  M5StamPLC.Display.setCursor(10, 2);
  M5StamPLC.Display.printf("BATCH FILL  SP:%.0f ml", setpointMl);
  
  // Version number in top right
  M5StamPLC.Display.setTextColor(TFT_CYAN);
  M5StamPLC.Display.setCursor(M5StamPLC.Display.width() - 30, 2);
  M5StamPLC.Display.printf("v%s", VERSION);
  
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.drawLine(0, 14, M5StamPLC.Display.width(), 14, TFT_WHITE);
  
  // Header row (with 5px left margin)
  M5StamPLC.Display.setCursor(7, 18);
  M5StamPLC.Display.printf("CH IN OUT   ml  STATE CYCL");
  
  M5StamPLC.Display.drawLine(0, 30, M5StamPLC.Display.width(), 30, TFT_YELLOW);
  
  // Force redraw all channels
  for (int i = 0; i < 4; i++) {
    lastMl[i] = -1;
    lastState[i] = IDLE;
    lastInputDisplay[i] = false;
    lastCycleCount[i] = 0xFFFFFFFF; // Force initial display
  }
  
  // Draw channel status immediately
  for (int ch = 0; ch < 4; ch++) {
    drawChannelStatus(ch);
  }
  
  // Draw button legends
  drawButtonLegends();
}

void updateDisplay() {
  for (int ch = 0; ch < 4; ch++) {
    drawChannelStatus(ch);
  }
  drawButtonLegends();
}

void drawChannelStatus(int ch) {
  int yPos = 36 + (ch * 22);
  
  float currentMl = countsToMl(ch, getCounter(ch));
  // Show physical input OR latch state for the indicator
  bool physicalInput = input_list[ch];
  bool inputIndicator = physicalInput || remoteLatched[ch];
  bool outputOn = (channelState[ch] == RUNNING);
  
  // Channel number (with 5px left margin)
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.setCursor(7, yPos + 4);
  M5StamPLC.Display.printf("%d", ch + 1);
  
  // Input indicator (with 5px left margin)
  // Shows GREEN if physical input OR remote latched
  if (inputIndicator) {
    M5StamPLC.Display.fillCircle(28, yPos + 8, 5, TFT_GREEN);
  } else {
    M5StamPLC.Display.fillCircle(28, yPos + 8, 5, TFT_RED);
  }
  M5StamPLC.Display.drawCircle(28, yPos + 8, 5, TFT_WHITE);
  lastInputDisplay[ch] = inputIndicator;
  
  // Output indicator (with 5px left margin)
  if (outputOn) {
    M5StamPLC.Display.fillCircle(48, yPos + 8, 5, TFT_GREEN);
  } else {
    M5StamPLC.Display.fillCircle(48, yPos + 8, 5, TFT_RED);
  }
  M5StamPLC.Display.drawCircle(48, yPos + 8, 5, TFT_WHITE);
  
  // ml value - NO DECIMALS
  M5StamPLC.Display.fillRect(60, yPos, 40, 16, TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_CYAN);
  M5StamPLC.Display.setCursor(60, yPos + 4);
  M5StamPLC.Display.printf("%5.0f", currentMl);  // Changed to .0f for no decimals
  lastMl[ch] = currentMl;
  
  // State (adjusted position)
  M5StamPLC.Display.fillRect(102, yPos, 35, 16, TFT_BLACK);
  M5StamPLC.Display.setCursor(102, yPos + 4);
  switch (channelState[ch]) {
    case IDLE:
      M5StamPLC.Display.setTextColor(TFT_WHITE);
      M5StamPLC.Display.printf("IDLE");
      break;
    case RUNNING:
      M5StamPLC.Display.setTextColor(TFT_GREEN);
      M5StamPLC.Display.printf("RUN");
      break;
    case COMPLETE:
      M5StamPLC.Display.setTextColor(TFT_YELLOW);
      M5StamPLC.Display.printf("DONE");
      break;
    case FAILED:
      M5StamPLC.Display.setTextColor(TFT_RED);
      M5StamPLC.Display.printf("FAIL");
      break;
  }
  lastState[ch] = channelState[ch];
  
  // Cycle count
  M5StamPLC.Display.fillRect(138, yPos, 22, 16, TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_MAGENTA);
  M5StamPLC.Display.setCursor(138, yPos + 4);
  if (cycleCount[ch] < 1000) {
    M5StamPLC.Display.printf("%3lu", cycleCount[ch]);
  } else {
    M5StamPLC.Display.printf("%3luk", cycleCount[ch] / 1000);
  }
  lastCycleCount[ch] = cycleCount[ch];
}

void handleButtons() {
  M5StamPLC.update();
  
  // Debounce button presses
  if (millis() - lastButtonPress < BUTTON_DEBOUNCE) {
    return;
  }
  
  if (displayMode == MAIN_SCREEN) {
    // Button C enters settings menu - only if all channels are IDLE or FAILED
    if (M5StamPLC.BtnC.wasPressed()) {
      // Check if all channels are IDLE or FAILED
      bool canConfig = true;
      for (int i = 0; i < 4; i++) {
        if (channelState[i] != IDLE && channelState[i] != FAILED) {
          canConfig = false;
          break;
        }
      }
      
      if (canConfig) {
        displayMode = SETTINGS_MENU;
        menuSelection = 0;
        drawSettingsMenu();
        lastButtonPress = millis();
        Serial.println("Entered settings menu");
      } else {
        Serial.println("Settings blocked - channels running or complete");
        M5StamPLC.Display.fillRect(0, 110, M5StamPLC.Display.width(), 18, TFT_RED);
        M5StamPLC.Display.setTextColor(TFT_WHITE);
        M5StamPLC.Display.setCursor(10, 112);
        M5StamPLC.Display.printf("Wait for IDLE/FAIL");
        delay(1000);
        forceDisplayUpdate = true;
        drawStaticGUI();
      }
    }
    
  } else if (displayMode == SETTINGS_MENU) {
    // Button A - navigate up
    if (M5StamPLC.BtnA.wasPressed()) {
      menuSelection--;
      if (menuSelection < 0) menuSelection = MENU_ITEMS - 1;
      drawSettingsMenu();
      lastButtonPress = millis();
      Serial.printf("Menu: %d\n", menuSelection);
    }
    
    // Button B - navigate down
    if (M5StamPLC.BtnB.wasPressed()) {
      menuSelection++;
      if (menuSelection >= MENU_ITEMS) menuSelection = 0;
      drawSettingsMenu();
      lastButtonPress = millis();
      Serial.printf("Menu: %d\n", menuSelection);
    }
    
    // Button C - select menu item
    if (M5StamPLC.BtnC.wasPressed()) {
      if (menuSelection == 0) {
        // EXIT - return to main screen
        displayMode = MAIN_SCREEN;
        forceDisplayUpdate = true;
        drawStaticGUI();
        Serial.println("Exited settings menu");
      } else if (menuSelection == 1) {
        // Setpoint adjustment
        displayMode = SETPOINT_ADJUST;
        tempSetpointMl = setpointMl;
        drawSetpointScreen();
        Serial.println("Adjusting setpoint");
      } else if (menuSelection >= 2 && menuSelection <= 5) {
        // K-factor adjustment (channels 1-4)
        displayMode = KFACTOR_ADJUST;
        kFactorChannel = menuSelection - 2;
        tempKFactor = kFactor[kFactorChannel];
        drawKFactorScreen();
        Serial.printf("Adjusting K-factor CH%d\n", kFactorChannel + 1);
      } else if (menuSelection == 6) {
        // Reset cycle counts
        displayMode = RESET_CYCLES;
        drawResetCyclesScreen();
        Serial.println("Reset cycles screen");
      }
      lastButtonPress = millis();
    }
    
  } else if (displayMode == SETPOINT_ADJUST) {
    // Button A increments setpoint
    if (M5StamPLC.BtnA.wasPressed()) {
      tempSetpointMl += 5.0;
      if (tempSetpointMl > 9999.0) {
        tempSetpointMl = 9999.0;
      }
      drawSetpointScreen();
      lastButtonPress = millis();
      Serial.printf("Setpoint: %.0f ml\n", tempSetpointMl);
    }
    
    // Button B decrements setpoint
    if (M5StamPLC.BtnB.wasPressed()) {
      tempSetpointMl -= 5.0;
      if (tempSetpointMl < 5.0) {
        tempSetpointMl = 5.0;
      }
      drawSetpointScreen();
      lastButtonPress = millis();
      Serial.printf("Setpoint: %.0f ml\n", tempSetpointMl);
    }
    
    // Button C saves and returns to menu
    if (M5StamPLC.BtnC.wasPressed()) {
      setpointMl = tempSetpointMl;
      saveSetpoint();
      displayMode = SETTINGS_MENU;
      drawSettingsMenu();
      lastButtonPress = millis();
      Serial.printf("Setpoint saved: %.0f ml\n", setpointMl);
    }
    
  } else if (displayMode == KFACTOR_ADJUST) {
    // Button A increments K-factor
    if (M5StamPLC.BtnA.wasPressed()) {
      tempKFactor += 0.01;
      if (tempKFactor > 100.0) {
        tempKFactor = 100.0;
      }
      drawKFactorScreen();
      lastButtonPress = millis();
      Serial.printf("K-factor: %.2f\n", tempKFactor);
    }
    
    // Button B decrements K-factor
    if (M5StamPLC.BtnB.wasPressed()) {
      tempKFactor -= 0.01;
      if (tempKFactor < 0.01) {
        tempKFactor = 0.01;
      }
      drawKFactorScreen();
      lastButtonPress = millis();
      Serial.printf("K-factor: %.2f\n", tempKFactor);
    }
    
    // Button C saves and returns to menu
    if (M5StamPLC.BtnC.wasPressed()) {
      kFactor[kFactorChannel] = tempKFactor;
      saveKFactor(kFactorChannel);
      displayMode = SETTINGS_MENU;
      drawSettingsMenu();
      lastButtonPress = millis();
      Serial.printf("K-factor CH%d saved: %.2f\n", kFactorChannel + 1, tempKFactor);
    }
    
  } else if (displayMode == RESET_CYCLES) {
    // Button A - Confirm reset (YES)
    if (M5StamPLC.BtnA.wasPressed()) {
      resetAllCycleCounts();
      displayMode = SETTINGS_MENU;
      forceDisplayUpdate = true;
      drawSettingsMenu();
      lastButtonPress = millis();
      Serial.println("Cycle counts reset");
    }
    
    // Button B - Cancel (NO)
    if (M5StamPLC.BtnB.wasPressed()) {
      displayMode = SETTINGS_MENU;
      drawSettingsMenu();
      lastButtonPress = millis();
      Serial.println("Reset cancelled");
    }
    
    // Button C - Cancel
    if (M5StamPLC.BtnC.wasPressed()) {
      displayMode = SETTINGS_MENU;
      drawSettingsMenu();
      lastButtonPress = millis();
      Serial.println("Reset cancelled");
    }
  }
}

void drawButtonLegends() {
  // Clear bottom area for legends
  M5StamPLC.Display.fillRect(0, 118, M5StamPLC.Display.width(), 10, TFT_BLACK);
  
  // Check if all channels are IDLE or FAILED (safe to enter config)
  bool canConfig = true;
  for (int i = 0; i < 4; i++) {
    if (channelState[i] != IDLE && channelState[i] != FAILED) {
      canConfig = false;
      break;
    }
  }
  
  // Show "config" above BtnC (right side) if all channels are IDLE or FAILED
  if (canConfig) {
    M5StamPLC.Display.setTextSize(1);
    M5StamPLC.Display.setTextColor(TFT_CYAN);
    M5StamPLC.Display.setCursor(M5StamPLC.Display.width() - 38, 120);
    M5StamPLC.Display.printf("config");
  }
}

void drawSettingsMenu() {
  M5StamPLC.Display.fillScreen(TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.setTextSize(1);
  
  // Title
  M5StamPLC.Display.setCursor(30, 5);
  M5StamPLC.Display.printf("SETTINGS MENU");
  
  M5StamPLC.Display.drawLine(0, 18, M5StamPLC.Display.width(), 18, TFT_CYAN);
  
  // Menu items - EXIT is now first option
  const char* menuItems[7] = {
    "<< EXIT",
    "Setpoint (all CH)",
    "K-Factor CH1",
    "K-Factor CH2",
    "K-Factor CH3",
    "K-Factor CH4",
    "Reset Cycle Counts"
  };
  
  for (int i = 0; i < MENU_ITEMS; i++) {
    int yPos = 22 + (i * 12);
    
    // Highlight selected item
    if (i == menuSelection) {
      M5StamPLC.Display.fillRect(0, yPos - 1, M5StamPLC.Display.width(), 11, TFT_BLUE);
      M5StamPLC.Display.setTextColor(TFT_YELLOW);
    } else {
      M5StamPLC.Display.setTextColor(TFT_WHITE);
    }
    
    M5StamPLC.Display.setCursor(5, yPos);
    M5StamPLC.Display.printf(menuItems[i]);
    
    // Show current value
    if (i == 1) {
      // Setpoint
      M5StamPLC.Display.setCursor(115, yPos);
      M5StamPLC.Display.printf("%.0f", setpointMl);
    } else if (i >= 2 && i <= 5) {
      // K-factors
      M5StamPLC.Display.setCursor(108, yPos);
      M5StamPLC.Display.printf("%.2f", kFactor[i - 2]);
    }
  }
  
  // Instructions
  M5StamPLC.Display.setTextColor(TFT_GREEN);
  M5StamPLC.Display.setCursor(5, 110);
  M5StamPLC.Display.printf("A:UP B:DN C:SELECT");
}

void drawSetpointScreen() {
  M5StamPLC.Display.fillScreen(TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.setTextSize(1);
  
  // Title
  M5StamPLC.Display.setCursor(20, 5);
  M5StamPLC.Display.printf("SETPOINT ADJUST");
  
  M5StamPLC.Display.drawLine(0, 18, M5StamPLC.Display.width(), 18, TFT_CYAN);
  
  // Current setpoint value (large)
  M5StamPLC.Display.setTextSize(2);
  M5StamPLC.Display.setTextColor(TFT_YELLOW);
  M5StamPLC.Display.setCursor(30, 40);
  M5StamPLC.Display.printf("%.0f", tempSetpointMl);
  
  M5StamPLC.Display.setTextSize(1);
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.setCursor(100, 50);
  M5StamPLC.Display.printf("ml");
  
  // Range indicator
  M5StamPLC.Display.setTextColor(TFT_CYAN);
  M5StamPLC.Display.setCursor(20, 70);
  M5StamPLC.Display.printf("Range: 5-9999 ml");
  M5StamPLC.Display.setCursor(32, 82);
  M5StamPLC.Display.printf("Step: 5 ml");
  
  // Instructions
  M5StamPLC.Display.setTextColor(TFT_GREEN);
  M5StamPLC.Display.setCursor(5, 108);
  M5StamPLC.Display.printf("A:+5  B:-5  C:SAVE");
}

void drawKFactorScreen() {
  M5StamPLC.Display.fillScreen(TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.setTextSize(1);
  
  // Title
  M5StamPLC.Display.setCursor(15, 5);
  M5StamPLC.Display.printf("K-FACTOR CH%d ADJUST", kFactorChannel + 1);
  
  M5StamPLC.Display.drawLine(0, 18, M5StamPLC.Display.width(), 18, TFT_CYAN);
  
  // Current K-factor value (large)
  M5StamPLC.Display.setTextSize(2);
  M5StamPLC.Display.setTextColor(TFT_YELLOW);
  M5StamPLC.Display.setCursor(25, 40);
  M5StamPLC.Display.printf("%.2f", tempKFactor);
  
  M5StamPLC.Display.setTextSize(1);
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.setCursor(95, 50);
  M5StamPLC.Display.printf("p/ml");
  
  // Range indicator
  M5StamPLC.Display.setTextColor(TFT_CYAN);
  M5StamPLC.Display.setCursor(15, 70);
  M5StamPLC.Display.printf("Range: 0.01-100.0");
  M5StamPLC.Display.setCursor(28, 82);
  M5StamPLC.Display.printf("Step: 0.01");
  
  // Instructions
  M5StamPLC.Display.setTextColor(TFT_GREEN);
  M5StamPLC.Display.setCursor(3, 108);
  M5StamPLC.Display.printf("A:+.01 B:-.01 C:SAVE");
}

void drawResetCyclesScreen() {
  M5StamPLC.Display.fillScreen(TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  M5StamPLC.Display.setTextSize(1);
  
  // Title
  M5StamPLC.Display.setCursor(20, 5);
  M5StamPLC.Display.printf("RESET CYCLE COUNTS");
  
  M5StamPLC.Display.drawLine(0, 18, M5StamPLC.Display.width(), 18, TFT_RED);
  
  // Current counts
  M5StamPLC.Display.setTextColor(TFT_CYAN);
  M5StamPLC.Display.setCursor(10, 30);
  M5StamPLC.Display.printf("Current Counts:");
  
  M5StamPLC.Display.setTextColor(TFT_WHITE);
  for (int i = 0; i < 4; i++) {
    M5StamPLC.Display.setCursor(15, 45 + (i * 12));
    M5StamPLC.Display.printf("CH%d: %lu cycles", i + 1, cycleCount[i]);
  }
  
  // Warning
  M5StamPLC.Display.setTextColor(TFT_RED);
  M5StamPLC.Display.setCursor(10, 95);
  M5StamPLC.Display.printf("Reset ALL to zero?");
  
  // Instructions
  M5StamPLC.Display.setTextColor(TFT_GREEN);
  M5StamPLC.Display.setCursor(5, 108);
  M5StamPLC.Display.printf("A:YES B:NO C:CANCEL");
}

void sendStatusJSON() {
  // Create JSON status report
  String jsonStr = "{";
  jsonStr += "\"timestamp\":";
  jsonStr += String(millis());
  jsonStr += ",\"setpoint\":";
  jsonStr += String(setpointMl, 0);
  jsonStr += ",\"channels\":[";
  
  for (int i = 0; i < 4; i++) {
    if (i > 0) jsonStr += ",";
    
    jsonStr += "{";
    jsonStr += "\"ch\":";
    jsonStr += String(i + 1);
    jsonStr += ",\"state\":\"";
    
    switch (channelState[i]) {
      case IDLE: jsonStr += "IDLE"; break;
      case RUNNING: jsonStr += "RUNNING"; break;
      case COMPLETE: jsonStr += "COMPLETE"; break;
      case FAILED: jsonStr += "FAILED"; break;
    }
    
    jsonStr += "\",\"input\":";
    jsonStr += input_list[i] ? "true" : "false";  // Physical input only
    jsonStr += ",\"output\":";
    jsonStr += (channelState[i] == RUNNING) ? "true" : "false";
    jsonStr += ",\"counts\":";
    jsonStr += String(getCounter(i));
    jsonStr += ",\"ml\":";
    jsonStr += String((unsigned long)countsToMl(i, getCounter(i)));
    jsonStr += ",\"kfactor\":";
    jsonStr += String(kFactor[i], 2);
    jsonStr += ",\"cycles\":";
    jsonStr += String(cycleCount[i]);
    jsonStr += ",\"latched\":";
    jsonStr += remoteLatched[i] ? "true" : "false";  // Separate latch state
    jsonStr += "}";
  }
  
  jsonStr += "]}";
  
  // Send to RPI
  Serial2.println(jsonStr);
  
  // Echo to terminal for debugging
  Serial.print("RPI TX: ");
  Serial.println(jsonStr);
}

void handleRPiCommands() {
  if (Serial2.available()) {
    String cmd = Serial2.readStringUntil('\n');
    cmd.trim();
    
    // Echo received command to terminal
    Serial.print("RPI RX: ");
    Serial.println(cmd);
    
    cmd.toUpperCase();
    String response = "";
    
    if (cmd.startsWith("SP=")) {
      float newSp = cmd.substring(3).toFloat();
      if (newSp >= 5.0 && newSp <= 9999.0) {
        setpointMl = newSp;
        saveSetpoint();
        response = "{\"response\":\"OK\",\"setpoint\":" + String(setpointMl, 0) + "}";
        if (displayMode == MAIN_SCREEN) {
          forceDisplayUpdate = true;
          drawStaticGUI();
        }
      } else {
        response = "{\"response\":\"ERROR\",\"msg\":\"Setpoint out of range (5-9999)\"}";
      }
    } 
    else if (cmd.startsWith("K") && cmd.charAt(2) == '=') {
      int ch = cmd.charAt(1) - '1';
      float newK = cmd.substring(3).toFloat();
      if (ch >= 0 && ch < 4 && newK >= 0.01 && newK <= 100.0) {
        kFactor[ch] = newK;
        saveKFactor(ch);
        response = "{\"response\":\"OK\",\"channel\":" + String(ch + 1) + ",\"kfactor\":" + String(kFactor[ch], 2) + "}";
      } else {
        response = "{\"response\":\"ERROR\",\"msg\":\"Invalid K-factor (0.01-100.0)\"}";
      }
    } 
    else if (cmd.startsWith("START")) {
      int ch = cmd.charAt(5) - '1';
      if (ch >= 0 && ch < 4) {
        if (channelState[ch] == IDLE || channelState[ch] == FAILED) {
          remoteStartRequest[ch] = true;
          response = "{\"response\":\"OK\",\"action\":\"start\",\"channel\":" + String(ch + 1) + ",\"latched\":true}";
          Serial.printf("Remote start request CH%d (will latch)\n", ch + 1);
        } else if (channelState[ch] == RUNNING) {
          response = "{\"response\":\"OK\",\"action\":\"start\",\"channel\":" + String(ch + 1) + ",\"msg\":\"Already running\"}";
        } else if (channelState[ch] == COMPLETE) {
          response = "{\"response\":\"WAIT\",\"action\":\"start\",\"channel\":" + String(ch + 1) + ",\"msg\":\"Wait for IDLE (cycle complete)\"}";
        }
      } else {
        response = "{\"response\":\"ERROR\",\"msg\":\"Invalid channel (1-4)\"}";
      }
    }
    else if (cmd.startsWith("STOP")) {
      int ch = cmd.charAt(4) - '1';
      if (ch >= 0 && ch < 4) {
        if (remoteLatched[ch] || channelState[ch] == RUNNING) {
          remoteStopRequest[ch] = true;
          response = "{\"response\":\"OK\",\"action\":\"stop\",\"channel\":" + String(ch + 1) + ",\"latched\":false}";
          Serial.printf("Remote stop request CH%d (will unlatch)\n", ch + 1);
        } else if (channelState[ch] == COMPLETE) {
          // Force transition to IDLE by simulating input release
          remoteStopRequest[ch] = true;
          response = "{\"response\":\"OK\",\"action\":\"stop\",\"channel\":" + String(ch + 1) + ",\"msg\":\"Releasing from COMPLETE\"}";
          Serial.printf("Remote stop request CH%d (releasing from COMPLETE)\n", ch + 1);
        } else {
          response = "{\"response\":\"OK\",\"action\":\"stop\",\"channel\":" + String(ch + 1) + ",\"msg\":\"Already stopped\"}";
        }
      } else {
        response = "{\"response\":\"ERROR\",\"msg\":\"Invalid channel (1-4)\"}";
      }
    }
    else if (cmd == "STATUS") {
      sendStatusJSON();
      return; // Status already echoed in sendStatusJSON
    }
    else if (cmd == "DEBUG") {
      // Debug command via RPI
      response = "{\"gpio\":{\"g1\":" + String(digitalRead(GPIO_NUM_1)) + 
                 ",\"g2\":" + String(digitalRead(GPIO_NUM_2)) +
                 ",\"g4\":" + String(digitalRead(GPIO_NUM_4)) +
                 ",\"g5\":" + String(digitalRead(GPIO_NUM_5)) + "}";
      response += ",\"counters\":{\"c1\":" + String(getCounter(0)) +
                  ",\"c2\":" + String(getCounter(1)) +
                  ",\"c3\":" + String(getCounter(2)) +
                  ",\"c4\":" + String(getCounter(3)) + "}}";
    }
    else if (cmd == "RESETCYCLES") {
      resetAllCycleCounts();
      forceDisplayUpdate = true;
      response = "{\"response\":\"OK\",\"action\":\"reset_cycles\",\"cycles\":[" + 
                 String(cycleCount[0]) + "," + String(cycleCount[1]) + "," + 
                 String(cycleCount[2]) + "," + String(cycleCount[3]) + "]}";
    }
    else if (cmd == "HELP") {
      response = "{\"commands\":[\"SP=value\",\"K1=value\",\"K2=value\",\"K3=value\",\"K4=value\",\"START1\",\"START2\",\"START3\",\"START4\",\"STOP1\",\"STOP2\",\"STOP3\",\"STOP4\",\"RESETCYCLES\",\"STATUS\",\"DEBUG\",\"HELP\"]}";
    } 
    else {
      response = "{\"response\":\"ERROR\",\"msg\":\"Unknown command\"}";
    }
    
    // Send response and echo to terminal
    Serial2.println(response);
    Serial.print("RPI TX: ");
    Serial.println(response);
  }
}