# LIBA Hardware Connections (from current firmware)

> Verify each connection on your physical prototype before powering it. Pin mapping below is extracted from the current firmware; photos may show earlier wiring revisions.

## Motor driver — L298N
| L298N | ESP32 GPIO |
|---|---:|
| IN1 | 26 |
| IN2 | 27 |
| IN3 | 14 |
| IN4 | 12 |
| ENA | 25 |
| ENB | 33 |

PWM configured in firmware: 5 kHz, 8-bit; default speed 120.

## IR line sensors (3 used by current firmware)
| Sensor | ESP32 GPIO |
|---|---:|
| Left | 34 |
| Center | 35 |
| Right | 32 |

The firmware treats the all-LOW sensor state as a stop marker. Confirm sensor polarity for your module.

## Servos (6)
| Servo | ESP32 GPIO |
|---|---:|
| Servo 1 | 17 |
| Servo 2 | 18 |
| Servo 3 | 19 |
| Servo 4 | 21 |
| Servo 5 | 22 |
| Servo 6 | 23 |

Servo 6 is used as the gripper/opening action in the firmware.

## Power and safety notes
- Use a suitable separate servo supply; do not power multiple servos from the ESP32 5 V rail.
- Connect grounds together (ESP32, motor driver, servo supply) unless the power design explicitly isolates them.
- Confirm motor-driver logic jumper/PWM configuration before use.
- Never connect a motor/servo supply voltage directly to ESP32 GPIO.
- Test the emergency-stop command and verify motor power behavior before autonomous missions.
