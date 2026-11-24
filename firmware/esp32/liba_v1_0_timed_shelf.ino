/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  LIBA 1.0 — ESP32 Firmware  (WiFi + WebSocket)              ║
 * ║  Platform : PlatformIO + VS Code                            ║
 * ║  Board    : ESP32 Dev Module                                 ║
 * ║  Framework: Arduino                                          ║
 * ║                                                              ║
 * ║  SHELF TIMING (timed drive — no IR):                        ║
 * ║    Shelf N  →  forward  N × 10 seconds                      ║
 * ║             →  arm sequence (shelf1FromHome + goHome)       ║
 * ║             →  backward N × 10 seconds  →  stop             ║
 * ║    e.g. Shelf 1 = 10 s fwd / 10 s back                     ║
 * ║         Shelf 2 = 20 s fwd / 20 s back  …etc.              ║
 * ║    Adjust SHELF_TIME_MS to match your track.                ║
 * ║                                                              ║
 * ║  PROTOCOL — Web → ESP32 commands:                           ║
 * ║    T1:N          → go to shelf N, run arm, return home      ║
 * ║    T1:N1,N2,...  → visit multiple shelves in order          ║
 * ║    S             → emergency stop (always accepted)         ║
 * ║    H             → go home (servo reset)                    ║
 * ║    1             → arm to shelf position (manual)           ║
 * ║    A=angle       → manual servo A (1–6) at angle            ║
 * ║    SPEED:N       → set motor speed (60–255)                 ║
 * ║                                                              ║
 * ║  PROTOCOL — ESP32 → Web messages:                           ║
 * ║    READY                → robot ready after connect         ║
 * ║    ACK:cmd              → command acknowledged              ║
 * ║    BUSY                 → robot busy, command ignored       ║
 * ║    GOING:shelf=N        → driving forward to shelf N        ║
 * ║    AT TARGET:N          → arm executing at shelf N          ║
 * ║    RETURNING:shelf=N    → driving back from shelf N         ║
 * ║    SHELF DONE:N         → shelf N complete                  ║
 * ║    DONE                 → all shelves complete              ║
 * ║    ESTOP                → emergency stop fired              ║
 * ║    ERR:reason           → unknown / bad command             ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

/* ───────────────────────────────────────────────────────────────
   WIFI CREDENTIALS  ←  CHANGE THESE
   ─────────────────────────────────────────────────────────────── */
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

/* ───────────────────────────────────────────────────────────────
   Shelf timing
   SHELF_TIME_MS — milliseconds the robot drives PER shelf unit.
   Shelf 1 = 1 × SHELF_TIME_MS forward, then same backward.
   Shelf 2 = 2 × SHELF_TIME_MS forward, then same backward.
   Tune this number to match the physical distance on your track.
   ─────────────────────────────────────────────────────────────── */
#define SHELF_TIME_MS  10000UL   // 10 000 ms = 10 seconds per shelf unit

/* ───────────────────────────────────────────────────────────────
   WebSocket server — port 81
   ─────────────────────────────────────────────────────────────── */
WebSocketsServer wsServer(81);
uint8_t connectedClient = 0;
bool    clientConnected = false;

/* ───────────────────────────────────────────────────────────────
   Motor driver  (L298N)
   ─────────────────────────────────────────────────────────────── */
#define IN1 26
#define IN2 27
#define IN3 14
#define IN4 12
#define ENA 25
#define ENB 33

int motorSpeed = 120;

#define LEDC_CH_A  0
#define LEDC_CH_B  1
#define LEDC_FREQ  5000
#define LEDC_RES   8

/* ── Motor running flag ─────────────────────────────────────────
   Set true whenever the DC motors are spinning.
   smoothMove() checks this and refuses to move any servo while
   motors are active — prevents power-rail conflicts and jitter.
   ─────────────────────────────────────────────────────────────── */
bool motorsRunning = false;

void motorPWM(int chA, int chB) {
  ledcWrite(LEDC_CH_A, chA);
  ledcWrite(LEDC_CH_B, chB);
}

/* ───────────────────────────────────────────────────────────────
   6-DOF Arm servos
   ─────────────────────────────────────────────────────────────── */
