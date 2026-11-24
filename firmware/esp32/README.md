# ESP32 Firmware

This folder contains the PlatformIO project for LIBA.

## Build / flash
1. Install VS Code and PlatformIO IDE extension.
2. Open this folder (`firmware/esp32`) as a PlatformIO project.
3. Create `src/liba_secrets.h` by copying `src/liba_secrets.example.h` and enter your Wi-Fi credentials.
4. Connect ESP32 and use PlatformIO **Upload**.
5. Open Serial Monitor at the baud rate configured in firmware to see Wi-Fi connection/IP and startup messages.

The secrets file is intentionally ignored by Git. Do not publish Wi-Fi passwords.

## Protocol
- `T1:2` / `T1:3` — retrieve from stop 2 / 3 and return home
- `S` — emergency stop
- `H` — home/reset arm
- `1` — arm to shelf position
- `B1` — extend arm to book
- `R1` — retract arm
- `X` — open gripper
- `A=angle` — manual Servo 1 angle
- `SPEED:60..255` — motor speed

The desktop app uses WebSocket on port 81.
