/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  LIBA 1.1 — ESP32 Firmware  (WiFi + WebSocket)              ║
 * ║  Platform : PlatformIO + VS Code                            ║
 * ║  Board    : ESP32 Dev Module                                 ║
 * ║  Framework: Arduino                                          ║
 * ║                                                              ║
 * ║  STOP LAYOUT:                                               ║
 * ║    Stop 1 = Home base (bot start position)                  ║
 * ║    Stop 2 = Shelf 1                                         ║
 * ║    Stop 3 = Shelf 2                                         ║
 * ║                                                              ║
 * ║  MISSION FLOW:                                              ║
 * ║    1. Bot at Stop 1, drives forward (outbound)              ║
 * ║    2. Passes Stop 1 marker (ignored — it's the launch pad)  ║
 * ║    3. Reaches target shelf stop → runs arm sequence          ║
 * ║    4. Reverses backward (return journey)                    ║
 * ║    5. Hits Stop 1 again → releases gripper + sends          ║
 * ║       DELIVERED:N → resets state                            ║
 * ║                                                              ║
 * ║  PROTOCOL — Web → ESP32 commands:                           ║
 * ║    T1:N          → go to stop N, run arm, return home       ║
 * ║    T1:N1,N2,...  → visit multiple stops in order            ║
 * ║    S             → emergency stop (always accepted)         ║
 * ║    H             → go home (servo reset)                    ║
 * ║    1             → arm to shelf position                    ║
 * ║    B1            → extend arm to book                       ║
 * ║    R1            → retract arm to shelf                     ║
 * ║    X             → open gripper (servo6 → 0°)              ║
 * ║    A=angle       → manual servo A (1) at angle              ║
 * ║    SPEED:N       → set motor speed (60–255)                 ║
 * ║                                                              ║
 * ║  PROTOCOL — ESP32 → Web messages:                           ║
 * ║    READY                → robot ready after connect         ║
 * ║    ACK:cmd              → command acknowledged              ║
 * ║    BUSY                 → robot busy, command ignored       ║
 * ║    GOING:stops=N        → starting run, N stops queued      ║
 * ║    STOP:N               → stop marker N detected            ║
 * ║    AT TARGET:N          → arm executing at stop number N    ║
 * ║    RESUMING:next=N      → driving to next stop N            ║
 * ║    RETURNING            → heading back to home base         ║
 * ║    DELIVERED:N          → book delivered, gripper released  ║
 * ║    DONE                 → all stops complete                ║
 * ║    ESTOP                → emergency stop fired              ║
 * ║    ERR:reason           → unknown / bad command             ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Arduino.h>
#include <WiFi.h>
#if defined(__has_include)
  #if __has_include(<WebSocketsServer.h>)
    #include <WebSocketsServer.h>
  #else
    enum WStype_t { WStype_DISCONNECTED, WStype_CONNECTED, WStype_TEXT };
    class WebSocketsServer {
    public:
      WebSocketsServer(uint16_t) {}
      void begin() {}
      void onEvent(void (*)(uint8_t, WStype_t, uint8_t*, size_t)) {}
      void loop() {}
      void sendTXT(uint8_t, const char*) {}
      IPAddress remoteIP(uint8_t) { return IPAddress(0, 0, 0, 0); }
    };
  #endif
#else
  #include <WebSocketsServer.h>
#endif
#include <ESP32Servo.h>

/* ───────────────────────────────────────────────────────────────
   WIFI CREDENTIALS  ←  CHANGE THESE
   ─────────────────────────────────────────────────────────────── */
// Create liba_secrets.h from liba_secrets.example.h for your local Wi-Fi.
// The secrets header is ignored by Git.
#if __has_include("liba_secrets.h")
  #include "liba_secrets.h"
#else
  const char* WIFI_SSID     = "YOUR_WIFI_SSID";
  const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
#endif

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
   IR line sensors
   ─────────────────────────────────────────────────────────────── */
#define IR_LEFT   34
#define IR_CENTER 35
#define IR_RIGHT  32

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

/* SPEED_DELAY — ms per degree step.
   SERVO_SETTLE — pause after each servo finishes moving before
   the next one starts. Gives the power rail time to recover and
   prevents mechanical coupling jitter.                           */
#define SPEED_DELAY   30
#define SERVO_SETTLE  150   // ms — tune up if jitter persists

int currentPos[6] = {0, 160, 180, 10, 10, 30};
int homePos[6]    = {0, 160, 180, 10, 10, 30};

/* ───────────────────────────────────────────────────────────────
   Multi-stop state machine
   ─────────────────────────────────────────────────────────────── */
#define MAX_STOPS 10
int  stopTargets[MAX_STOPS];
int  stopTargetCount = 0;
int  stopTargetIndex = 0;
int  stopCount       = 0;        // counts markers seen on outbound leg
bool stopDetected    = false;
bool executing       = false;

/* ── Return-journey state ───────────────────────────────────────
   Set to true after the last shelf arm sequence completes and
   the bot starts reversing toward Stop 1 (home base).
   While returningHome == true the bot is on the return leg:
     • lineFollow() uses backward() for steering
     • Any Stop 1 marker hit triggers gripper release + DELIVERED
   ─────────────────────────────────────────────────────────────── */
bool returningHome      = false;
int  lastDeliveredStop  = 0;     // which shelf stop was just served

/* ═══════════════════════════════════════════════════════════════
   Forward declarations
   ═══════════════════════════════════════════════════════════════ */
void wsSend(const String& msg);
void motorPWM(int chA, int chB);
void forward();
void backward();
void turnLeft();
void turnRight();
void stopMotors();
void ensureAttached();
void moveServoRaw(int s, int angle);
void smoothMove(int s, int target);
void goHome();
void shelf1FromHome();
void shelf1ToBook();
void bookToShelf1();
void runArmSequence(int stopNumber);
void deliverAtHome();
void manualCommand(const String& cmd);
bool parseStopCommand(const String& cmd);
void resetExecution();
void handleCommand(const String& rawCmd);
void lineFollow();
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
   Motor functions
   motorsRunning is kept in sync with every direction change
   so that smoothMove() always knows the correct motor state.
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
void turnLeft() {
  motorsRunning = true;
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  motorPWM(motorSpeed, motorSpeed);
}
void turnRight() {
  motorsRunning = true;
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
  motorPWM(motorSpeed, motorSpeed);
}
void stopMotors() {
  /* Clear flag BEFORE cutting power so any servo call issued
     immediately after stopMotors() is not blocked.              */
  motorsRunning = false;
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  motorPWM(0, 0);
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
 * smoothMove — moves ONE servo at a time, one degree per step.
 *
 * MOTOR INTERLOCK:
 *   If DC motors are spinning (motorsRunning == true), this
 *   function does nothing and returns immediately. The caller
 *   (arm sequences, lineFollow) is responsible for calling
 *   stopMotors() and waiting before invoking arm motion.
 *
 * SETTLE DELAY:
 *   After reaching the target, SERVO_SETTLE ms is added before
 *   returning. This lets the power rail stabilise before the
 *   next servo starts, preventing coupled jitter.
 */
void smoothMove(int s, int target) {
  /* ── Motor interlock ── */
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
      wsServer.loop();   // keep WebSocket alive during sweep
    }
  } else {
    for (int i = *p; i >= target; i--) {
      moveServoRaw(s, i);
      delay(SPEED_DELAY);
      wsServer.loop();
    }
  }
  *p = target;

  /* Settle: let the rail recover before the next servo moves */
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

/*
 * shelf1FromHome — NEW sequence (v1.1)
 *
 *   Step  1 : A (servo1) →   0°
 *   Step  2 : C (servo3) → 140°
 *   Step  3 : B (servo2) → 130°
 *   Step  4 : C (servo3) → 140°
 *   Step  5 : D (servo4) →  40°
 *   Step  6 : D (servo4) →   0°
 *   Step  7 : A (servo1) →   0°
 *   Step  8 : C (servo3) → 160°
 *   Step  9 : B (servo2) → 160°
 *   Step 10 : C (servo3) → 180°
 */
void shelf1FromHome() {
  smoothMove(1,   0);   // A → 0°
  smoothMove(3, 140);   // C → 140°
  smoothMove(2, 130);   // B → 130°
  smoothMove(3, 140);   // C → 140°
  smoothMove(4,  40);   // D → 40°
  smoothMove(4,   0);   // D → 0°
  smoothMove(1,   0);   // A → 0°  (re-confirm position)
  smoothMove(3, 160);   // C → 160°
  smoothMove(2, 160);   // B → 160°
  smoothMove(3, 180);   // C → 180°
}

/* shelf1ToBook and bookToShelf1 — UNCHANGED from v1.0 */
void shelf1ToBook() {
  smoothMove(1,   0);
  smoothMove(3, 120);
  smoothMove(2, 120);
  smoothMove(3, 130);
  smoothMove(2, 140);
  smoothMove(4,  70);
  smoothMove(2, 130);
  smoothMove(3, 160);
  smoothMove(4, 110);
  smoothMove(3, 180);
}

void bookToShelf1() {
  smoothMove(3, 160);
  smoothMove(4, 110);
  smoothMove(3, 130);
  smoothMove(2, 130);
  smoothMove(4,  70);
  smoothMove(2, 140);
  smoothMove(3, 130);
  smoothMove(2, 120);
  smoothMove(3, 120);
  smoothMove(1,  90);
}

/* ═══════════════════════════════════════════════════════════════
   Full arm sequence at a target shelf stop.
   Motors are guaranteed stopped before any servo moves.
   ═══════════════════════════════════════════════════════════════ */
void runArmSequence(int stopNumber) {
  wsSend("AT TARGET:" + String(stopNumber));

  /* Hard stop + short power-settle before touching servos */
  stopMotors();
  delay(300);

  ensureAttached();
  shelf1FromHome();
  delay(500);
  shelf1ToBook();
  delay(500);
  bookToShelf1();
  delay(500);
  goHome();
}

/* ═══════════════════════════════════════════════════════════════
   deliverAtHome — called when bot returns to Stop 1 (home base)
   with a book on board.

   Actions:
     1. Stop motors
     2. Release gripper (servo6 → 0°)
     3. Send DELIVERED:N  (N = the shelf stop that was served)
     4. Reset all execution state
   ═══════════════════════════════════════════════════════════════ */
void deliverAtHome() {
  stopMotors();
  delay(300);

  Serial.printf("[DELIVER] Releasing gripper at home — served stop %d\n",
                lastDeliveredStop);

  ensureAttached();
  smoothMove(6, 0);   // F (servo6) → 0° = open gripper
  delay(500);

  wsSend("DELIVERED:" + String(lastDeliveredStop));
  Serial.println("[DELIVER] Book delivered. Status updated.");

  resetExecution();
}

/* ═══════════════════════════════════════════════════════════════
   Manual servo command parser  ("A=90", "C=140", …)
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
   Multi-stop parser  ("T1:2,4" → stopTargets = [2, 4])
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

  /* Bubble sort ascending */
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
  stopCount        = 0;
  stopTargetIndex  = 0;
  stopTargetCount  = 0;
  stopDetected     = false;
  executing        = false;
  returningHome    = false;
  lastDeliveredStop = 0;
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

  /* ── Multi-stop travel ── */
  if (cmd.startsWith("T1:")) {
    resetExecution();
    if (parseStopCommand(cmd)) {
      executing = true;
      wsSend("GOING:stops=" + String(stopTargetCount));
      Serial.printf("[RUN] %d stop(s) queued: ", stopTargetCount);
      for (int i = 0; i < stopTargetCount; i++) Serial.printf("%d ", stopTargets[i]);
      Serial.println();
    } else {
      wsSend("ERR:BAD_STOP_LIST");
    }
    return;
  }

  /* ── Immediate arm / gripper / manual commands ── */
  if      (cmd == "H")            { ensureAttached(); goHome(); }
  else if (cmd == "1")            { ensureAttached(); shelf1FromHome(); }
  else if (cmd == "B1")           { ensureAttached(); shelf1ToBook(); }
  else if (cmd == "R1")           { ensureAttached(); bookToShelf1(); }
  else if (cmd == "X")            { ensureAttached(); smoothMove(6, 0); }
  else if (cmd.indexOf('=') > 0)  { ensureAttached(); manualCommand(cmd); }
  else                            { wsSend("ERR:UNKNOWN:" + cmd); }
}

