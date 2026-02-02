#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <time.h>
#include <Firebase_ESP_Client.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Firebase helper includes
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// Load credentials from secrets file (not committed to git)
#include "secrets.h"

// NTP settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;      // Adjust for timezone (e.g., 3600 for GMT+1)
const int daylightOffset_sec = 0;  // Adjust for daylight saving

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Connection state
bool wifiConnected = false;
bool firebaseConnected = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 300000;  // Try reconnect every 5 minutes (was 30s)

// Offline cache file
const char* CACHE_FILE = "/offline_cache.json";
const int MAX_CACHE_ENTRIES = 100;

typedef struct sensor_data {
  uint8_t sensorID;
  float temperature;
  float humidity;
  int airQuality;
} sensor_data;

sensor_data receivedData;

// Forward declarations
void onDataReceive(const uint8_t *mac, const uint8_t *data, int len);

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

// Get Unix timestamp for Firebase
unsigned long getUnixTime() {
  time_t now;
  time(&now);
  return now;
}

// Initialize SPIFFS for offline storage
bool initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS mount failed");
    return false;
  }
  Serial.println("✅ SPIFFS mounted");
  return true;
}

// Save data to offline cache
void saveToOfflineCache(sensor_data &data, String timestamp) {
  File file = SPIFFS.open(CACHE_FILE, FILE_READ);
  JsonDocument doc;
  
  if (file) {
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
      doc.clear();
      doc["entries"] = doc.createNestedArray("entries");
    }
  } else {
    doc["entries"] = doc.createNestedArray("entries");
  }
  
  JsonArray entries = doc["entries"];
  
  // Limit cache size
  while (entries.size() >= MAX_CACHE_ENTRIES) {
    entries.remove(0);
  }
  
  // Add new entry
  JsonObject entry = entries.add<JsonObject>();
  entry["sensorID"] = data.sensorID;
  entry["temperature"] = data.temperature;
  entry["humidity"] = data.humidity;
  entry["airQuality"] = data.airQuality;
  entry["timestamp"] = timestamp;
  entry["unixTime"] = getUnixTime();
  
  // Save to file
  file = SPIFFS.open(CACHE_FILE, FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.printf("💾 Cached offline (total: %d entries)\n", entries.size());
  }
}

// Get cached entry count
int getCacheCount() {
  File file = SPIFFS.open(CACHE_FILE, FILE_READ);
  if (!file) return 0;
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) return 0;
  return doc["entries"].size();
}

// Upload cached data to Firebase
void syncOfflineCache() {
  File file = SPIFFS.open(CACHE_FILE, FILE_READ);
  if (!file) return;
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error || !doc.containsKey("entries")) return;
  
  JsonArray entries = doc["entries"];
  int count = entries.size();
  
  if (count == 0) return;
  
  Serial.printf("📤 Syncing %d cached entries to Firebase...\n", count);
  
  int successCount = 0;
  for (JsonVariant entry : entries) {
    String path = "/sensor_readings/" + String(entry["sensorID"].as<int>()) + "/" + 
                  String(entry["unixTime"].as<unsigned long>());
    
    FirebaseJson json;
    json.set("temperature", entry["temperature"].as<float>());
    json.set("humidity", entry["humidity"].as<float>());
    json.set("airQuality", entry["airQuality"].as<int>());
    json.set("timestamp", entry["timestamp"].as<String>());
    json.set("synced", true);
    
    if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
      successCount++;
    } else {
      Serial.println("❌ Sync failed: " + fbdo.errorReason());
      break;  // Stop on first failure
    }
    delay(50);  // Small delay between writes
  }
  
  if (successCount == count) {
    // All synced - clear cache
    SPIFFS.remove(CACHE_FILE);
    Serial.printf("✅ Synced all %d entries, cache cleared\n", successCount);
  } else {
    Serial.printf("⚠️ Synced %d/%d entries\n", successCount, count);
  }
}

// Connect to WiFi
bool connectWiFi() {
  Serial.println("📶 Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
    return true;
  }
  
  Serial.println("\n❌ WiFi connection failed");
  wifiConnected = false;
  return false;
}

