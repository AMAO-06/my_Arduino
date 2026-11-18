#include <HX711.h>

const int LOADCELL_DOUT_PIN = 16;
const int LOADCELL_SCK_PIN = 4;

HX711 scale;

// FINAL CALIBRATION VALUE
float calibration_factor = 92.0;

void setup() {
  Serial.begin(115200);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  Serial.println("Taring... remove all weight");
  delay(5000);

  scale.tare();
  Serial.println("Tare done");
}

void loop() {
  if (scale.is_ready()) {

    scale.set_scale(calibration_factor);

    float weight = scale.get_units(10);

    Serial.print("Weight: ");
    Serial.print(weight, 2);
    Serial.println(" kg");

  } else {
    Serial.println("HX711 not ready");
  }

  delay(300);
}