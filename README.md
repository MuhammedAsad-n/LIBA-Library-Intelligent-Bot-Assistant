# LIBA — Library Intelligent Bot Assistant 🤖📚

LIBA is a prototype autonomous mobile robot designed around a library book-retrieval workflow. The project combines an ESP32-controlled mobile platform, line-following navigation, a servo-based robotic arm, and a laptop desktop controller.

> **Repository status:** Contains the current ESP32 firmware you supplied, a reconstructed Python desktop controller, wiring notes, and project photos. The original laptop-side Python app was not found among the uploaded files; the desktop app here is a new implementation of the firmware's WebSocket protocol.

## System overview

```text
Laptop / Desktop GUI (Python Tkinter)
          |
          | Wi-Fi WebSocket :81
          v
       ESP32
       |   |
       |   +---- 3 IR line sensors (track + stop markers)
       |
       +---- L298N motor driver ---- DC drive motors
       |
       +---- 6 servo channels ---- robotic arm / gripper
```

## Current mission workflow
1. Robot starts at Stop 1 (home).
2. Desktop sends `T1:2` or `T1:3`.
3. Robot follows the track and counts stop markers.
4. At the selected stop, it performs the programmed arm sequence.
5. Robot reverses and returns to Home.
6. At Home, the gripper release action runs and the ESP32 reports `DELIVERED:<stop>`.

### Stop mapping in current firmware
- **Stop 1:** Home
- **Stop 2:** Shelf 1
- **Stop 3:** Shelf 2

## Repository structure

```text
LIBA/
├── README.md
├── LICENSE_NOTE.txt
├── .gitignore
├── desktop_app/
│   ├── liba_desktop.py
│   ├── requirements.txt
│   └── README.md
├── firmware/
│   └── esp32/
│       ├── platformio.ini
│       ├── src/
│       │   ├── main.cpp
│       │   └── liba_secrets.example.h
│       ├── liba_v1_0_multistop.ino
│       ├── liba_v1_0_timed_shelf.ino
│       └── liba_v1_1_line_following_return.ino
├── docs/
│   ├── PROJECT_OVERVIEW.md
│   ├── HARDWARE_CONNECTIONS.md
│   └── DEVELOPMENT_TIMELINE.md
└── assets/
    ├── early-prototype/
    ├── development/
    └── final-prototype/
```

## Run the desktop controller

### Requirements
- Python 3.9 or newer
- A desktop with Tkinter available
- ESP32 firmware running and reachable over Wi-Fi
- `websocket-client` Python package

### Install
```bash
cd desktop_app
python -m pip install -r requirements.txt
python liba_desktop.py
```

In the app, enter the ESP32 IP address shown in Serial Monitor, then click **Connect**.

## Desktop commands
| Action | Command |
|---|---|
| Retrieve from stop 2 | `T1:2` |
| Retrieve from stop 3 | `T1:3` |
| Emergency stop | `S` |
| Home/reset arm | `H` |
| Arm to shelf | `1` |
| Extend arm | `B1` |
| Retract arm | `R1` |
| Open gripper | `X` |
| Servo 1 manual angle | `A=<angle>` |
| Motor speed | `SPEED:<60–255>` |

## Firmware setup
1. Install Visual Studio Code and PlatformIO IDE extension.
2. Open `firmware/esp32/`.
3. Copy `src/liba_secrets.example.h` to `src/liba_secrets.h`.
4. Add local Wi-Fi SSID/password to `liba_secrets.h`.
5. Build and upload to the ESP32.
6. Check Serial Monitor for the ESP32 IP.
7. Connect the desktop controller to that IP on WebSocket port 81.

`liba_secrets.h` is ignored by Git so credentials are not published.

## Hardware pin connections
See [`docs/HARDWARE_CONNECTIONS.md`](docs/HARDWARE_CONNECTIONS.md). The tables are extracted from the current firmware. Please verify against the actual wiring before power-up.

## Photos
Project photos are included under `assets/development/`. Once the chronology is confirmed, move the early photos into `early-prototype/` and the final build photos into `final-prototype/` and update the image gallery in this README.

## Safety
- Test the emergency stop before autonomous operation.
- Keep wheels lifted for initial motor tests.
- Keep the arm clear of people and pinch points.
- Use appropriately rated power supplies for motors and servos.
- Ensure all required grounds are common where appropriate.
- Do not run autonomous missions unattended.

## Limitations and TODO
- Desktop app is reconstructed, not recovered from the original project files.
- Hardware has not been independently tested in this environment.
- Verify the multi-stop behavior before advertising it as supported.
- Add photos labeled by development phase.
- Add a video/GIF of a successful retrieval mission.
- Add BOM and mechanical CAD files if available.

## License
See `LICENSE_NOTE.txt`. Add an open-source license (MIT, Apache-2.0, etc.) only after deciding how you want the project licensed.
