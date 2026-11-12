/*
 * SortingController.ino
 * -----------------------
 * Runs on a plain ESP32 dev board (NOT the ESP32-CAM). Receives a
 * classification command over UART from the ESP32-CAM, drives the sorting
 * servo, runs the conveyor belt via an L298N, and does the MQ135
 * gas-sensor ripeness check for fruit the camera couldn't confidently
 * classify.
 *
 * Wiring
 * ------
 * UART link to ESP32-CAM (cross-connected, share a common GND):
 *   Sorting ESP32  RX2 (GPIO16)  <-  ESP32-CAM TX (GPIO14)
 *   Sorting ESP32  TX2 (GPIO17)  ->  ESP32-CAM RX (GPIO15)
 *
 * Servo:
 *   Signal -> GPIO18 (any PWM-capable pin works; change SERVO_PIN below)
 *   Power servo from a separate 5V supply if it's more than a small
 *   micro-servo -- don't pull it from the ESP32's 3.3V/5V pin directly.
 *
 * L298N motor driver (conveyor DC motor):
 *   IN1 -> GPIO26        (direction)
 *   IN2 -> GPIO27        (direction)
 *   ENA -> GPIO25 (PWM)  (speed)
 *   Motor power (12V/battery) goes to the L298N's own supply input, not
 *   the ESP32.
 *
 * MQ135 gas sensor:
 *   AOUT -> GPIO34 (ADC1, input-only pin, fine for analog read)
 *   VCC  -> 5V, GND -> GND
 *
 * Protocol from ESP32-CAM (one line per message, newline-terminated):
 *   "CMD:R" = ripe or old   -> servo to RIPE bin
 *   "CMD:D" = damaged       -> servo to DAMAGED bin
 *   "CMD:U" = unripe/unsure -> servo to gas-sensor bin, then read MQ135
 *   "LOG:..."               -> a line from the CAM's own Serial output,
 *                              just echoed here so both boards' activity
 *                              shows up in this board's serial monitor.
 */

#include <Arduino.h>
#include <ESP32Servo.h>

// ---------- UART link to ESP32-CAM ----------
#define LINK_UART_RX_PIN   16
#define LINK_UART_TX_PIN   17
#define LINK_UART_BAUD    115200

#define CMD_PREFIX         "CMD:"
#define LOG_PREFIX         "LOG:"

#define CMD_RIPE           'R'
#define CMD_DAMAGED        'D'
#define CMD_UNRIPE         'U'

// ---------- Servo ----------
#define SERVO_PIN          18
#define SERVO_ANGLE_RIPE     0
#define SERVO_ANGLE_DAMAGED 180
#define SERVO_ANGLE_UNRIPE  90   // also where the MQ135 is mounted

// ---------- L298N conveyor motor ----------
#define MOTOR_IN1          26
#define MOTOR_IN2          27
#define MOTOR_ENA          25
#define MOTOR_PWM_FREQ   1000
#define MOTOR_PWM_RES       8    // 8-bit -> 0-255
#define CONVEYOR_SPEED    200    // 0-255, tune to your belt/motor

// ---------- MQ135 gas sensor ----------
#define GAS_SENSOR_PIN     34
#define MQ135_WARMUP_MS  20000   // let the sensor heater stabilize before use
#define GAS_BASELINE_SAMPLES  20
#define GAS_BASELINE_SAMPLE_DELAY_MS 100
#define GAS_READ_SETTLE_MS  3000 // let gas build up around the sensor once
                                  // the fruit is parked at the 90 deg station
#define GAS_READ_SAMPLES     10
// How far above the clean-air baseline a reading has to be before we call
// the fruit "ripe enough" and route it to the ripe bin. This is a relative
// threshold (raw ADC counts above baseline), not a calibrated ppm value --
// start here and tune it against fruit you've manually graded.
#define GAS_RIPENESS_DELTA   80

static Servo sortServo;
static HardwareSerial linkSerial(2);
static int gasBaseline = 0;
static String linkLineBuf;

void driveServo(int angle);
void startConveyor();
void stopConveyor();
int readGasAverage(int samples, int delayMsBetween);
void calibrateGasBaseline();
void handleCommand(char cmd);
void handleLinkLine(const String &line);

