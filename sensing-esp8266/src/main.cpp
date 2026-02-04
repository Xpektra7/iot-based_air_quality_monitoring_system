// Sensing subsystem - ESP8266 (Sender/Master)
// Reads sensors and sends data to ESP32 via ESP-NOW

#include <Arduino.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
extern "C" {
  #include <user_interface.h>
}


DHT dht(4, DHT11);

// ESP32 MAC Address (receiver)
// ⚠️ IMPORTANT: Update this to match YOUR ESP32's MAC address!
// Check ESP32 serial output for: "ESP32 MAC: XX:XX:XX:XX:XX:XX"
uint8_t esp32Address[] = {0x00, 0x4B, 0x12, 0x38, 0xB0, 0xE4};

// Data structure - MUST match ESP32 receiver exactly!
// Packed struct ensures consistent memory layout across ESP8266/ESP32
typedef struct __attribute__((packed)) sensor_data {
  uint8_t sensorID;      // Which sensor is this (change for each ESP8266)
  float temperature;     
  float humidity;        
  int airQuality;        
} sensor_data;

sensor_data myData;

// Define which sensor this is (change to 2, 3, etc. for other ESP8266s)
#define THIS_SENSOR_ID 1

// Send interval (milliseconds)
#define SEND_INTERVAL 2000


// ===== CALLBACK FUNCTION =====
// Runs after data is sent - tells you if it worked
void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Send Status: ");
  if (sendStatus == 0) {
    Serial.println("Success");
  } else {
    Serial.println("❌ Failed");
  }
}


void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== ESP8266 Sensor Node #" + String(THIS_SENSOR_ID) + " ===");
  
  // Initialize sensors
  dht.begin();
  
  // Set WiFi to Station mode (required for ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Force WiFi channel 1 for ESP-NOW communication
  wifi_set_channel(1);
  
  // Print this ESP8266's MAC address
  wifi_promiscuous_enable(1);
  wifi_set_channel(1);
  wifi_promiscuous_enable(0);
  
  // Print this ESP8266's MAC address
  Serial.print("ESP8266 MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  // Initialize ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("❌ ESP-NOW init failed!");
    return;
  }
  Serial.println("✅ ESP-NOW initialized");
  
  // Set role as controller (sender)
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  
  // Register send callback
  esp_now_register_send_cb(onDataSent);
  
  // Add ESP32 as peer (the receiver)
  esp_now_add_peer(esp32Address, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  
  Serial.println("📡 Sending data to ESP32...\n");
}


void loop() {
  // ===== READ SENSORS =====
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int gasValue = analogRead(A0);
  
  // Check if sensor readings are valid
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT sensor read failed!");
    temperature = 0.0;
    humidity = 0.0;
  }
  
  // ===== PREPARE DATA =====
  myData.sensorID = THIS_SENSOR_ID;
  myData.temperature = temperature;
  myData.humidity = humidity;
  myData.airQuality = gasValue;
  
  // Print what we're sending
  Serial.printf("Sensor #%d | Temp: %.1f°C | Hum: %.1f%% | AQ: %d\n",
                myData.sensorID, myData.temperature, myData.humidity, myData.airQuality);
  
  // ===== SEND DATA TO ESP32 =====
  esp_now_send(esp32Address, (uint8_t *)&myData, sizeof(myData));
  
  // Wait before next reading
  delay(SEND_INTERVAL);
}
