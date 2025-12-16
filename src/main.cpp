#include <Arduino.h>
#include <DHT.h>

#define DHTPIN 4
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  dht.begin();
}

void loop() {
  int value13 = analogRead(13);
  int value14 = analogRead(14);
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  Serial.print("Aout: ");
  Serial.print(value13);
  Serial.print("\tDout: ");
  Serial.print(value14);
  Serial.print("\tTemp: ");
  Serial.print(temperature);
  Serial.print("\tHum: ");
  Serial.println(humidity);

  delay(500);
}
