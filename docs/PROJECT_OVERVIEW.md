# LIBA Project Overview

**LIBA — Library Intelligent Bot Assistant** is a prototype autonomous mobile robot concept for assisting with library book retrieval.

## Concept
A laptop-side controller sends a mission to an ESP32. The robot follows a line/track, detects stop markers, reaches a shelf stop, performs a servo-based manipulation sequence, then returns to its home marker.

## Current architecture
- **Desktop:** Python Tkinter GUI, WebSocket client
- **Embedded controller:** ESP32
- **Communication:** Wi-Fi WebSocket, server port 81
- **Navigation:** 3 IR sensors for line following and marker detection
- **Drive:** DC motors through L298N
- **Manipulation:** 6 servo channels defined in current firmware
- **Stops:** 1 = Home, 2 = Shelf 1, 3 = Shelf 2

## Mission flow (current firmware)
1. Receive `T1:<stop>` command.
2. Start outbound navigation from home.
3. Count stop markers; skip the launch/home marker.
4. At the selected target marker, execute the programmed arm sequence.
5. Reverse and navigate toward home.
6. At home marker, release gripper, send `DELIVERED:<stop>`, and reset.

## Development history
The uploaded photographs show an early smaller experimental setup and a later, larger integrated prototype. The exact chronology of each individual photo is not yet labeled, so photos are stored in `assets/development/` without claiming a precise date or phase.

## Known limitations / verification needed
- The firmware's multiple-stop command parser exists, but its real behavior should be validated on the robot before describing multi-shelf retrieval as fully working.
- The current source uses 3 IR sensors; older hardware notes may mention different sensor counts.
- Servo count and GPIO mapping are taken from the current firmware.
- The desktop app is a new reconstruction; the original laptop application was not available in the uploaded files.
- The project package has not been physically tested against your robot hardware.