Servo servo1, servo2, servo3, servo4, servo5, servo6;

#define SERVO1_PIN  17
#define SERVO2_PIN  18
#define SERVO3_PIN  19
#define SERVO4_PIN  21
#define SERVO5_PIN  22
#define SERVO6_PIN  23

/* SPEED_DELAY  — ms per degree step during a servo sweep.
   SERVO_SETTLE — pause after each servo finishes before the
   next one starts; lets the power rail recover.               */
#define SPEED_DELAY   30
#define SERVO_SETTLE  150

int currentPos[6] = {0, 160, 180, 10, 10, 30};
int homePos[6]    = {0, 160, 180, 10, 10, 30};

/* ───────────────────────────────────────────────────────────────
   Multi-shelf queue
   ─────────────────────────────────────────────────────────────── */
#define MAX_STOPS 10
int  stopTargets[MAX_STOPS];
int  stopTargetCount = 0;
int  stopTargetIndex = 0;
bool executing       = false;

/* ═══════════════════════════════════════════════════════════════
   Forward declarations
   ═══════════════════════════════════════════════════════════════ */
void wsSend(const String& msg);
void motorPWM(int chA, int chB);
void forward();
void backward();
void stopMotors();
void driveForwardTimed(unsigned long ms);
void driveBackwardTimed(unsigned long ms);
void ensureAttached();
void moveServoRaw(int s, int angle);
void smoothMove(int s, int target);
void goHome();
void shelf1FromHome();
void runArmSequence(int shelfNumber);
void goToShelf(int shelfNumber);
void manualCommand(const String& cmd);
bool parseStopCommand(const String& cmd);
void resetExecution();
void handleCommand(const String& rawCmd);
void onWebSocketEvent(uint8_t clientNum, WStype_t type,
                      uint8_t* payload, size_t length);

/* ═══════════════════════════════════════════════════════════════
   WebSocket send helper
   ═══════════════════════════════════════════════════════════════ */
void wsSend(const String& msg) {
  Serial.println("[WS OUT] " + msg);
  if (clientConnected) {
    wsServer.sendTXT(connectedClient, msg.c_str());
  }
}

/* ═══════════════════════════════════════════════════════════════
   Motor primitives
   motorsRunning kept in sync so smoothMove() knows when to block.
   ═══════════════════════════════════════════════════════════════ */
void forward() {
  motorsRunning = true;
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  motorPWM(motorSpeed, motorSpeed);
}

void backward() {
  motorsRunning = true;
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
  motorPWM(motorSpeed, motorSpeed);
}

void stopMotors() {
  /* Clear flag BEFORE cutting power so any servo call issued
     immediately after stopMotors() is not blocked.           */
  motorsRunning = false;
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  motorPWM(0, 0);
}

/* ───────────────────────────────────────────────────────────────
   driveForwardTimed / driveBackwardTimed
   Drives for exactly `ms` milliseconds while keeping the
   WebSocket alive (wsServer.loop() every 50 ms).
   Exits early if an emergency stop clears `executing`.
   ─────────────────────────────────────────────────────────────── */
void driveForwardTimed(unsigned long ms) {
  Serial.printf("[DRIVE] Forward %lu ms\n", ms);
  forward();
  unsigned long start = millis();
  while (millis() - start < ms) {
    wsServer.loop();
    if (!executing) break;   // emergency stop
    delay(50);
  }
  stopMotors();
  delay(300);                // short power-rail settle before arm
}

void driveBackwardTimed(unsigned long ms) {
  Serial.printf("[DRIVE] Backward %lu ms\n", ms);
  backward();
  unsigned long start = millis();
  while (millis() - start < ms) {
    wsServer.loop();
    if (!executing) break;   // emergency stop
    delay(50);
  }
  stopMotors();
  delay(300);
}

/* ═══════════════════════════════════════════════════════════════
   Servo helpers
   ═══════════════════════════════════════════════════════════════ */
