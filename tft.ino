#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// =====================================================
// ESP32-S3 DevKitC-1 N16R8
// 2.8" TFT SPI 240x320 V1.2
// ILI9341
// =====================================================

#define TFT_MOSI 11
#define TFT_MISO 12
#define TFT_SCK  13
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  8

// Use ESP32-S3 hardware SPI
SPIClass spi(FSPI);

// ILI9341 display
Adafruit_ILI9341 tft(&spi, TFT_DC, TFT_CS, TFT_RST);

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println("   ESP32-S3 ILI9341 TFT TEST");
  Serial.println("====================================");

  // CS must start HIGH
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  // Start SPI with our custom pins
  spi.begin(
    TFT_SCK,
    TFT_MISO,
    TFT_MOSI,
    TFT_CS
  );

  Serial.println("SPI started");

  // Initialize TFT at normal speed
  tft.begin();

  Serial.println("ILI9341 initialized");

  // Portrait
  tft.setRotation(0);

  // -----------------------------------------
  // RED TEST
  // -----------------------------------------
  Serial.println("RED TEST");
  tft.fillScreen(ILI9341_RED);
  delay(2000);

  // -----------------------------------------
  // GREEN TEST
  // -----------------------------------------
  Serial.println("GREEN TEST");
  tft.fillScreen(ILI9341_GREEN);
  delay(2000);

  // -----------------------------------------
  // BLUE TEST
  // -----------------------------------------
  Serial.println("BLUE TEST");
  tft.fillScreen(ILI9341_BLUE);
  delay(2000);

  // -----------------------------------------
  // WHITE TEST
  // -----------------------------------------
  Serial.println("WHITE TEST");
  tft.fillScreen(ILI9341_WHITE);
  delay(2000);

  // -----------------------------------------
  // BLACK + TEXT
  // -----------------------------------------
  Serial.println("TEXT TEST");

  tft.fillScreen(ILI9341_BLACK);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);

  tft.setCursor(20, 30);
  tft.println("TFT TEST");

  tft.setTextSize(2);

  tft.setCursor(20, 80);
  tft.println("ESP32-S3");

  tft.setCursor(20, 115);
  tft.println("ILI9341");

  tft.setTextSize(1);

  tft.setCursor(20, 165);
  tft.println("MOSI : GPIO 11");

  tft.setCursor(20, 180);
  tft.println("MISO : GPIO 12");

  tft.setCursor(20, 195);
  tft.println("SCK  : GPIO 13");

  tft.setCursor(20, 210);
  tft.println("CS   : GPIO 10");

  tft.setCursor(20, 225);
  tft.println("DC   : GPIO 9");

  tft.setCursor(20, 240);
  tft.println("RST  : GPIO 8");

  // Green PASS box
  tft.drawRect(20, 265, 200, 40, ILI9341_GREEN);

  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);

  tft.setCursor(55, 278);
  tft.println("TEST PASS");

  Serial.println();
  Serial.println("====================================");
  Serial.println("       TFT TEST COMPLETE");
  Serial.println("====================================");
}

void loop() {
}
