/*
  PARADE ROBOT (ESP32) - v2 (low-speed fix)
  Sequence: attention -> QUICK MARCH -> SLOW MARCH -> halt -> SALUTE -> stand easy

  Library: ESP32Servo  (Arduino IDE > Library Manager > "ESP32Servo")

  WIRING
  - Servos need their own 5-6V supply (NOT the ESP32 pin). Join all grounds together.
  - Motors: L298N (or similar) driver. ENA/ENB take PWM from the ESP32.
*/

#include <ESP32Servo.h>

// ===================== PINS (change to match your wiring) =====================
#define PIN_L_SHOULDER 13
#define PIN_L_ELBOW    12
#define PIN_R_SHOULDER 14
#define PIN_R_ELBOW    27
#define PIN_TORSO      26   // optional torso tilt servo

// Motor driver
#define ENA 25   // left motor speed (PWM)
#define IN1 33
#define IN2 32
#define ENB 23   // right motor speed (PWM)
#define IN3 19
#define IN4 18

// ===================== OPTIONS =====================
const bool HAS_TORSO_SERVO = false;   // true if you add a servo to tilt the body
const bool MIRROR_RIGHT    = true;    // right-side servos are mounted mirrored
const bool FLIP_MOTOR_L    = false;   // set true if left wheel spins backwards
const bool FLIP_MOTOR_R    = false;   // set true if right wheel spins backwards

// =====================================================================
//  >>>>>>>>>>>>>>>>>  SPEED SETTINGS - CHANGE SPEED HERE  <<<<<<<<<<<<<<<<<
// =====================================================================
// Speed is a PERCENT (0-100), NOT a raw PWM number.
// 0 = stopped, 1 = slowest the wheels can turn, 100 = full speed.

// 1) Calibrate this FIRST: the lowest PWM (0-255) at which YOUR wheels just turn
//    while the robot is on the floor. Raise it if the wheels hum/stall at low speed.
const int MIN_PWM = 90;

// 2) How hard the motors push while they are ON
const int QUICK_SPEED_PCT = 25;     // quick march speed (try 30-70)
const int SLOW_SPEED_PCT  = 10;      // slow march speed  (try 1-10 for very slow)

// 3) For how much of each step the wheels are powered (the rest of the step they coast/stop).
//    Lower = even slower, gentler movement. 100 = powered the whole step.
const int QUICK_DRIVE_PCT = 25;    // quick march
const int SLOW_DRIVE_PCT  = 10;     // slow march (try 20-50 for very slow)

// 4) Body sway when there is no torso servo (as % of current speed). 0 = no sway.
const int WHEEL_SWAY_PCT  = 25;

// 5) Step length (beat). Bigger number = slower cadence.
const int QUICK_STEPS   = 10;
const int QUICK_STEP_MS = 450;
const int SLOW_STEPS    = 6;
const int SLOW_STEP_MS  = 1300;

const bool SLOW_MARK_TIME_IN_PLACE = false;  // true = arms march but wheels stay still
// =====================================================================

// ===================== POSES (degrees) - CALIBRATE THESE =====================
// Shoulder: 90 = arm hanging straight down, higher = swings forward (flip if opposite)
const int SHOULDER_REST   = 90;
const int SHOULDER_SWING  = 35;    // +/- swing from rest during march
const int ELBOW_STRAIGHT  = 90;    // arm straight
const int ELBOW_BENT      = 45;    // forearm bent up (marching arm)

const int TORSO_CENTER    = 90;
const int TORSO_TILT      = 8;     // body lean amount (servo degrees)

// Salute pose (right arm): upper arm raised, forearm bent so the hand reaches the brow
const int SALUTE_SHOULDER = 150;
const int SALUTE_ELBOW    = 20;

// ===================== GLOBALS =====================
Servo lShoulder, lElbow, rShoulder, rElbow, torso;
int curLS = SHOULDER_REST, curLE = ELBOW_STRAIGHT;
int curRS = SHOULDER_REST, curRE = ELBOW_STRAIGHT;
int curT  = TORSO_CENTER;

// ===================== MOTOR HELPERS =====================
void setMotor(int en, int inA, int inB, int speed, bool flip) {
  bool fwd = speed >= 0;
  if (flip) fwd = !fwd;
  digitalWrite(inA, fwd ? HIGH : LOW);
  digitalWrite(inB, fwd ? LOW : HIGH);
  analogWrite(en, constrain(abs(speed), 0, 255));
}

void drive(int left, int right) {
  setMotor(ENA, IN1, IN2, left,  FLIP_MOTOR_L);
  setMotor(ENB, IN3, IN4, right, FLIP_MOTOR_R);
}

void stopDrive() { drive(0, 0); }

// Convert speed percent (0-100) into a PWM value that is always above the dead zone.
int pctToPwm(int pct) {
  if (pct <= 0) return 0;
  return map(constrain(pct, 1, 100), 1, 100, MIN_PWM, 255);
}

// ===================== SERVO HELPERS =====================
int mirrorR(int a) { return MIRROR_RIGHT ? 180 - a : a; }

void writeAll(int ls, int le, int rs, int re, int t) {
  lShoulder.write(ls);
  lElbow.write(le);
  rShoulder.write(mirrorR(rs));
  rElbow.write(mirrorR(re));
  if (HAS_TORSO_SERVO) torso.write(t);
}