void ensureAttached() {
  if (!servo1.attached()) servo1.attach(SERVO1_PIN, 500, 2500);
  if (!servo2.attached()) servo2.attach(SERVO2_PIN, 500, 2500);
  if (!servo3.attached()) servo3.attach(SERVO3_PIN, 500, 2500);
  if (!servo4.attached()) servo4.attach(SERVO4_PIN, 500, 2500);
  if (!servo5.attached()) servo5.attach(SERVO5_PIN, 500, 2500);
  if (!servo6.attached()) servo6.attach(SERVO6_PIN, 500, 2500);
}

void moveServoRaw(int s, int angle) {
  switch (s) {
    case 1: servo1.write(angle); break;
    case 2: servo2.write(angle); break;
    case 3: servo3.write(angle); break;
    case 4: servo4.write(angle); break;
    case 5: servo5.write(angle); break;
    case 6: servo6.write(angle); break;
  }
}

/*
 * smoothMove — sweeps ONE servo one degree per step.
 *
 * MOTOR INTERLOCK:
 *   Returns immediately if DC motors are spinning. All callers
 *   must call stopMotors() before invoking arm motion.
 *
 * SETTLE DELAY:
 *   SERVO_SETTLE ms after reaching target so the power rail
 *   recovers before the next servo starts.
 */
void smoothMove(int s, int target) {
  if (motorsRunning) {
    Serial.printf("[SERVO] Blocked — motors running (servo %d → %d°)\n", s, target);
    return;
  }

  target = constrain(target, 0, 180);
  int* p = &currentPos[s - 1];

  if (*p < target) {
    for (int i = *p; i <= target; i++) {
      moveServoRaw(s, i);
      delay(SPEED_DELAY);
      wsServer.loop();
    }
  } else {
    for (int i = *p; i >= target; i--) {
      moveServoRaw(s, i);
      delay(SPEED_DELAY);
      wsServer.loop();
    }
  }
  *p = target;
  delay(SERVO_SETTLE);
}

/* ═══════════════════════════════════════════════════════════════
   Arm sequences
   ═══════════════════════════════════════════════════════════════ */
void goHome() {
  ensureAttached();
  for (int i = 1; i <= 6; i++) {
    smoothMove(i, homePos[i - 1]);
  }
  delay(300);
}

void shelf1FromHome() {
  smoothMove(6, 0);
  smoothMove(5, 0);
  smoothMove(4, 40);
  smoothMove(3, 150);
  smoothMove(2, 120);
  smoothMove(3, 100);
  smoothMove(2, 100);
  smoothMove(4, 50);
  smoothMove(1, 90);
  smoothMove(6, 42);
}

/* ═══════════════════════════════════════════════════════════════
   runArmSequence
   Performs the full pick cycle at the current position.
   Motors must already be stopped before this is called.
   ═══════════════════════════════════════════════════════════════ */
void runArmSequence(int shelfNumber) {
  wsSend("AT TARGET:" + String(shelfNumber));
  Serial.printf("[ARM] Sequence for shelf %d\n", shelfNumber);

  /* Belt-and-suspenders: guarantee motors are off */
  stopMotors();
  delay(300);

  ensureAttached();
  shelf1FromHome();
  delay(500);
  goHome();
}

/* ═══════════════════════════════════════════════════════════════
   goToShelf — complete timed mission for one shelf:
     Phase 1 — Drive forward  shelfNumber × SHELF_TIME_MS
     Phase 2 — Run arm sequence (pick + return to home pose)
     Phase 3 — Drive backward shelfNumber × SHELF_TIME_MS
   ═══════════════════════════════════════════════════════════════ */
void goToShelf(int shelfNumber) {
  unsigned long travelMs = (unsigned long)shelfNumber * SHELF_TIME_MS;

  /* ── Phase 1: drive to shelf ── */
  wsSend("GOING:shelf=" + String(shelfNumber));
  driveForwardTimed(travelMs);

  if (!executing) return;   // bail on emergency stop

  /* ── Phase 2: arm work ── */
  runArmSequence(shelfNumber);

  if (!executing) return;

  /* ── Phase 3: return home ── */
  wsSend("RETURNING:shelf=" + String(shelfNumber));
  driveBackwardTimed(travelMs);
}

/* ═══════════════════════════════════════════════════════════════
   Manual servo command parser  ("A=90", "F=42", …)
   ═══════════════════════════════════════════════════════════════ */
