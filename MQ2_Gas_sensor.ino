#define AO_pin 4
void setup() {
  // put your setup code here, to run once:
Serial.begin(9600);
pinMode(AO_pin, INPUT);
// analogSetAttenuation(ADC_11db);
  Serial.println("Warming up the MQ2 sensor");
  delay(20000);  // wait for the MQ2 to warm up
}

void loop() {
  int gasValue = analogRead(AO_pin);

  Serial.print("MQ2 sensor AO value: ");
  Serial.println(gasValue);
  delay(1000);
}