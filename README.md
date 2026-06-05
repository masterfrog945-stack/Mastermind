# Mastermind — ESP32-S3 Controller Node Firmware

Controller-node firmware for **Sensor-based Mastermind (GAME6)**, a project that modernises the
classic *Mastermind* board game with embedded systems. This repository contains the master
controller firmware that runs the game logic, coordinates the wireless nodes, drives the scanning
platform, and talks to the companion Android app.

> Developed for *Engineering Experience 3 — Electronics and ICT Engineering*, KU Leuven, Faculty of
> Engineering Technology, Group T Leuven Campus (Academic Year 2025–2026).
> Team: Zhengyu Zhang, Wenqi Miao, Aaron Dumlao, Victor Charlier, Kian Cardoen — Coach: Wout Tessens.

---

## What this firmware does

The system is a three-node embedded board: one **controller node** (this repo) and two peripheral
nodes — a **sensing node** and a **feedback node**. The controller is the master that owns all the
game rules.

```
  TCS3200 color sensors           ┌─────────────────────────────┐         24× RG LEDs
   + PIC18F (sensing node)        │   ESP32-S3 Controller Node  │     + PIC18F (feedback node)
            │                     │        (this firmware)      │              ▲
            │  nRF24L01 2.4 GHz   │  • Mastermind game logic    │  nRF24L01    │
            └────────────────────▶│  • Scoring (exact / color)  │─────────────┘
                  16-byte RGBC    │  • Stepper platform control │   LED feedback
                                  │  • Wi-Fi + TCP server :5000 │
                                  └──────────────┬──────────────┘
                                                 │ Wi-Fi / TCP (JSON lines)
                                                 ▼
                                          Android companion app
```

On each turn this firmware:

1. **Receives** a 16-byte RGBC frame (4 color sensors × R/G/B/Clear) from the sensing node over the
   nRF24L01 link.
2. **Classifies** each token into a color and **streams the current guess** to the Android app once
   per second as a JSON line.
3. On a `SUBMIT_GUESS` command from the app, **scores the guess** against the secret code
   (exact-position matches and correct-color-wrong-position matches).
4. **Replays the LED feedback** for every played row to the feedback node, then restores the radio
   to sensor-receive mode.
5. **Advances the stepper platform** to the next row — or, on a win / loss, returns the platform to
   row 1 and starts a new game.

## Repository layout

| File | Responsibility |
|------|----------------|
| `main/main.c` | App entry point, Wi-Fi/TCP server, game loop, scoring, task orchestration |
| `main/NRF24L01.c/.h` | nRF24L01 driver (SPI transfers, TX/RX mode, packet send/receive) |
| `main/NRF24L01_Define.h` | nRF24L01 register and command definitions |
| `main/motor.c/.h` | 28BYJ-48 stepper control — `motor_run_1` … `motor_run_6`, per-row movement |
| `main/feedback_sender.c/.h` | Sends LED feedback packets to the feedback node (`send_feedback`, `feedback_clear`) |
| `main/led_patterns.c/.h` | LED position / color definitions used when building feedback |
| `main/CMakeLists.txt` | Component build definition |
| `CMakeLists.txt` | Top-level ESP-IDF project file |

## Architecture notes

- **Concurrency (FreeRTOS).** Two tasks share the radio and network stack:
  - `nrf_receive_task` — continuously reads sensor frames from the nRF24L01.
  - `tcp_server_task` — accepts one Android client on TCP port `5000` and handles its commands.
- **Mutexes.** `g_data_mutex` guards the shared 16-byte sensor buffer; `g_nrf_mutex` serialises every
  access to the nRF24L01 peripheral so sensing RX and feedback TX never collide.
- **Radio multiplexing.** A single nRF24L01 is reused for both directions. After sending LED
  feedback, `nrf_restore_sensor_rx_mode()` fully re-initialises the radio back to sensor-receive on
  channel `0x72`.
- **Color classification** is done on the master (`check_colour_from_rgb`) using RGB thresholds, so
  the sensing node only ships normalised data.

## App ↔ firmware protocol (JSON lines over TCP)

Each message is one JSON object terminated by `\n`.

**Firmware → app**

```json
{"type":"GUESS","colors":[1,1,2,2]}
{"type":"FEEDBACK","exact":2,"color":1,"attemptsLeft":4,"gameState":"PLAYING"}
```

`gameState` is one of `PLAYING`, `WON`, `LOST`. Color ids: `1=RED 2=GREEN 3=BLUE 4=YELLOW 5=CYAN 6=MAGENTA`, `0=UNDEFINED`.

**App → firmware**

```text
SUBMIT_GUESS   // score the current row and advance
RESET          // return platform to row 1 and start a new game
```

## Build & flash

This is an [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) project targeting the
**ESP32-S3-WROOM-1**.

```bash
# one-time, in your ESP-IDF environment
idf.py set-target esp32s3

# build, flash and watch the logs
idf.py build
idf.py -p <PORT> flash monitor      # e.g. -p COM5 on Windows
```

On boot the firmware logs the IP address it obtained; enter that IP with port `5000` in the Android
app to connect.

### Configuration

Edit the defines at the top of `main/main.c` before building:

| Define | Default | Meaning |
|--------|---------|---------|
| `WIFI_SSID` / `WIFI_PASS` | `open` / *(set your own)* | 2.4 GHz Wi-Fi the ESP32 joins |
| `TCP_PORT` | `5000` | Port the companion app connects to |
| `MAX_ROWS` | `6` | Number of guess rows |
| `CODE_LEN` | `4` | Tokens per row |
| `SENSOR_RF_CHANNEL` | `0x72` | nRF24L01 channel for the sensing link |

> **Note:** Wi-Fi credentials are currently compiled into `main/main.c`. Replace them with your own
> network and avoid committing real passwords to a public repository.

## Hardware

| Part | Used for |
|------|----------|
| ESP32-S3-WROOM-1 | Controller / master MCU (dual-core, Wi-Fi) |
| nRF24L01 (2.4 GHz) | Wireless link to sensing & feedback nodes |
| 28BYJ-48 5 V stepper + ULN2003A | Moving sensor platform (one axis, timing belt) |
| PIC18F57Q43 ×2 | Sensing node and feedback node MCUs |
| TCS3200 ×4 + 74HCT4051 mux | Frequency-based color sensing |
| CD4094 SIPO + MIC2981 + HLMP-4000 RG LEDs | Feedback display |

## Results (from the project paper)

| Metric | Result |
|--------|--------|
| Color-detection accuracy | 93.3 % (56/60); green & pink weakest at 80 % |
| Wireless feedback reliability | 100 % over 17 input vectors |
| Average total response time | 6.77 s (sensing dominates and varies most) |
| Restart (homing) success rate | 62.5 % (mechanical pulley/motor slip) |

See the full reliability study for design rationale and discussion.

## License

Academic project — © KU Leuven, Group T Leuven Campus. Reuse of the methods, schematics, and
programs requires written permission of the course coordinator and the authors.