void manualCommand(const String& cmd) {
  char servoChar = toupper(cmd.charAt(0));
  int  angle     = cmd.substring(cmd.indexOf('=') + 1).toInt();
  int  s         = servoChar - 'A' + 1;
  if (s >= 1 && s <= 6) {
    smoothMove(s, angle);
  } else {
    wsSend("ERR:INVALID_SERVO:" + String(servoChar));
  }
}

/* ═══════════════════════════════════════════════════════════════
   Multi-shelf parser  ("T1:1,2" → stopTargets = [1, 2])
   ═══════════════════════════════════════════════════════════════ */
bool parseStopCommand(const String& cmd) {
  int colon = cmd.indexOf(':');
  if (colon < 0) return false;

  String listStr = cmd.substring(colon + 1);
  listStr.trim();
  stopTargetCount = 0;

  int start = 0;
  while (start < (int)listStr.length() && stopTargetCount < MAX_STOPS) {
    int    comma = listStr.indexOf(',', start);
    String token;
    if (comma < 0) {
      token = listStr.substring(start);
      start = (int)listStr.length();
    } else {
      token = listStr.substring(start, comma);
      start = comma + 1;
    }
    token.trim();
    if (token.length() > 0) {
      int n = token.toInt();
      if (n > 0) stopTargets[stopTargetCount++] = n;
    }
  }

  /* Bubble sort ascending so shelves are visited nearest-first */
  for (int i = 0; i < stopTargetCount - 1; i++) {
    for (int j = i + 1; j < stopTargetCount; j++) {
      if (stopTargets[j] < stopTargets[i]) {
        int tmp        = stopTargets[i];
        stopTargets[i] = stopTargets[j];
        stopTargets[j] = tmp;
      }
    }
  }

  return stopTargetCount > 0;
}

/* ═══════════════════════════════════════════════════════════════
   Reset all execution state
   ═══════════════════════════════════════════════════════════════ */
void resetExecution() {
  stopTargetIndex = 0;
  stopTargetCount = 0;
  executing       = false;
}

/* ═══════════════════════════════════════════════════════════════
   Command handler
   ═══════════════════════════════════════════════════════════════ */
void handleCommand(const String& rawCmd) {
  String cmd = rawCmd;
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.println("[CMD] " + cmd);
  wsSend("ACK:" + cmd);

  /* ── Emergency stop — always accepted ── */
  if (cmd == "S") {
    stopMotors();
    resetExecution();
    wsSend("ESTOP");
    return;
  }

  /* ── Motor speed live update ── */
  if (cmd.startsWith("SPEED:")) {
    int spd = cmd.substring(6).toInt();
    if (spd >= 60 && spd <= 255) {
      motorSpeed = spd;
      Serial.printf("[SPEED] Set to %d\n", motorSpeed);
    } else {
      wsSend("ERR:SPEED_OUT_OF_RANGE");
    }
    return;
  }

  /* ── Multi-shelf timed mission ── */
  if (cmd.startsWith("T1:")) {
    resetExecution();
    if (parseStopCommand(cmd)) {
      executing = true;
      Serial.printf("[RUN] %d shelf(ves) queued: ", stopTargetCount);
      for (int i = 0; i < stopTargetCount; i++) Serial.printf("%d ", stopTargets[i]);
      Serial.println();

      /* Visit each shelf in order. Each goToShelf() drives there,
         runs the arm, and drives back to the start position before
         the next shelf begins.                                    */
      for (stopTargetIndex = 0;
           stopTargetIndex < stopTargetCount && executing;
           stopTargetIndex++) {

        int shelf = stopTargets[stopTargetIndex];
        goToShelf(shelf);

        if (!executing) break;   // emergency stop mid-mission

        wsSend("SHELF DONE:" + String(shelf));
        Serial.printf("[DONE] Shelf %d complete\n", shelf);

        if (stopTargetIndex + 1 < stopTargetCount) {
          delay(500);   // brief pause between consecutive shelves
        }
      }

      if (executing) {
        wsSend("DONE");
        Serial.println("[DONE] All shelves complete");
        resetExecution();
      }
    } else {
      wsSend("ERR:BAD_SHELF_LIST");
    }
    return;
  }

  /* ── Immediate arm / manual commands (idle only) ── */
  if      (cmd == "H")            { ensureAttached(); goHome(); }
  else if (cmd == "1")            { ensureAttached(); shelf1FromHome(); }
  else if (cmd.indexOf('=') > 0)  { ensureAttached(); manualCommand(cmd); }
  else                            { wsSend("ERR:UNKNOWN:" + cmd); }
}

