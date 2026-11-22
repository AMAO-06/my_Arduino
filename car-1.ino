// 4WD ESP32 Manual Control Robot Car
// Motor driver: L298N
// Compatible with ESP32 Arduino Core v3.x+

#define IN1 14
#define IN2 27
#define IN3 26
#define IN4 25
#define ENA 32
#define ENB 33

#define LEDC_FREQ  1000
#define LEDC_RES   8       // 8-bit: 0–255

char command;
int motorSpeed = 1;

void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // New API: ledcAttach(pin, frequency, resolution)
  ledcAttach(ENA, LEDC_FREQ, LEDC_RES);
  ledcAttach(ENB, LEDC_FREQ, LEDC_RES);

  stopCar();
}

// ----- SPEED HELPER -----

void setSpeed(int spd) {
  ledcWrite(ENA, spd);   // Write directly to pin, not channel
  ledcWrite(ENB, spd);
}

// ----- MOVEMENT FUNCTIONS -----

void forward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setSpeed(motorSpeed);
}

void backward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  setSpeed(motorSpeed);
}

void leftTurn() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setSpeed(motorSpeed);
}

void rightTurn() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  setSpeed(motorSpeed);
}

void stopCar() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  setSpeed(0);
}

// ----- MAIN LOOP -----

void loop() {
  if (Serial.available()) {
    command = Serial.read();
    Serial.println(command);

    switch (command) {
      case 'F': forward();   break;
      case 'B': backward();  break;
      case 'L': leftTurn();  break;
      case 'R': rightTurn(); break;
      case 'S': stopCar();   break;

      case '1': motorSpeed = 1;  Serial.println("Speed: 10%"); break;
      case '2': motorSpeed = 56;  Serial.println("Speed: 20%"); break;
      case '3': motorSpeed = 85;  Serial.println("Speed: 30%"); break;
      case '4': motorSpeed = 113; Serial.println("Speed: 40%"); break;
      case '5': motorSpeed = 140; Serial.println("Speed: 50%"); break;
      case '6': motorSpeed = 160; Serial.println("Speed: 60%"); break;
      case '7': motorSpeed = 180; Serial.println("Speed: 70%"); break;
      case '8': motorSpeed = 200; Serial.println("Speed: 80%"); break;
      case '9': motorSpeed = 230; Serial.println("Speed: 90%"); break;
      case '+': motorSpeed = 255; Serial.println("Speed: 100%"); break;
    }
  }
}