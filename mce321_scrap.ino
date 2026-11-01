#include <ESP32Servo.h>

// ---------------- TCS3200 PINS ----------------
#define S0 5
#define S1 18
#define S2 19
#define S3 21
#define sensorOut 22

int red = 0;
int green = 0;
int blue = 0;

// ---------------- SERVO SETUP ----------------
Servo base;
Servo shoulder;
Servo elbow;
Servo gripper;

// Servo pins
#define BASE_PIN     14
#define SHOULDER_PIN 27
#define ELBOW_PIN    26
#define GRIPPER_PIN  25

// ---------------- ARM POSITIONS ----------------
void homePosition() {
  base.write(90);
  shoulder.write(110);
  elbow.write(70);
  gripper.write(90);
  delay(500);
}

void pickObject() {
  // Move above object
  shoulder.write(120);
  elbow.write(60);
  delay(500);

  // Move down
  shoulder.write(150);
  elbow.write(40);
  delay(500);

  // Close gripper
  gripper.write(30);
  delay(500);

  // Lift object
  shoulder.write(110);
  elbow.write(80);
  delay(500);
}

void dropRed() {
  base.write(40);
  delay(500);
  gripper.write(100);
  delay(300);
}

void dropGreen() {
  base.write(135);
  delay(500);
  gripper.write(100);
  delay(300);
}

void dropBlue() {
  base.write(170);
  delay(500);
  gripper.write(100);
  delay(300);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(9600);

  // Color sensor pins
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(sensorOut, INPUT);

  // Frequency scaling 20%
  digitalWrite(S0, HIGH);
  digitalWrite(S1, LOW);

  // Attach servos
  base.attach(BASE_PIN);
  shoulder.attach(SHOULDER_PIN);
  elbow.attach(ELBOW_PIN);
  gripper.attach(GRIPPER_PIN);

  homePosition();
}

// ---------------- LOOP ----------------
void loop() {

  // Read RED
  digitalWrite(S2, LOW);
  digitalWrite(S3, LOW);
  red = pulseIn(sensorOut, LOW);

  // Read GREEN
  digitalWrite(S2, HIGH);
  digitalWrite(S3, HIGH);
  green = pulseIn(sensorOut, LOW);

  // Read BLUE
  digitalWrite(S2, LOW);
  digitalWrite(S3, HIGH);
  blue = pulseIn(sensorOut, LOW);

  // ---------- NO OBJECT FIX ----------
  if (abs(red - green) < 50 && abs(red - blue) < 50 && abs(green - blue) < 50) {
    Serial.println("NO OBJECT DETECTED");
    delay(1000);
    return;
  }

  // ---------- COLOR SORTING ----------
  if (red < green && red < blue) {
    Serial.println("RED DETECTED");
    pickObject();
    dropRed();
    homePosition();
  }

  else if (green < red && green < blue) {
    Serial.println("GREEN DETECTED");
    pickObject();
    dropGreen();
    homePosition();
  }

  else if (blue < red && blue < green) {
    Serial.println("BLUE DETECTED");
    pickObject();
    dropBlue();
    homePosition();
  }

  delay(1000);
}