/* ═══════════════════════════════════════════════════════════════
   Line follower — v1.1

   TWO PHASES distinguished by the `returningHome` flag:

   OUTBOUND (returningHome == false):
     • Bot drives forward
     • Stop 1 (home base) is skipped on first encounter because
       stopCount will be 1 but no target is ever 1 in a valid
       shelf command (shelves are 2, 3, …)
     • On reaching a target stop → run arm, then set returningHome
       and start reversing

   RETURN (returningHome == true):
     • Bot drives backward
     • Steering is mirrored: left/right sensor roles swap so the
       bot tracks the line correctly while moving in reverse
     • Any all-black (stop marker) hit = home base → deliverAtHome()
   ═══════════════════════════════════════════════════════════════ */
void lineFollow() {
  int L = digitalRead(IR_LEFT);
  int C = digitalRead(IR_CENTER);
  int R = digitalRead(IR_RIGHT);

  Serial.printf("IR L:%d C:%d R:%d  count:%d  returning:%d  nextTarget:%d\n",
                L, C, R, stopCount, returningHome,
                (stopTargetIndex < stopTargetCount)
                  ? stopTargets[stopTargetIndex] : -1);

  /* ── All sensors on black → stop marker ── */
  if (L == 0 && C == 0 && R == 0) {
    if (!stopDetected) {
      stopDetected = true;
      stopMotors();
      delay(200);

      /* ── RETURN leg: any stop marker = home base ── */
      if (returningHome) {
        Serial.println("[HOME] Reached home base — delivering book");
        wsSend("STOP:HOME");
        deliverAtHome();   // releases gripper + sends DELIVERED + resets
        return;
      }

      /* ── OUTBOUND leg ── */
      stopCount++;
      Serial.printf("[STOP] Marker #%d\n", stopCount);
      wsSend("STOP:" + String(stopCount));

      if (stopTargetIndex < stopTargetCount &&
          stopCount == stopTargets[stopTargetIndex]) {

        int currentStopNum    = stopTargets[stopTargetIndex];
        lastDeliveredStop     = currentStopNum;
        stopTargetIndex++;

        /* Run arm sequence at this shelf */
        runArmSequence(currentStopNum);

        /* After arm work — start return journey */
        returningHome = true;
        wsSend("RETURNING");
        Serial.println("[RETURN] All shelf work done — reversing to home");
        delay(400);
        backward();   // sets motorsRunning = true
      }
      /* else: non-target stop on outbound — resume forward */
    }
    return;
  }

  /* ── Off stop marker — clear debounce ── */
  stopDetected = false;

  /* ── Steering ── */
  if (!returningHome) {
    /* OUTBOUND: normal line follow */
    if      (L == 1 && C == 0 && R == 1) forward();
    else if (L == 0 && C == 1 && R == 1) turnLeft();
    else if (L == 1 && C == 1 && R == 0) turnRight();
    else                                  forward();
  } else {
    /* RETURN: reversing — left/right roles are mirrored */
    if      (L == 1 && C == 0 && R == 1) backward();
    else if (L == 1 && C == 1 && R == 0) turnLeft();   // mirrored
    else if (L == 0 && C == 1 && R == 1) turnRight();  // mirrored
    else                                  backward();
  }
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
        if (executing || returningHome) {
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
      if (cmd == "S") {
        handleCommand(cmd);
      } else if (executing || returningHome) {
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
  Serial.println(F("║  LIBA 1.1  — Booting...     ║"));
  Serial.println(F("╚══════════════════════════════╝"));

  /* ── Motor driver ── */
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  ledcSetup(LEDC_CH_A, LEDC_FREQ, LEDC_RES);
  ledcSetup(LEDC_CH_B, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(ENA, LEDC_CH_A);
  ledcAttachPin(ENB, LEDC_CH_B);
  motorPWM(0, 0);

  /* ── IR sensors ── */
  pinMode(IR_LEFT,   INPUT);
  pinMode(IR_CENTER, INPUT);
  pinMode(IR_RIGHT,  INPUT);

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
   ═══════════════════════════════════════════════════════════════ */
void loop() {
  wsServer.loop();
  if (executing || returningHome) {
    lineFollow();
  }
}
