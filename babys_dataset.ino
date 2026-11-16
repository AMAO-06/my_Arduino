#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;

// Replace these with your WiFi credentials
const char* ssid = "Redmi15C";
const char* password = "khalidi123";

// Your deployed Google Apps Script Web App URL (with the deployment ID)
String scriptURL = "https://script.google.com/macros/s/AKfycbymkvHZccfrCZs11f0k3dy4negB3l3KZbphDFicjUTUN9ZUVRaBZ-DGzdIouTq9Zslwog/exec";

String currentLabel = "big_kick";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Ready! Type 'label your_label' to change label.");
}

// Helper function to send HTTP GET and follow redirect once if needed
String sendDataWithRedirect(String url) {
  HTTPClient http;
  http.begin(url);
  http.addHeader("User-Agent", "ESP32");

  int httpCode = http.GET();
  Serial.println("HTTP Code: " + String(httpCode));

  if (httpCode == 302) {
    String redirectUrl = http.getLocation();
    Serial.println("Redirect to: " + redirectUrl);
    http.end();

    // Follow redirect once
    http.begin(redirectUrl);
    http.addHeader("User-Agent", "ESP32");
    httpCode = http.GET();
    Serial.println("HTTP Code after redirect: " + String(httpCode));
    http.end();

    return (httpCode == 200) ? "OK" : "ERROR";
  }

  http.end();
  return (httpCode == 200) ? "OK" : "ERROR";
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.startsWith("label ")) {
      currentLabel = input.substring(6);
      Serial.println("Label changed to: " + currentLabel);
    }
  }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  String url = scriptURL +
               "?ax=" + String(a.acceleration.x, 3) +
               "&ay=" + String(a.acceleration.y, 3) +
               "&az=" + String(a.acceleration.z, 3) +
               "&gx=" + String(g.gyro.x, 3) +
               "&gy=" + String(g.gyro.y, 3) +
               "&gz=" + String(g.gyro.z, 3) +
               "&label=" + currentLabel;

  Serial.println("Sending: " + url);

  String result = sendDataWithRedirect(url);
  Serial.println("Result: " + result);

  delay(50);
}