// Initialize Firebase
bool initFirebase() {
  if (!wifiConnected) return false;
  
  Serial.println("🔥 Initializing Firebase...");
  
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_HOST;
  
  auth.user.email = FIREBASE_USER_EMAIL;
  auth.user.password = FIREBASE_USER_PASSWORD;
  
  config.token_status_callback = tokenStatusCallback;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Wait for authentication
  unsigned long startTime = millis();
  while (!Firebase.ready() && (millis() - startTime) < 10000) {
    delay(100);
  }
  
  if (Firebase.ready()) {
    Serial.println("✅ Firebase connected");
    firebaseConnected = true;
    return true;
  }
  
  Serial.println("❌ Firebase connection failed");
  firebaseConnected = false;
  return false;
}

// Send data to Firebase
bool sendToFirebase(sensor_data &data, String timestamp) {
  if (!Firebase.ready()) {
    firebaseConnected = false;
    return false;
  }
  
  String path = "/sensor_readings/" + String(data.sensorID) + "/" + String(getUnixTime());
  
  FirebaseJson json;
  json.set("temperature", data.temperature);
  json.set("humidity", data.humidity);
  json.set("airQuality", data.airQuality);
  json.set("timestamp", timestamp);
  
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("☁️  Sent to Firebase");
    
    // Also update latest reading
    String latestPath = "/latest/" + String(data.sensorID);
    json.set("lastUpdate", timestamp);
    Firebase.RTDB.setJSON(&fbdo, latestPath.c_str(), &json);
    
    return true;
  } else {
    Serial.println("❌ Firebase error: " + fbdo.errorReason());
    firebaseConnected = false;
    return false;
  }
}

// Try to reconnect WiFi and Firebase
void tryReconnect() {
  if (millis() - lastReconnectAttempt < RECONNECT_INTERVAL) return;
  lastReconnectAttempt = millis();
  
  Serial.println("\n🔄 Attempting reconnection...");
  
  // Temporarily disable ESP-NOW for WiFi
  esp_now_deinit();
  
  if (connectWiFi()) {
    // Sync time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    delay(1000);
    
    if (initFirebase()) {
      // Sync cached data
      syncOfflineCache();
    }
  }
  
  // Re-enable ESP-NOW
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataReceive);
    Serial.println("✅ ESP-NOW re-initialized\n");
  }
}

// ESP-NOW receive callback
void onDataReceive(const uint8_t *mac, const uint8_t *data, int len) {
  memcpy(&receivedData, data, sizeof(receivedData));
  
  String timestamp = getTimestamp();

  Serial.printf(
    "[%s] ID:%d | T:%.2f°C | H:%.2f%% | AQ:%d",
    timestamp.c_str(),
    receivedData.sensorID,
    receivedData.temperature,
    receivedData.humidity,
    receivedData.airQuality
  );
  
  // Try to send to Firebase, otherwise cache offline
  if (firebaseConnected && sendToFirebase(receivedData, timestamp)) {
    Serial.println("");
  } else {
    Serial.println(" [OFFLINE]");
    saveToOfflineCache(receivedData, timestamp);
  }
}

// Sync time with NTP server
void syncTimeWithNTP() {
  Serial.println("⏰ Syncing time with NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  struct tm timeinfo;
  int timeout = 0;
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
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n========================================");
  Serial.println("   ESP32 Air Quality Control Hub");
  Serial.println("   With Firebase + Offline Fallback");
  Serial.println("========================================\n");

  // Initialize SPIFFS for offline storage
  initSPIFFS();
  
  // Check for cached data
  int cachedCount = getCacheCount();
  if (cachedCount > 0) {
    Serial.printf("📦 Found %d cached entries from previous session\n", cachedCount);
  }

  // Connect WiFi
  if (connectWiFi()) {
    // Sync time
    syncTimeWithNTP();
    
    // Initialize Firebase
    if (initFirebase()) {
      // Sync any cached data
      if (cachedCount > 0) {
        syncOfflineCache();
      }
    }
  }
  
  // Disconnect WiFi and setup ESP-NOW
  Serial.println("\n📡 Setting up ESP-NOW...");
  WiFi.disconnect();
  delay(100);
  
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
  Serial.println("✅ ESP-NOW Receiver Ready");
  Serial.println("\n----------------------------------------");
  Serial.println("Waiting for sensor data...");
  Serial.println("----------------------------------------\n");
}

void loop() {
  // Periodically try to reconnect if offline
  if (!firebaseConnected) {
    tryReconnect();
  }
  
  delay(10);
}
