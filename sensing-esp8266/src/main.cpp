// Sensing subsystem - ESP8266 (Sender/Master)
// Reads sensors and sends data to ESP32 via ESP-NOW

#include <Arduino.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <LittleFS.h>
extern "C" {
  #include <user_interface.h>
}


DHT dht(5, DHT11);

// Default ESP32 MAC Address (fallback if not configured)
// ⚠️ Update this to match YOUR ESP32's MAC address!
uint8_t defaultEsp32Address[] = {0x00, 0x4B, 0x12, 0x38, 0xB0, 0xE4};
uint8_t esp32Address[6];

// Data structure - MUST match ESP32 receiver exactly!
// Packed struct ensures consistent memory layout across ESP8266/ESP32
typedef struct __attribute__((packed)) sensor_data {
  uint8_t sensorID;
  float temperature;
  float humidity;
  int airQuality;
  uint8_t crc;
} sensor_data;

sensor_data myData;
float lastKnownTemp = 22.0;
float lastKnownHumidity = 50.0;
int lastKnownAirQuality = 100;

// Define which sensor this is (change to 2, 3, etc. for other ESP8266s)
#define THIS_SENSOR_ID 1

// Send interval (milliseconds)
#define SEND_INTERVAL 2000

// Watchdog - reset if no successful loop in 60 seconds
#include <Ticker.h>
Ticker watchdog;
unsigned long lastSuccessfulSend = 0;

#define WATCHDOG_TIMEOUT 60

// CRC8 calculation (simple XOR-based)
uint8_t calculateCRC(const sensor_data* data) {
  uint8_t crc = 0;
  const uint8_t* bytes = (const uint8_t*)data;
  for (size_t i = 0; i < sizeof(sensor_data) - 1; i++) {
    crc ^= bytes[i];
  }
  return crc;
}

// Read DHT with retry logic and fallback
bool readDHT(float& temperature, float& humidity, int retries = 3) {
  for (int i = 0; i < retries; i++) {
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    
    if (!isnan(temperature) && !isnan(humidity)) {
      lastKnownTemp = temperature;
      lastKnownHumidity = humidity;
      return true;
    }
    
    Serial.printf("DHT read attempt %d failed, retrying...\n", i + 1);
    delay(100);
  }
  
  Serial.println("DHT read failed, using last known values");
  temperature = lastKnownTemp;
  humidity = lastKnownHumidity;
  return false;
}

// Load ESP32 MAC from LittleFS config
bool loadEsp32Mac() {
  if (!LittleFS.begin()) {
    Serial.println("⚠️ LittleFS mount failed, using default MAC");
    memcpy(esp32Address, defaultEsp32Address, 6);
    return false;
  }
  
  if (LittleFS.exists("/esp32_mac.txt")) {
    File file = LittleFS.open("/esp32_mac.txt", "r");
    if (file) {
      String macStr = file.readStringUntil('\n');
      file.close();
      
      int values[6];
      if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
                 &values[0], &values[1], &values[2],
                 &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; i++) {
          esp32Address[i] = (uint8_t)values[i];
        }
        Serial.printf("✅ Loaded ESP32 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      esp32Address[0], esp32Address[1], esp32Address[2],
                      esp32Address[3], esp32Address[4], esp32Address[5]);
        return true;
      }
    }
  }
  
  Serial.println("⚠️ No configured MAC found, using default");
  memcpy(esp32Address, defaultEsp32Address, 6);
  return false;
}

// Save ESP32 MAC to LittleFS
void saveEsp32Mac(const String& macStr) {
  File file = LittleFS.open("/esp32_mac.txt", "w");
  if (file) {
    file.println(macStr);
    file.close();
    
    int values[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) == 6) {
      for (int i = 0; i < 6; i++) {
        esp32Address[i] = (uint8_t)values[i];
      }
      Serial.printf("✅ Saved ESP32 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    esp32Address[0], esp32Address[1], esp32Address[2],
                    esp32Address[3], esp32Address[4], esp32Address[5]);
    }
  } else {
    Serial.println("❌ Failed to save MAC config");
  }
}

