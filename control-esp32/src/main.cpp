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
const long gmtOffset_sec = 3600;   // UTC+1 (West Africa)
const int daylightOffset_sec = 0; // No daylight saving

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Connection state
bool wifiConnected = false;
bool firebaseConnected = false;
bool firebaseAuthAttempted = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 60000;  // Try reconnect every 60 seconds (was 5 min)
const unsigned long FIREBASE_AUTH_TIMEOUT = 5000;  // 5 seconds timeout for auth
const int MAX_AUTH_RETRIES = 3;
int authRetryCount = 0;
bool tokenExpired = false;  // Track if token was revoked/expired

// Offline cache file
const char* CACHE_FILE = "/offline_cache.json";
const int MAX_CACHE_ENTRIES = 100;

// Packed struct ensures consistent memory layout across ESP8266/ESP32
typedef struct __attribute__((packed)) sensor_data {
  uint8_t sensorID;
  float temperature;
  float humidity;
  int airQuality;
  uint8_t crc;
} sensor_data;

sensor_data receivedData;

// CRC8 calculation (must match ESP8266)
uint8_t calculateCRC(const sensor_data* data) {
  uint8_t crc = 0;
  const uint8_t* bytes = (const uint8_t*)data;
  for (size_t i = 0; i < sizeof(sensor_data) - 1; i++) {
    crc ^= bytes[i];
  }
  return crc;
}

// Verify CRC of received data
bool verifyCRC(const sensor_data* data) {
  uint8_t calculatedCRC = calculateCRC(data);
  return (calculatedCRC == data->crc);
}

// Watchdog - reset if no activity
#include <Ticker.h>
Ticker watchdog;
unsigned long lastDataReceived = 0;
#define WATCHDOG_TIMEOUT 60

void watchdogCallback() {
  unsigned long elapsed = millis() - lastDataReceived;
  if (elapsed > WATCHDOG_TIMEOUT * 1000) {
    Serial.printf("\n⚠️ Watchdog triggered! No data received in %lu seconds\n", elapsed / 1000);
    ESP.restart();
  }
}

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
  if (!Firebase.ready()) {
    Serial.println("⚠️ Firebase not ready, resetting auth for retry...");
    firebaseConnected = false;
    firebaseAuthAttempted = false;  // Allow retry
    tokenExpired = true;
    return;
  }
  
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
    // Check if Firebase token expired during sync
    if (!Firebase.ready()) {
      String err = fbdo.errorReason();
      if (err.indexOf("token") != -1 || err.indexOf("expired") != -1 || err.indexOf("revoked") != -1) {
        Serial.println("⚠️ Token expired during sync, resetting auth...");
        tokenExpired = true;
        firebaseAuthAttempted = false;
      }
      break;
    }
    
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
      String err = fbdo.errorReason();
      Serial.println("❌ Sync failed: " + err);
      
      // Check if token expired
      if (err.indexOf("token") != -1 || err.indexOf("expired") != -1 || err.indexOf("revoked") != -1) {
        tokenExpired = true;
        firebaseAuthAttempted = false;
      }
      break;
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

// WiFi disconnect handler
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println("📶 WiFi connected");
      wifiConnected = true;
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("⚠️ WiFi disconnected");
      wifiConnected = false;
      firebaseConnected = false;
      break;
    default:
      break;
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
  
  // Prevent repeated auth attempts if already failed MAX_AUTH_RETRIES times (unless token expired)
  if (firebaseAuthAttempted && authRetryCount >= MAX_AUTH_RETRIES && !tokenExpired) {
    Serial.println("⚠️ Firebase auth disabled (max retries exceeded)");
    Serial.println("   System will continue with offline storage only");
    return false;
  }
  
  Serial.println("🔥 Initializing Firebase...");
  
  // Re-init Firebase if token expired (need fresh auth)
  if (!firebaseAuthAttempted || tokenExpired) {
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_HOST;
    
    auth.user.email = FIREBASE_USER_EMAIL;
    auth.user.password = FIREBASE_USER_PASSWORD;
    
    config.token_status_callback = tokenStatusCallback;
    
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    firebaseAuthAttempted = true;
    tokenExpired = false;
  }
  
  // Wait for authentication with shorter timeout
  unsigned long startTime = millis();
  while (!Firebase.ready() && (millis() - startTime) < FIREBASE_AUTH_TIMEOUT) {
    delay(100);
  }
  
  if (Firebase.ready()) {
    Serial.println("✅ Firebase connected");
    firebaseConnected = true;
    authRetryCount = 0;  // Reset retry count on success
    tokenExpired = false;
    return true;
  }
  
  authRetryCount++;
  Serial.printf("❌ Firebase auth failed (attempt %d/%d)\n", authRetryCount, MAX_AUTH_RETRIES);
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
  if (len != sizeof(sensor_data)) {
    Serial.printf("⚠️ Invalid packet size: %d (expected %d)\n", len, sizeof(sensor_data));
    return;
  }
  
  memcpy(&receivedData, data, sizeof(receivedData));
  
  // Verify CRC before processing
  if (!verifyCRC(&receivedData)) {
    Serial.printf("❌ CRC mismatch! Packet corrupted. Expected: %02X, Got: %02X\n",
                 calculateCRC(&receivedData), receivedData.crc);
    return;
  }
  
  // CRC valid - update watchdog
  lastDataReceived = millis();
  
  String timestamp = getTimestamp();

  Serial.printf(
    "[%s] ID:%d | T:%.2f°C | H:%.2f%% | AQ:%d | CRC:✓",
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
  Serial.println("   With CRC, Watchdog & Auto-Reconnect");
  Serial.println("========================================\n");

  // Register WiFi event handler
  WiFi.onEvent(WiFiEvent);
  
  // Initialize watchdog timer
  lastDataReceived = millis();
  watchdog.attach(WATCHDOG_TIMEOUT, watchdogCallback);
  Serial.printf("✅ Watchdog started (%ds timeout)\n", WATCHDOG_TIMEOUT);

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

// Reset Firebase auth (callable from serial for manual retry)
void resetFirebaseAuth() {
  firebaseAuthAttempted = false;
  authRetryCount = 0;
  firebaseConnected = false;
  tokenExpired = false;
  Serial.println("🔄 Firebase auth reset. Will retry on next reconnect.");
}

// Handle serial commands
void handleSerialCommands() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "RESET_FB") {
      resetFirebaseAuth();
    } else if (cmd == "STATUS") {
      Serial.printf("WiFi: %s\n", wifiConnected ? "Connected" : "Disconnected");
      Serial.printf("Firebase: %s\n", firebaseConnected ? "Connected" : "Disconnected");
      Serial.printf("Token Expired: %s\n", tokenExpired ? "Yes" : "No");
      Serial.printf("Auth Attempts: %d/%d\n", authRetryCount, MAX_AUTH_RETRIES);
      Serial.printf("Cached entries: %d\n", getCacheCount());
      Serial.printf("Last Reconnect: %lu seconds ago\n", (millis() - lastReconnectAttempt) / 1000);
    } else if (cmd.length() > 0) {
      Serial.println("Commands: RESET_FB, STATUS");
    }
  }
}

void loop() {
  // Handle serial commands
  handleSerialCommands();
  
  // Periodically try to reconnect if offline (respects RECONNECT_INTERVAL)
  if (!firebaseConnected || tokenExpired) {
    // Check if enough time has passed since last attempt
    if (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL) {
      tryReconnect();
    }
  }
  
  delay(10);
}
