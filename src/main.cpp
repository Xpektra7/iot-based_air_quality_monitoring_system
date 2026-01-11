#include <Arduino.h>
#include <DHT.h>

#define DHTPIN 4
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

const int greenPin = 16;
const int yellowPin = 15;
const int redPin = 12;

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(greenPin, OUTPUT);
  pinMode(yellowPin, OUTPUT);
  pinMode(redPin, OUTPUT);
}

void loop() {
  int gasValue = analogRead(13);
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  Serial.print("Aout: ");
  Serial.print(gasValue);
  Serial.print("\tTemp: ");
  Serial.print(temperature);
  Serial.print("\tHum: ");
  Serial.println(humidity);

  if(gasValue > 3000 ){
    digitalWrite(redPin,HIGH);
    digitalWrite(yellowPin,LOW);
    digitalWrite(greenPin,LOW);
  }
  else if(gasValue > 2000){
    digitalWrite(redPin,LOW);
    digitalWrite(yellowPin,HIGH);
    digitalWrite(greenPin,LOW);
  } else {
    digitalWrite(redPin,LOW);
    digitalWrite(yellowPin,LOW);
    digitalWrite(greenPin,HIGH);
  }

  delay(500);
}