// Handle serial commands for configuration
void handleSerialCommands() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd.startsWith("SETMAC ")) {
      String macStr = cmd.substring(7);
      macStr.replace(":", "");
      if (macStr.length() == 12) {
        Serial.printf("Setting new ESP32 MAC: %s\n", macStr.c_str());
        saveEsp32Mac(macStr);
        Serial.println("✅ MAC updated. Restarting ESP-NOW peer...");
        esp_now_del_peer(esp32Address);
        esp_now_add_peer(esp32Address, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
      } else {
        Serial.println("❌ Invalid MAC format. Use: SETMAC XX:XX:XX:XX:XX:XX");
      }
    } else if (cmd == "GETMAC") {
      Serial.printf("Current ESP32 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    esp32Address[0], esp32Address[1], esp32Address[2],
                    esp32Address[3], esp32Address[4], esp32Address[5]);
    } else if (cmd == "STATUS") {
      Serial.printf("Sensor ID: %d\n", THIS_SENSOR_ID);
      Serial.printf("Last Temp: %.1f°C, Humidity: %.1f%%, AQ: %d\n",
                    lastKnownTemp, lastKnownHumidity, lastKnownAirQuality);
    } else if (cmd.length() > 0) {
      Serial.println("Unknown command. Available: SETMAC, GETMAC, STATUS");
    }
  }
}


// ===== CALLBACK FUNCTION =====
// Runs after data is sent - tells you if it worked
void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Send Status: ");
  if (sendStatus == 0) {
    Serial.println("Success");
    lastSuccessfulSend = millis();
  } else {
    Serial.println("❌ Failed");
  }
}

// Watchdog callback - reset ESP if frozen
void watchdogCallback() {
  unsigned long elapsed = millis() - lastSuccessfulSend;
  if (elapsed > WATCHDOG_TIMEOUT * 1000) {
    Serial.printf("\n⚠️ Watchdog triggered! No successful send in %lu seconds\n", elapsed / 1000);
    ESP.restart();
  }
}


void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n========================================");
  Serial.println("  ESP8266 Sensor Node #" + String(THIS_SENSOR_ID));
  Serial.println("  With CRC, Retry & Watchdog");
  Serial.println("========================================\n");
  
  // Initialize sensors
  dht.begin();
  
  // Initialize LittleFS for config storage
  LittleFS.begin();
  
  // Load ESP32 MAC from config (or use default)
  loadEsp32Mac();
  
  // Initialize last successful send time
  lastSuccessfulSend = millis();
  
  // Start watchdog timer
  watchdog.attach(WATCHDOG_TIMEOUT, watchdogCallback);
  Serial.printf("✅ Watchdog started (%ds timeout)\n", WATCHDOG_TIMEOUT);
  
  // Set WiFi to Station mode (required for ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Force WiFi channel 1 for ESP-NOW communication
  wifi_set_channel(1);
  
  // Print this ESP8266's MAC address
  wifi_promiscuous_enable(1);
  wifi_set_channel(1);
  wifi_promiscuous_enable(0);
  
  Serial.print("ESP8266 MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Target ESP32 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                esp32Address[0], esp32Address[1], esp32Address[2],
                esp32Address[3], esp32Address[4], esp32Address[5]);
  
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
  
  Serial.println("📡 Ready to send data to ESP32");
  Serial.println("Commands: SETMAC XX:XX:XX:XX:XX:XX | GETMAC | STATUS");
  Serial.println("----------------------------------------\n");
}


void loop() {
  // Handle serial commands for configuration
  handleSerialCommands();
  
  // ===== READ SENSORS WITH RETRY =====
  float temperature, humidity;
  bool dhtSuccess = readDHT(temperature, humidity);
  int gasValue = analogRead(A0);
  
  // Update last known air quality
  lastKnownAirQuality = gasValue;
  
  // ===== PREPARE DATA =====
  myData.sensorID = THIS_SENSOR_ID;
  myData.temperature = temperature;
  myData.humidity = humidity;
  myData.airQuality = gasValue;
  
  // Calculate and add CRC
  myData.crc = calculateCRC(&myData);
  
  // Print what we're sending
  Serial.printf("Sensor #%d | T:%.1f°C | H:%.1f%% | AQ:%d | CRC:%02X %s\n",
                myData.sensorID, myData.temperature, myData.humidity, 
                myData.airQuality, myData.crc, 
                dhtSuccess ? "✓" : "(fallback)");
  
  // ===== SEND DATA TO ESP32 =====
  int result = esp_now_send(esp32Address, (uint8_t *)&myData, sizeof(myData));
  
  if (result == 0) {
    Serial.println("  → Data sent successfully");
  } else {
    Serial.printf("  → Send failed: %d\n", result);
  }
  
  // Wait before next reading
  delay(SEND_INTERVAL);
}
