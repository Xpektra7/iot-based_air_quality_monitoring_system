#include <Arduino.h>
#include <DHT.h>

// ESP8266 Pin Mapping
// DHT22 Data Pin: GPIO4 (D2 physical pin)
#define DHTPIN 4        // GPIO4 - Physical Pin D2 on ESP8266
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// LED Status Indicators - ESP8266 GPIO Pins
// const int greenPin = 5;   // GPIO5 - Physical Pin D1 on ESP8266
// const int yellowPin = 12; // GPIO12 - Physical Pin D6 on ESP8266
// const int redPin = 13;    // GPIO13 - Physical Pin D7 on ESP8266

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nESP8266 Air Quality Monitoring System");
  
  dht.begin();
  // pinMode(greenPin, OUTPUT);
  // pinMode(yellowPin, OUTPUT);
  // pinMode(redPin, OUTPUT);
  
  // // Initialize all LEDs as OFF
  // digitalWrite(greenPin, LOW);
  // digitalWrite(yellowPin, LOW);
  // digitalWrite(redPin, LOW);
}

void loop() {
  // ESP8266 has only 1 ADC (A0 - Physical Pin A0)
  int gasValue = analogRead(A0);  // A0 - Physical Pin A0 on ESP8266
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  Serial.print("Aout: ");
  Serial.print(gasValue);
  Serial.print("\tTemp: ");
  Serial.print(temperature);
  Serial.print("\tHum: ");
  Serial.println(humidity);

  // if(gasValue > 3000 ){
  //   digitalWrite(redPin,HIGH);      // GPIO13 - D7
  //   digitalWrite(yellowPin,LOW);    // GPIO12 - D6
  //   digitalWrite(greenPin,LOW);     // GPIO5 - D1
  // }
  // else if(gasValue > 2000){
  //   digitalWrite(redPin,LOW);       // GPIO13 - D7
  //   digitalWrite(yellowPin,HIGH);   // GPIO12 - D6
  //   digitalWrite(greenPin,LOW);     // GPIO5 - D1
  // } else {
  //   digitalWrite(redPin,LOW);       // GPIO13 - D7
  //   digitalWrite(yellowPin,LOW);    // GPIO12 - D6
  //   digitalWrite(greenPin,HIGH);    // GPIO5 - D1
  // }

  delay(500);
}
