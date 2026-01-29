#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <time.h>

// WiFi credentials for NTP sync
const char* ssid = "YOUR_WIFI_SSID";         // Change this!
const char* password = "YOUR_WIFI_PASSWORD"; // Change this!

// NTP settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;      // Adjust for timezone (e.g., 3600 for GMT+1)
const int daylightOffset_sec = 0;  // Adjust for daylight saving

typedef struct sensor_data {
  uint8_t sensorID;
  float temperature;
  float humidity;
  int airQuality;
} sensor_data;

sensor_data receivedData;

// Get formatted timestamp string
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not set";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void onDataReceive(const uint8_t *mac, const uint8_t *data, int len) {
  memcpy(&receivedData, data, sizeof(receivedData));
  
  String timestamp = getTimestamp();

  Serial.printf(
    "[%s] ID:%d | T:%.2f | H:%.2f | AQ:%d\n",
    timestamp.c_str(),
    receivedData.sensorID,
    receivedData.temperature,
    receivedData.humidity,
    receivedData.airQuality
  );
}

// Sync time with NTP server
void syncTimeWithNTP() {
  Serial.println("Connecting to WiFi for NTP sync...");
  WiFi.begin(ssid, password);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected");
    
    // Configure and sync time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    Serial.print("Syncing time");
    struct tm timeinfo;
    timeout = 0;
    while (!getLocalTime(&timeinfo) && timeout < 10) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    
    if (getLocalTime(&timeinfo)) {
      Serial.println("\n✅ Time synced: " + getTimestamp());
    } else {
      Serial.println("\n❌ Time sync failed");
    }
  } else {
    Serial.println("\n❌ WiFi connection failed");
  }
  
  // Disconnect WiFi, prepare for ESP-NOW
  WiFi.disconnect();
  delay(100);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Control Hub ===");

  // Sync time first (before ESP-NOW)
  syncTimeWithNTP();

  WiFi.mode(WIFI_STA);
  
  // Set WiFi channel to 1 (required for ESP-NOW)
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  Serial.print("ESP32 MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataReceive);
  Serial.println("✅ ESP-NOW Receiver Ready\n");
}

void loop() {
  delay(10);
}