/* ═══════════════════════════════════════════════════════════════
   WebSocket event handler
   ═══════════════════════════════════════════════════════════════ */
void onWebSocketEvent(uint8_t clientNum, WStype_t type,
                      uint8_t* payload, size_t length) {
  switch (type) {

    case WStype_CONNECTED: {
      IPAddress ip = wsServer.remoteIP(clientNum);
      Serial.printf("[WS] Client #%u connected from %s\n",
                    clientNum, ip.toString().c_str());
      connectedClient = clientNum;
      clientConnected = true;
      wsSend("READY");
      break;
    }

    case WStype_DISCONNECTED: {
      Serial.printf("[WS] Client #%u disconnected\n", clientNum);
      if (clientNum == connectedClient) {
        clientConnected = false;
        if (executing) {
          stopMotors();
          resetExecution();
          Serial.println("[SAFETY] Motors stopped — client disconnected");
        }
      }
      break;
    }

    case WStype_TEXT: {
      String cmd = String((char*)payload);
      cmd.trim();
      /* Emergency stop is always accepted, even mid-mission */
      if (cmd == "S") {
        handleCommand(cmd);
      } else if (executing) {
        wsSend("BUSY");
      } else {
        handleCommand(cmd);
      }
      break;
    }

    default:
      break;
  }
}

/* ═══════════════════════════════════════════════════════════════
   SETUP
   ═══════════════════════════════════════════════════════════════ */
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n╔══════════════════════════════╗"));
  Serial.println(F("║  LIBA 1.0  — Booting...     ║"));
  Serial.println(F("╚══════════════════════════════╝"));

  /* ── Motor driver ── */
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  ledcSetup(LEDC_CH_A, LEDC_FREQ, LEDC_RES);
  ledcSetup(LEDC_CH_B, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(ENA, LEDC_CH_A);
  ledcAttachPin(ENB, LEDC_CH_B);
  motorPWM(0, 0);

  /* ── Servos ── */
  servo1.attach(SERVO1_PIN, 500, 2500);
  servo2.attach(SERVO2_PIN, 500, 2500);
  servo3.attach(SERVO3_PIN, 500, 2500);
  servo4.attach(SERVO4_PIN, 500, 2500);
  servo5.attach(SERVO5_PIN, 500, 2500);
  servo6.attach(SERVO6_PIN, 500, 2500);
  delay(200);

  Serial.println(F("[INIT] Moving arm to home position..."));
  goHome();
  Serial.println(F("[INIT] Arm at home — ready"));

  /* ── WiFi ── */
  Serial.printf("[WIFI] Connecting to: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
    if (++attempts > 40) {
      Serial.println(F("\n[WIFI] Failed after 20s — restarting..."));
      delay(2000);
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println(F("╔══════════════════════════════════════════╗"));
  Serial.println(F("║  WiFi connected!                         ║"));
  Serial.print  (F("║  ESP32 IP: "));
  Serial.print  (WiFi.localIP());
  Serial.println(F("                       ║"));
  Serial.println(F("║  >>> PASTE THIS IP INTO THE WEB APP <<<  ║"));
  Serial.println(F("╚══════════════════════════════════════════╝"));
  Serial.printf("[WIFI] Signal: %d dBm\n", WiFi.RSSI());

  /* ── WebSocket server on port 81 ── */
  wsServer.begin();
  wsServer.onEvent(onWebSocketEvent);
  Serial.println(F("[WS] Server started on port 81"));
  Serial.println(F("[READY] Waiting for web app..."));
}

/* ═══════════════════════════════════════════════════════════════
   LOOP
   The shelf mission runs synchronously inside handleCommand(),
   so the main loop only needs to service WebSocket events.
   ═══════════════════════════════════════════════════════════════ */
void loop() {
  wsServer.loop();
}