void setup() {
  Serial.begin(115200);
  Serial.println("Sorting controller starting...");

  // UART link to the ESP32-CAM
  linkSerial.begin(LINK_UART_BAUD, SERIAL_8N1, LINK_UART_RX_PIN, LINK_UART_TX_PIN);

  // Servo
  sortServo.setPeriodHertz(50);
  sortServo.attach(SERVO_PIN, 500, 2400);
  driveServo(SERVO_ANGLE_UNRIPE);  // start neutral

  // L298N
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  ledcAttach(MOTOR_ENA, MOTOR_PWM_FREQ, MOTOR_PWM_RES);

  // MQ135 needs its heater to stabilize before readings are meaningful.
  // For real ripeness decisions the datasheet recommends a much longer
  // burn-in (24-48h) the first time the sensor is ever powered; this
  // shorter delay is just the per-boot warm-up.
  Serial.printf("Warming up MQ135 (%d ms)...\n", MQ135_WARMUP_MS);
  delay(MQ135_WARMUP_MS);
  calibrateGasBaseline();

  startConveyor();
  Serial.println("Sorting controller ready");
}

void loop() {
  while (linkSerial.available()) {
    char c = linkSerial.read();
    if (c == '\n') {
      handleLinkLine(linkLineBuf);
      linkLineBuf = "";
    } else if (c != '\r') {
      linkLineBuf += c;
      // Safety valve: don't let a malformed/unterminated line grow forever.
      if (linkLineBuf.length() > 200) {
        linkLineBuf = "";
      }
    }
  }
}

/**
 * @brief Handle one newline-terminated line received from the ESP32-CAM.
 *        Either a "CMD:<letter>" sort command or a "LOG:<text>" line to
 *        echo to this board's own serial monitor.
 */
void handleLinkLine(const String &line) {
  if (line.startsWith(CMD_PREFIX)) {
    if (line.length() > strlen(CMD_PREFIX)) {
      char cmd = line.charAt(strlen(CMD_PREFIX));
      if (cmd == CMD_RIPE || cmd == CMD_DAMAGED || cmd == CMD_UNRIPE) {
        handleCommand(cmd);
      }
    }
  } else if (line.startsWith(LOG_PREFIX)) {
    Serial.print("[CAM] ");
    Serial.println(line.substring(strlen(LOG_PREFIX)));
  }
  // anything else (stray bytes, noise) is silently ignored
}

/**
 * @brief Act on a classification command from the ESP32-CAM.
 */
void handleCommand(char cmd) {
  if (cmd == CMD_RIPE) {
    Serial.println("CAM: ripe/old -> RIPE bin");
    driveServo(SERVO_ANGLE_RIPE);

  } else if (cmd == CMD_DAMAGED) {
    Serial.println("CAM: damaged -> DAMAGED bin");
    driveServo(SERVO_ANGLE_DAMAGED);

  } else if (cmd == CMD_UNRIPE) {
    Serial.println("CAM: unripe/unsure -> gas sensor station");
    driveServo(SERVO_ANGLE_UNRIPE);

    // Give the fruit a moment to sit at the sensor and let gas build up
    stopConveyor();
    delay(GAS_READ_SETTLE_MS);

    int reading = readGasAverage(GAS_READ_SAMPLES, 50);
    int delta = reading - gasBaseline;
    Serial.printf("Gas reading: %d (baseline %d, delta %d)\n", reading, gasBaseline, delta);

    if (delta > GAS_RIPENESS_DELTA) {
      Serial.println("-> Gas sensor says ripe enough, moving to RIPE bin");
      driveServo(SERVO_ANGLE_RIPE);
    } else {
      Serial.println("-> Still unripe, leaving fruit at the unripe station");
      // stays at SERVO_ANGLE_UNRIPE
    }

    startConveyor();
  }
}

void driveServo(int angle) {
  sortServo.write(angle);
}

void startConveyor() {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  ledcWrite(MOTOR_ENA, CONVEYOR_SPEED);
}

void stopConveyor() {
  ledcWrite(MOTOR_ENA, 0);
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
}

int readGasAverage(int samples, int delayMsBetween) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(GAS_SENSOR_PIN);
    delay(delayMsBetween);
  }
  return (int)(total / samples);
}

/**
 * @brief Average a batch of readings taken right after warm-up, with no
 *        fruit near the sensor, and use that as the "clean air" baseline
 *        every later reading gets compared against.
 */
void calibrateGasBaseline() {
  Serial.println("Calibrating gas sensor baseline (keep sensor clear)...");
  gasBaseline = readGasAverage(GAS_BASELINE_SAMPLES, GAS_BASELINE_SAMPLE_DELAY_MS);
  Serial.printf("Gas baseline: %d\n", gasBaseline);
}