// Smoothly move every joint to a target pose over 'ms' milliseconds.
// leftPwm/rightPwm keep the wheels running; drivePct = how much of the move they stay powered.
void moveTo(int ls, int le, int rs, int re, int t, int ms,
            int leftPwm = 0, int rightPwm = 0, int drivePct = 100) {
  const int dt = 15;
  int steps = max(1, ms / dt);
  int cutStep = steps * constrain(drivePct, 0, 100) / 100;   // wheels switch off after this update
  bool wheelsOn = true;
  int sLS = curLS, sLE = curLE, sRS = curRS, sRE = curRE, sT = curT;

  if (cutStep > 0) drive(leftPwm, rightPwm); else { stopDrive(); wheelsOn = false; }

  for (int i = 1; i <= steps; i++) {
    if (wheelsOn && i > cutStep) { stopDrive(); wheelsOn = false; }
    float p = (float)i / steps;
    p = p * p * (3 - 2 * p);                 // ease in/out
    writeAll(sLS + (ls - sLS) * p, sLE + (le - sLE) * p,
             sRS + (rs - sRS) * p, sRE + (re - sRE) * p,
             sT  + (t  - sT)  * p);
    delay(dt);
  }
  curLS = ls; curLE = le; curRS = rs; curRE = re; curT = t;
}

// ===================== POSES =====================
void attention(int ms = 800) {
  stopDrive();
  moveTo(SHOULDER_REST, ELBOW_STRAIGHT, SHOULDER_REST, ELBOW_STRAIGHT, TORSO_CENTER, ms);
}

// One march step. leftForward = true -> left arm swings forward, body tilts RIGHT.
void marchStep(bool leftForward, int stepMs, int speedPct, int drivePct, bool inPlace) {
  int ls, le, rs, re, tilt;
  if (leftForward) {
    ls = SHOULDER_REST + SHOULDER_SWING;  le = ELBOW_BENT;
    rs = SHOULDER_REST - SHOULDER_SWING;  re = ELBOW_STRAIGHT;
    tilt = TORSO_CENTER + TORSO_TILT;     // lean right
  } else {
    ls = SHOULDER_REST - SHOULDER_SWING;  le = ELBOW_STRAIGHT;
    rs = SHOULDER_REST + SHOULDER_SWING;  re = ELBOW_BENT;
    tilt = TORSO_CENTER - TORSO_TILT;     // lean left
  }

  int base = pctToPwm(speedPct);
  int lp = inPlace ? 0 : base;
  int rp = inPlace ? 0 : base;

  // No torso servo? Fake the sway by biasing the wheels (scaled to the speed).
  if (!HAS_TORSO_SERVO && !inPlace) {
    int bias = base * WHEEL_SWAY_PCT / 100;
    if (!leftForward) bias = -bias;
    lp = constrain(base + bias, 0, 255);
    rp = constrain(base - bias, 0, 255);
  }

  // Move for ~80% of the beat, then a brief plant/stop so each step feels crisp.
  moveTo(ls, le, rs, re, tilt, (int)(stepMs * 0.8), lp, rp, drivePct);
  stopDrive();
  delay((int)(stepMs * 0.2));
}

void march(int steps, int stepMs, int speedPct, int drivePct, bool inPlace) {
  bool leftForward = true;
  for (int i = 0; i < steps; i++) {
    marchStep(leftForward, stepMs, speedPct, drivePct, inPlace);
    leftForward = !leftForward;
  }
  stopDrive();
}

void salute() {
  stopDrive();
  attention(700);
  delay(500);
  // Sharp raise of the right hand to the brow
  moveTo(SHOULDER_REST, ELBOW_STRAIGHT, SALUTE_SHOULDER, SALUTE_ELBOW, TORSO_CENTER, 500);
  delay(2500);                                   // hold the salute
  // Snap the arm back down
  moveTo(SHOULDER_REST, ELBOW_STRAIGHT, SHOULDER_REST, ELBOW_STRAIGHT, TORSO_CENTER, 250);
}

// ===================== SETUP / LOOP =====================
void setup() {
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  analogWriteFrequency(ENA, 500);   // gentler on motors at low duty than the default 1 kHz
  analogWriteFrequency(ENB, 500);
#endif
  stopDrive();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  lShoulder.setPeriodHertz(50); lElbow.setPeriodHertz(50);
  rShoulder.setPeriodHertz(50); rElbow.setPeriodHertz(50);
  lShoulder.attach(PIN_L_SHOULDER, 500, 2400);
  lElbow.attach(PIN_L_ELBOW, 500, 2400);
  rShoulder.attach(PIN_R_SHOULDER, 500, 2400);
  rElbow.attach(PIN_R_ELBOW, 500, 2400);
  if (HAS_TORSO_SERVO) { torso.setPeriodHertz(50); torso.attach(PIN_TORSO, 500, 2400); }

  writeAll(curLS, curLE, curRS, curRE, curT);
  delay(2000);                       // time to place the robot and step back
}

void loop() {
  attention();
  delay(1000);

  // 1) QUICK MARCH
  march(QUICK_STEPS, QUICK_STEP_MS, QUICK_SPEED_PCT, QUICK_DRIVE_PCT, false);

  // 2) SLOW TIME
  march(SLOW_STEPS, SLOW_STEP_MS, SLOW_SPEED_PCT, SLOW_DRIVE_PCT, SLOW_MARK_TIME_IN_PLACE);

  // 3) HALT + SALUTE
  salute();

  // Stand easy, then run again after a pause (remove the delay/loop if you want one run only)
  delay(8000);
}