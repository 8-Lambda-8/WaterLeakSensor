#include <Arduino.h>


#define SENSE_PIN 4 //Für Touch: 2, 4, 12, 13, 14, 15, 27

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  Serial.println("EndSetup");
}


int touch_value = 100;

void loop() {
  // put your main code here, to run repeatedly:
  touch_value = touchRead(SENSE_PIN);
  Serial.println(touch_value);
}