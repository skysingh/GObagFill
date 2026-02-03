# M5StampPLC Batch Fill Controller - Quick Reference v0.16

## Pin Assignments

| Channel | Flowmeter (GPIO) | Start Input | Relay Output |
|---------|------------------|-------------|--------------|
| CH1     | GPIO 1           | IN1         | OUT1         |
| CH2     | GPIO 2           | IN2         | OUT2         |
| CH3     | GPIO 4           | IN3         | OUT3         |
| CH4     | GPIO 5 ⚠️        | IN4         | OUT4         |

⚠️ GPIO5 is ESP32 strapping pin - no external pull-down!

## Raspberry Pi Serial
- **RX:** GPIO 40 ← RPi TX
- **TX:** GPIO 41 → RPi RX  
- **Baud:** 115200, 8N1

---

## State Machine

```
IDLE → [Input ON] → RUNNING → [Setpoint reached] → COMPLETE → [Input OFF] → IDLE
                         ↓
                   [Input OFF early]
                         ↓
                      FAILED → [Input ON] → RUNNING (restart)
```

---

## Display States

| State    | Color  | Meaning                           |
|----------|--------|-----------------------------------|
| IDLE     | White  | Ready for new cycle               |
| RUN      | Green  | Filling in progress               |
| DONE     | Yellow | Complete, release input           |
| FAIL     | Red    | Interrupted, shows failure volume |

---

## Button Functions

### Main Screen
- **Button C:** Enter Config (when IDLE or FAILED)

### Settings Menu
- **Button A:** Navigate UP / Increase value
- **Button B:** Navigate DOWN / Decrease value
- **Button C:** SELECT / SAVE

---

## Menu Options

1. **<< EXIT** - Return to main screen
2. **Setpoint** - 5-9999 ml (step 5)
3. **K-Factor CH1** - 0.01-100.0 (step 0.01)
4. **K-Factor CH2** - 0.01-100.0 (step 0.01)
5. **K-Factor CH3** - 0.01-100.0 (step 0.01)
6. **K-Factor CH4** - 0.01-100.0 (step 0.01)
7. **Reset Cycle Counts**

---

## Serial Commands

| Command | Description |
|---------|-------------|
| `SP=xxx` | Set setpoint (5-9999 ml) |
| `K1=x.xx` ... `K4=x.xx` | Set K-factor |
| `START1` ... `START4` | Remote start (latching) |
| `STOP1` ... `STOP4` | Remote stop |
| `STATUS` | Get JSON status |
| `DEBUG` | Show GPIO/counter info |
| `RESETCYCLES` | Zero cycle counters |
| `HELP` | List commands |

---

## K-Factor Calibration

```
K-factor = pulses ÷ actual_ml
```

**Example:** 520 pulses for 100ml → K = 5.2

---

## Default Values

| Parameter | Default |
|-----------|---------|
| Setpoint  | 375 ml  |
| K-Factor  | 5.0     |

---

## Troubleshooting Quick Guide

| Problem | Check |
|---------|-------|
| No counts | Flowmeter wiring, use DEBUG command |
| False counts | Add 4.7k pull-up resistor |
| CH4 issues | GPIO5 strapping - no pull-down |
| Can't enter config | Wait for IDLE or FAILED state |
| Inaccurate fills | Recalibrate K-factor |
