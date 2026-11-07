#include <ESP32Servo.h>
const int mqPin = 34; // Analog pin connected to MQ-135 AO
Servo myservo;
void setup() {
  Serial.begin(115200); // Start serial communication
  pinMode(mqPin, INPUT);
  myservo.attach(14);
}

void loop() {
  int sensorValue = analogRead(mqPin); // Read the analog value (0 - 4095 on ESP32)
  Serial.print("MQ-135 Value: ");
  Serial.println(map(sensorValue, 0,1023, 0,100));
  
  delay(1000); // Wait 1 second between readings

  if (sensorValue <=50 ){
    myservo.write(90);
    delay(3000);
  }
  else{
    myservo.write(180);
  }
}
