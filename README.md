# GOBagFill - Reverse FSD Batch Fill Controller

A 4-channel batch fill controller for liquid dispensing applications. Implements a "reverse FSD" (Form-Fill-Seal) workflow where the bag/container is placed first, then filled to a precise volume using flowmeter feedback.

**Repository:** github.com/skysingh/GObagFill
**Version:** M5StampPLC v0.16 / Teensy LC v2.1
**Date:** January 2026

---

## Table of Contents

- [Overview](#overview)
- [Hardware Variants](#hardware-variants)
- [System Architecture](#system-architecture)
- [State Machine](#state-machine)
- [Pin Mapping](#pin-mapping)
- [Wiring Schematic](#wiring-schematic)
- [Communication Protocol](#communication-protocol)
- [Configuration](#configuration)
- [Calibration](#calibration)
- [Serial Commands](#serial-commands)
- [JSON Status Format](#json-status-format)
- [Operation Modes](#operation-modes)
- [Safety Features](#safety-features)
- [Bill of Materials](#bill-of-materials)
- [Troubleshooting](#troubleshooting)

---

## Overview

### What is a "Reverse FSD"?

Traditional Form-Fill-Seal (FSD) machines form a bag, fill it, then seal it in a continuous process. This **Reverse FSD** inverts the workflow:

1. Operator places a pre-formed bag on the fill station
2. Operator holds deadman switch (or HMI sends START command)
3. Valve opens, flowmeter counts pulses
4. When setpoint volume is reached, valve closes
5. Operator removes filled bag

### Key Features

- **4 independent fill channels** - each with dedicated flowmeter, valve, and input
- **Deadman switch operation** - releasing input mid-fill triggers FAILED state
- **Remote control via Raspberry Pi** - JSON protocol over serial UART
- **Non-volatile settings** - setpoint and K-factors survive power cycles (M5StampPLC)
- **Cycle counting** - tracks total fills per channel for production metrics
- **On-device configuration** - LCD menu system (M5StampPLC variant)

### Application Examples

- Liquid soap/detergent filling
- Beverage dispensing
- Chemical portioning
- Agricultural liquid measurement
- Any batch liquid transfer requiring volumetric accuracy

---

## Hardware Variants

### M5StampPLC (ESP32-based)

| Feature | Specification |
|---------|---------------|
| MCU | ESP32-PICO-D4 |
| Display | 160x128 TFT LCD |
| Inputs | 8x opto-isolated (IN1-IN8) |
| Outputs | 4x relay (OUT1-OUT4) |
| GPIO | G1, G2, G4, G5 for flowmeters |
| Storage | NVS (Preferences library) |
| Serial | USB + Serial2 (GPIO40/41) |
| Buttons | 3x (A, B, C) |

**Firmware:** `M5stamPLCbagfill016_works012226/M5stamPLCbagfill016_works012226.ino`

### Teensy LC (ARM Cortex-M0+)

| Feature | Specification |
|---------|---------------|
| MCU | MKL26Z64VFT4 (48MHz) |
| Display | None (RPI HMI only) |
| Inputs | 4x digital (pins 7,8,11,12) |
| Outputs | M5Stack 4-Relay I2C (0x26) |
| GPIO | Pins 23,22,20,17 (FreqMeasureMulti) |
| Storage | Volatile (no NVS) |
| Serial | USB + Serial1 (pins 0/1) |

**Firmware:** `teensyBagFill021/teensyBagFill021.ino`

### Comparison

| Capability | M5StampPLC | Teensy LC |
|------------|------------|-----------|
| Standalone operation | Yes (LCD + buttons) | No (needs RPI) |
| Persistent settings | Yes | No |
| Cycle count storage | Yes (NVS) | No |
| High-freq flowmeters | GPIO interrupts | FreqMeasureMulti |
| Code size | ~1200 lines | ~380 lines |
| Cost | ~$35 | ~$15 + relay module |

---

## System Architecture

```
                                    +------------------+
                                    |   Raspberry Pi   |
                                    |      (HMI)       |
                                    +--------+---------+
                                             |
                                        UART (115200)
                                        JSON Protocol
                                             |
+-------------+    +---------------------+   |   +------------------+
|   12-24V    |--->|    M5StampPLC or    |<--+-->|   4x Solenoid    |
|   Power     |    |      Teensy LC      |       |     Valves       |
+-------------+    +----------+----------+       +------------------+
                              |
            +-----------------+-----------------+
            |                 |                 |
     +------+------+   +------+------+   +------+------+
     | Flowmeter 1 |   | Flowmeter 2 |   | Flowmeter 3 | ...
     | (Hall/Reed) |   | (Hall/Reed) |   | (Hall/Reed) |
     +-------------+   +-------------+   +-------------+
            |                 |                 |
     +------+------+   +------+------+   +------+------+
     |  Deadman 1  |   |  Deadman 2  |   |  Deadman 3  | ...
     |  (Switch)   |   |  (Switch)   |   |  (Switch)   |
     +-------------+   +-------------+   +-------------+
```

### Signal Flow

1. **Input** (deadman switch or RPI START command) triggers fill cycle
2. **Output** (relay) energizes solenoid valve
3. **Flowmeter** generates pulses as liquid flows
4. **Counter** (ISR) accumulates pulses
5. **K-factor** converts pulses to milliliters
6. **Setpoint** comparison determines when to stop
7. **State machine** manages cycle progression

---

## State Machine

Each of the 4 channels operates an independent state machine:

```
                    +-------+
          +-------->| IDLE  |<--------+
          |         +---+---+         |
          |             |             |
          |     Input ON (rising)     |
          |             |             |
          |             v             |
          |         +-------+         |
          |         |RUNNING|         |
          |         +---+---+         |
          |            /|\            |
          |           / | \           |
          |          /  |  \          |
    Input OFF       /   |   \    ml >= setpoint
    (early)        /    |    \        |
          |       /     |     \       |
          v      v      |      v      v
      +------+          |         +--------+
      |FAILED|          |         |COMPLETE|
      +--+---+          |         +----+---+
         |              |              |
         |   Input ON   |   Input OFF  |
         |   (restart)  |              |
         +--------->----+--------------+
```

### State Descriptions

| State | Description | Output | Display Color |
|-------|-------------|--------|---------------|
| IDLE | Waiting for input | OFF | White |
| RUNNING | Filling in progress | ON | Green |
| COMPLETE | Setpoint reached | OFF | Yellow |
| FAILED | Input released early | OFF | Red |

### Transitions

| From | To | Trigger |
|------|----|---------|
| IDLE | RUNNING | Input ON (physical or remote latch) |
| RUNNING | COMPLETE | ml >= setpoint |
| RUNNING | FAILED | Input OFF before setpoint |
| COMPLETE | IDLE | Input OFF (release) |
| FAILED | RUNNING | Input ON (restart with counter reset) |

---

## Pin Mapping

### M5StampPLC

```
+------------------+
|   M5StampPLC     |
|                  |
|  IN1-4: Deadman  |-----> Opto-isolated inputs (active LOW)
|  switches        |
|                  |
|  OUT1-4: Relays  |-----> Built-in relay outputs
|  (valves)        |
|                  |
|  GPIO1: FM1      |<----- Flowmeter 1 pulse (FALLING edge ISR)
|  GPIO2: FM2      |<----- Flowmeter 2 pulse
|  GPIO4: FM3      |<----- Flowmeter 3 pulse
|  GPIO5: FM4      |<----- Flowmeter 4 pulse
|                  |
|  GPIO40: RX2     |<----- From RPI TX
|  GPIO41: TX2     |-----> To RPI RX
|                  |
|  BtnA: Menu Up   |
|  BtnB: Menu Down |
|  BtnC: Select    |
+------------------+
```

### Teensy LC

```
+------------------+
|    Teensy LC     |
|                  |
|  Pin 7:  SW1     |<----- Deadman switch 1 (INPUT_PULLUP)
|  Pin 8:  SW2     |<----- Deadman switch 2
|  Pin 11: SW3     |<----- Deadman switch 3
|  Pin 12: SW4     |<----- Deadman switch 4
|                  |
|  I2C (18/19)     |-----> M5Stack 4-Relay Module (0x26)
|                  |
|  Pin 23: FM1     |<----- Flowmeter 1 (FreqMeasureMulti)
|  Pin 22: FM2     |<----- Flowmeter 2
|  Pin 20: FM3     |<----- Flowmeter 3
|  Pin 17: FM4     |<----- Flowmeter 4
|                  |
|  Pin 0:  RX1     |<----- From RPI TX
|  Pin 1:  TX1     |-----> To RPI RX
+------------------+
```

---

## Wiring Schematic

### Power Distribution

```
+12-24V DC Power Supply (2-3A)
        |
        +-----> M5StampPLC VIN
        |
        +-----> Relay Module Common (C terminals)
        |
        +-----> OPTO Board High Side (+12-21V)
        |
        +-----> Flowmeter VCC (if 12V type)

GND ----+-----> M5StampPLC GND
        |
        +-----> Relay Module GND
        |
        +-----> OPTO Board GND
        |
        +-----> Flowmeter GND
        |
        +-----> Valve GND (negative terminals)
```

### Flowmeter Connection (with OPTO isolation)

```
Flowmeter          OPTO Board           M5StampPLC
+---------+       +----------+         +----------+
| VCC (+) |------>| +12V     |         |          |
| GND (-) |------>| GND      |         |          |
| Signal  |------>| IN1      |         |          |
+---------+       |          |         |          |
                  | OUT1     |-------->| GPIO1    |
                  |          |         | (pullup) |
                  | GND(out) |-------->| GND      |
                  +----------+         +----------+
```

### Valve Connection

```
Relay Module                    Solenoid Valve
+----------+                   +----------+
| C (COM)  |<---- +12-24V      |          |
| NO       |------------------>| + (red)  |
| NC       | (not used)        |          |
+----------+                   | - (blk)  |----> GND
                               +----------+
```

### Complete Channel Wiring

```
CH1 Example:

[Deadman SW1] ---> [IN1 (opto)] ---> State Machine
                                           |
[Flowmeter1] ---> [OPTO IN1] ---> [GPIO1] -+-> Pulse Counter
                                           |
                               [OUT1 Relay] ---> [Valve1]
```

---

## Communication Protocol

### Physical Layer

| Parameter | M5StampPLC | Teensy LC |
|-----------|------------|-----------|
| Baud rate | 115200 | 115200 |
| Data bits | 8 | 8 |
| Parity | None | None |
| Stop bits | 1 | 1 |
| TX pin | GPIO41 | Pin 1 |
| RX pin | GPIO40 | Pin 0 |

### Protocol Overview

- **Controller to RPI:** JSON status packets every 500-2000ms
- **RPI to Controller:** Plain text commands (newline terminated)
- **Acknowledgment:** JSON response to each command

---

## Configuration

### M5StampPLC On-Device Menu

Press **Button C** (when all channels IDLE/FAILED) to enter settings:

```
SETTINGS MENU
─────────────────
> << EXIT
  Setpoint (all CH)    375
  K-Factor CH1         5.00
  K-Factor CH2         5.00
  K-Factor CH3         5.00
  K-Factor CH4         5.00
  Reset Cycle Counts

A:UP  B:DN  C:SELECT
```

### Setpoint Adjustment

- Range: 5 - 9999 ml
- Step: 5 ml per button press
- Applies to ALL channels (shared setpoint)

### K-Factor Adjustment

- Range: 0.01 - 100.00 pulses/ml
- Step: 0.01 per button press
- Independent per channel (for different flowmeters)

---

## Calibration

### K-Factor Determination

The K-factor converts flowmeter pulses to milliliters:

```
K-factor = Total Pulses / Actual Volume (ml)
```

### Calibration Procedure

1. Set K-factor to 1.00 (temporary)
2. Place container on calibrated scale
3. Initiate fill cycle
4. When scale reads target weight, release deadman
5. Record pulse count from display/serial
6. Calculate: `K = pulses / (weight_grams / density)`
7. Enter calculated K-factor via menu or serial command

### Example

```
Target: 375 ml of water
Scale reading after fill: 375g (water density ≈ 1.0)
Pulse count displayed: 1875 pulses

K-factor = 1875 / 375 = 5.0 pulses/ml
```

### Typical K-Factors

| Flowmeter Type | Typical K-Factor |
|----------------|------------------|
| YF-S201 (1/2") | 4.5 - 5.5 |
| YF-S401 (1/4") | 21 - 23 |
| YF-B1 (1") | 1.0 - 1.5 |

---

## Serial Commands

### From USB/Debug Terminal

| Command | Description | Example |
|---------|-------------|---------|
| `SP=xxx` | Set setpoint (ml) | `SP=500` |
| `K1=xx.xx` | Set K-factor CH1 | `K1=5.25` |
| `K2=xx.xx` | Set K-factor CH2 | `K2=4.80` |
| `K3=xx.xx` | Set K-factor CH3 | `K3=5.10` |
| `K4=xx.xx` | Set K-factor CH4 | `K4=5.00` |
| `STATUS` | Print current settings | `STATUS` |
| `DEBUG` | Show GPIO/counter states | `DEBUG` |
| `HELP` | Show command list | `HELP` |

### From RPI (Serial2/Serial1)

| Command | Description | Response |
|---------|-------------|----------|
| `SP=xxx` | Set setpoint | `{"response":"OK","setpoint":xxx}` |
| `K1=xx.xx` | Set K-factor | `{"response":"OK","channel":1,"kfactor":xx.xx}` |
| `START1` | Latch start CH1 | `{"response":"OK","action":"start","channel":1,"latched":true}` |
| `START2` | Latch start CH2 | (same format) |
| `START3` | Latch start CH3 | (same format) |
| `START4` | Latch start CH4 | (same format) |
| `STOP1` | Unlatch/stop CH1 | `{"response":"OK","action":"stop","channel":1,"latched":false}` |
| `STOP2` | Unlatch/stop CH2 | (same format) |
| `STOP3` | Unlatch/stop CH3 | (same format) |
| `STOP4` | Unlatch/stop CH4 | (same format) |
| `STATUS` | Request full status | (JSON status packet) |
| `RESETCYCLES` | Zero all cycle counts | `{"response":"OK","action":"reset_cycles",...}` |
| `DEBUG` | GPIO/counter debug | `{"gpio":{...},"counters":{...}}` |

---

## JSON Status Format

### M5StampPLC Status Packet

Sent every 2 seconds (configurable via `STATUS_REPORT_INTERVAL`):

```json
{
  "timestamp": 123456789,
  "setpoint": 375,
  "channels": [
    {
      "ch": 1,
      "state": "IDLE",
      "input": false,
      "output": false,
      "counts": 0,
      "ml": 0,
      "kfactor": 5.00,
      "cycles": 127,
      "latched": false
    },
    {
      "ch": 2,
      "state": "RUNNING",
      "input": true,
      "output": true,
      "counts": 1250,
      "ml": 250,
      "kfactor": 5.00,
      "cycles": 89,
      "latched": false
    },
    {
      "ch": 3,
      "state": "COMPLETE",
      "input": true,
      "output": false,
      "counts": 1875,
      "ml": 375,
      "kfactor": 5.00,
      "cycles": 156,
      "latched": false
    },
    {
      "ch": 4,
      "state": "FAILED",
      "input": false,
      "output": false,
      "counts": 800,
      "ml": 160,
      "kfactor": 5.00,
      "cycles": 42,
      "latched": false
    }
  ]
}
```

### Teensy LC Status Packet

Sent every 500ms:

```json
{
  "setpoint": 375,
  "channels": [
    {
      "state": "IDLE",
      "ml": 0.0,
      "input": false,
      "output": false,
      "cycles": 0,
      "latched": false
    },
    ...
  ]
}
```

### Field Definitions

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | int | Milliseconds since boot |
| `setpoint` | int | Target volume in ml |
| `ch` | int | Channel number (1-4) |
| `state` | string | IDLE, RUNNING, COMPLETE, FAILED |
| `input` | bool | Physical input state |
| `output` | bool | Relay output state |
| `counts` | int | Raw flowmeter pulse count |
| `ml` | int/float | Calculated volume (counts / kfactor) |
| `kfactor` | float | Pulses per milliliter |
| `cycles` | int | Total completed fills |
| `latched` | bool | Remote start latch state |

---

## Operation Modes

### Manual Mode (Deadman Switch)

1. Operator holds deadman switch for desired channel
2. Valve opens, filling begins
3. LCD shows real-time ml count
4. When setpoint reached: valve closes, state = COMPLETE
5. Operator releases switch: state returns to IDLE
6. If switch released early: state = FAILED, ml frozen at fail point

### Remote Mode (RPI Control)

1. RPI sends `START1` command
2. Controller latches channel 1 (no physical input needed)
3. Valve opens, filling begins
4. When setpoint reached: valve closes, latch auto-releases
5. State returns to IDLE when RPI sends `STOP1` or after complete

### Hybrid Mode

- Physical input OR remote latch triggers fill
- Either can initiate, either can abort
- Display shows green indicator for either source

---

## Safety Features

### Deadman Protection

- Releasing input mid-fill immediately stops valve
- State goes to FAILED, preserving ml count for diagnostics
- Prevents overflow if operator loses grip

### No Auto-Restart

- COMPLETE state requires input release before next fill
- Prevents accidental double-fills

### Configuration Lock

- Settings menu only accessible when all channels IDLE/FAILED
- Prevents parameter changes during active fills

### Watchdog (Teensy)

- FreqMeasureMulti library handles pulse timing edge cases
- I2C relay module has defined fail state

### Recommended External Safety

- Hardware E-STOP button cutting valve power
- Overflow catch basin
- Level sensors for source tank
- Pressure relief valves

---

## Bill of Materials

### M5StampPLC Variant

| Qty | Part | Description | Est. Cost |
|-----|------|-------------|-----------|
| 1 | M5StampPLC | ESP32 PLC controller | $32 |
| 4 | YF-S201 | 1/2" Hall effect flowmeter | $3 ea |
| 4 | 12V Solenoid | Normally-closed liquid valve | $8 ea |
| 1 | OPTO Board | 4-channel optocoupler | $5 |
| 4 | Deadman Switch | Momentary NO pushbutton | $2 ea |
| 1 | 24V 3A PSU | Din rail power supply | $15 |
| 1 | Enclosure | IP65 junction box | $12 |
| - | Wire, terminals | 18-22 AWG, ferrules | $15 |

**Total: ~$120**

### Teensy LC Variant

| Qty | Part | Description | Est. Cost |
|-----|------|-------------|-----------|
| 1 | Teensy LC | ARM Cortex-M0+ | $12 |
| 1 | M5Stack 4-Relay | I2C relay module | $15 |
| 4 | YF-S201 | 1/2" Hall effect flowmeter | $3 ea |
| 4 | 12V Solenoid | Normally-closed liquid valve | $8 ea |
| 4 | Deadman Switch | Momentary NO pushbutton | $2 ea |
| 1 | 24V 3A PSU | Din rail power supply | $15 |
| 1 | Enclosure | IP65 junction box | $12 |
| - | Wire, terminals | 18-22 AWG, ferrules | $15 |

**Total: ~$110** (requires RPI for HMI)

---

## Troubleshooting

### Flowmeter Not Counting

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| Counter stuck at 0 | Wiring disconnected | Check continuity |
| Counter stuck at 0 | Wrong GPIO pin | Verify pin mapping |
| Counter stuck at 0 | OPTO board not powered | Check 3.2V and 12V supplies |
| Erratic counts | Electrical noise | Add shielded cable, ferrite |
| Counts too fast | Floating input | Enable internal pullup |
| CH3/CH4 issues | GPIO4/5 conflict | Check v0.16 fixes |

### Valve Not Opening

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| No click from relay | Output not triggering | Check state machine |
| Click but no flow | Valve wiring reversed | Check polarity |
| Click but no flow | Insufficient voltage | Verify 12-24V at valve |
| Partial flow | Debris in valve | Clean/replace valve |

### Communication Issues

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| No JSON from controller | TX/RX swapped | Cross TX->RX, RX->TX |
| Garbled data | Baud rate mismatch | Verify 115200 both ends |
| Commands ignored | Missing newline | Send `\n` after command |
| Intermittent | Ground loop | Use common ground |

### Display Issues (M5StampPLC)

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| Blank screen | Not initialized | Check M5StamPLC.begin() |
| Flickering | Update rate too fast | Already limited to 250ms |
| Wrong colors | State not updating | Check forceDisplayUpdate |

---

## Version History

### M5StampPLC

| Version | Date | Changes |
|---------|------|---------|
| 0.16 | Jan 2026 | Fixed GPIO setup (CH3/CH4 counting issues) |
| 0.15 | Jan 2026 | Added cycle count persistence |
| 0.12 | Jan 2026 | Stable baseline |

### Teensy LC

| Version | Date | Changes |
|---------|------|---------|
| 2.1 | Jan 2026 | Added Serial1 HMI communication |
| 2.0 | Jan 2026 | Converted to I2C relay module |

---

## License

MIT License - See LICENSE file

---

## Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/improvement`)
3. Commit changes (`git commit -am 'Add feature'`)
4. Push to branch (`git push origin feature/improvement`)
5. Create Pull Request

---

## Contact

**Author:** Sky Singh
**Repository:** github.com/skysingh/GObagFill
**Issues:** github.com/skysingh/GObagFill/issues
