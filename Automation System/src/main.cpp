//Automation System for fingerprint-based food collection
//Designed for ESP32 with Adafruit Fingerprint Sensor
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <sys/time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <WiFiClientSecure.h>
#include <Adafruit_Fingerprint.h>

// WiFi credentials
const char* ssid = "iPhone 14 Plus";
const char* password = "jbgadget2023";

// WiFi client
WiFiClientSecure client;
HTTPClient https;

// Supabase
const char* supabase_url = "https://cskdjbpsiupasdhynazt.supabase.co";
const char* supabase_apikey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImNza2RqYnBzaXVwYXNkaHluYXp0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTI3NDM2MTIsImV4cCI6MjA2ODMxOTYxMn0.n5V-Jl2njI3AdzWuXcFjFjqCdD4xdqUf7OCcfEA8Ahg";

// UART1 Pins
#define RX_PIN 16
#define TX_PIN 17

// Display pins
#define DISPLAYRX 22
#define DISPLAYTX 23

#define BUZZER_PIN 13

// Fingerprint sensor on UART1
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger(&mySerial);

//Display object
HardwareSerial displaySerial(2);
TFT_eSPI tft = TFT_eSPI();

// State
String mode = "collection";
int staffidToRegister = -1;

// Forward declarations
int findNextAvailableID();
int getTagByFingerprint(int fid);
bool hasCollectedToday(int tag);
bool staffExists(int staffid);
bool updateStaffFingerprint(int staffid, int fid);

// ============= WiFi Connection ========================
void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }
  Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
}

// NTP time sync for accurate time-based operations
void syncTime() {
  Serial.println("Syncing time via NTP...");

  // Set local timezone to WAT (West Africa Time)
  configTzTime("WAT-1", "pool.ntp.org", "time.nist.gov");  // UTC+1

  time_t now = time(nullptr);
  int attempts = 0;

  while (now < 100000 && attempts < 20) {  // Try for 10 seconds max
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }

  if (now < 100000) {
    Serial.println("Time sync failed!");
  } else {
    Serial.println("Time synced!");
    struct tm* timeinfo = localtime(&now); 
    Serial.printf("Current time (WAT): %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
                  timeinfo->tm_mday, timeinfo->tm_hour,
                  timeinfo->tm_min, timeinfo->tm_sec);
  }
}

void successBeep(){
  tone(BUZZER_PIN, 1000, 150);
  delay(200);
}

void errorBeep(){
  tone(BUZZER_PIN, 500, 300);
  delay(350);
}

void sendToDisplay(const String &cmd){
  displaySerial.println(cmd);
  displaySerial.print('\n');
}

// =========== Check Supabase Control Mode =============
String checkControlMode() {
  client.setInsecure();
  HTTPClient http;

  String url = String(supabase_url) +
               "/rest/v1/control?select=mode,staffid&processed=eq.false&limit=1";

  Serial.println("Requesting: " + url);

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP request");
    return "";
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
  http.addHeader("Accept", "application/json");

  int responseCode = http.GET();
  String payload = http.getString();

  if (responseCode != 200) {
    Serial.println("Failed to get control mode. HTTP code: " + String(responseCode));
    String error = http.getString();
    Serial.println("Error response: " + error);
    http.end();
    return "";
  }

  Serial.println("Control GET payload: " + payload);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);
  http.end();

  if (error) {
    Serial.print("JSON deserialization failed: ");
    Serial.println(error.f_str());
    return "";
  }

  if (doc.is<JsonArray>() && doc.size() == 0) {
    mode = "collection";
    staffidToRegister = -1;
    Serial.println("No unprocessed control. Defaulting to collection mode.");
    return mode;
  }

  JsonObject first = doc[0];
  mode = first["mode"] | "";
  staffidToRegister = first["staffid"] | -1;

  Serial.println("Control mode: " + mode + ", staffidToRegister: " + String(staffidToRegister));

  return mode;
}

// =========== Update Control Mode ========
void updateControlModeToCollection() {
  if (staffidToRegister <= 0) return;

  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/control?staffid=eq." + String(staffidToRegister);

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP connection");
    return;
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("Accept", "application/json");
  
  JsonDocument doc;
  doc["processed"] = true;
  doc["mode"] = "collection";
  String payload;
  serializeJson(doc, payload);

  int responseCode = http.PATCH(payload);
  Serial.println("Control update response: " + String(responseCode));

  if (responseCode == HTTP_CODE_OK || responseCode == HTTP_CODE_NO_CONTENT) {
    Serial.println("Successfully updated control record");
    staffidToRegister = -1;
  } else {
    String response = http.getString();
    Serial.println("Update failed. Response: " + response);
  }

  http.end();
}

// ============= Fingerprint Enrollment ==================
bool enrollFingerprint(int staffid, unsigned long timeout = 30000) {
  // Verify staff exists first
  if (!staffExists(staffid)) {
    Serial.println("Error: Staff ID " + String(staffid) + " not found");
    return false;
  }

  int id = findNextAvailableID();
  if (id == -1) {
    Serial.println("No available fingerprint slots");
    return false;
  }

  Serial.println("Assigning fingerprint ID: " + String(id) + " to staff ID: " + String(staffid));
  Serial.println("Place finger to enroll...");
  sendToDisplay("show_register_1");


  unsigned long startTime = millis();
  uint8_t p;

  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (millis() - startTime > timeout) {
      Serial.println("Enrollment timed out (first image)");
      return false;
    }
    delay(100);
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    Serial.println("Error converting first image");
    return false;
  }

  Serial.println("Remove finger...");
  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    if (millis() - startTime > timeout) {
      Serial.println("Timeout waiting for finger to be removed");
      return false;
    }
    delay(50);
  }

  Serial.println("Place same finger again...");
  sendToDisplay("show_register_2");

  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (millis() - startTime > timeout) {
      Serial.println("Timeout waiting for second placement");
      return false;
    }
    delay(100);
  }

  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    Serial.println("Error converting second image");
    return false;
  }

  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("Fingerprints did not match");
    return false;
  }

  if (finger.storeModel(id) != FINGERPRINT_OK) {
    Serial.println("Failed to store fingerprint");
    errorBeep();
    return false;
  }

  Serial.println("Fingerprint enrolled successfully!");
  successBeep();
  sendToDisplay("show_success");
  return updateStaffFingerprint(staffid, id);
}

// ============= Helper Functions ==================
bool staffExists(int staffid) {
  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/staff?staffid=eq." + String(staffid) + "&select=staffid";
  
  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP connection");
    return false;
  }
  
  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
  
  int code = http.GET();
  bool exists = (code == HTTP_CODE_OK);
  http.end();
  
  if (!exists) {
    Serial.println("Staff ID " + String(staffid) + " not found in database");
  }
  
  return exists;
}

bool updateStaffFingerprint(int staffid, int fid) {
  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/staff?staffid=eq." + String(staffid);

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP connection");
    return false;
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  JsonDocument doc;
  doc["fingerprintid"] = fid;
  String body;
  serializeJson(doc, body);

  int res = http.PATCH(body);
  Serial.println("Staff update response: " + String(res));

  if (res == HTTP_CODE_OK || res == HTTP_CODE_NO_CONTENT) {
    Serial.println("Successfully updated staff record with fingerprint ID");
    http.end();
    return true;
  } else {
    String response = http.getString();
    Serial.println("Update failed. Response: " + response);
    http.end();
    return false;
  }
}

// ============= Get Next Fingerprint ID =================
int findNextAvailableID() {
  for (int id = 1; id <= 127; id++) {
    if (finger.loadModel(id) != FINGERPRINT_OK) {
      return id;
    }
  }
  return -1;
}

// ============= Get Staff ID by Fingerprint ==============
int getStaffIdByFingerprint(int fid) {
  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/staff?fingerprintid=eq." + String(fid) + "&select=staffid";

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP connection");
    return -1;
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc.size() > 0) {
      return doc[0]["staffid"];
    }
  }

  http.end();
  return -1;
}

// ====== Verify Fingerprint & Log Collection ============
void verifyFingerprintAndLog() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    if (p != FINGERPRINT_NOFINGER) {
      Serial.println("Error getting image: " + String(p));
    }
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting image: " + String(p));
    return;
  }

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOTFOUND) {
      Serial.println("No matching fingerprint found");
    } else {
      Serial.println("Search error: " + String(p));
    }
    return;
  }

  int fid = finger.fingerID;
  int tag = getTagByFingerprint(fid);

  if (tag == -1) {
    Serial.println("No staff record found for fingerprint ID: " + String(fid));
    return;
  }

  if (hasCollectedToday(tag)) {
    Serial.println("Tag " + String(tag) + " already collected today");
    sendToDisplay("show_error");
    return;
  }

  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/food_collections";

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP connection");
    return;
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  time_t now = time(nullptr);
  now += 3600; // Adjust for your timezone (3600 = +1 hour)
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S+01:00", gmtime(&now));

  // Get staffid for this fingerprint
  int staffid = getStaffIdByFingerprint(fid);
  if (staffid == -1) {
    Serial.println("Error: No staff ID found for fingerprint");
    http.end();
    return;
  }

  JsonDocument doc;
  doc["fingerprintid"] = fid;
  doc["tag"] = tag;
  doc["staffid"] = staffid; 
  doc["time_collected"] = timeStr; 

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);

  if (code == HTTP_CODE_CREATED) {
    Serial.println("Successfully logged collection");
    sendToDisplay("show_success");
    successBeep();
  } else {
    String response = http.getString();
    Serial.println("Failed to log collection. Response: " + response);
    sendToDisplay("show_error");
    errorBeep();
  }

  http.end();
}

// ============= Get Tag by Fingerprint ID ================
int getTagByFingerprint(int fid) {
  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/staff?fingerprintid=eq." + String(fid) + "&select=tag";

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP connection");
    return -1;
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc.size() > 0) {
      return doc[0]["tag"];
    }
  }

  http.end();
  return -1;
}

// ============ Check if Already Collected ===============
bool hasCollectedToday(int staffid) {
  client.setInsecure();

  String url = String(supabase_url) +
    "/rest/v1/food_collections?staffid=eq." + String(staffid) +
    "&select=time_collected&order=time_collected.desc&limit=1";

  https.begin(client, url);
  https.addHeader("apikey", supabase_apikey);
  https.addHeader("Authorization", "Bearer " + String(supabase_apikey));

  int httpCode = https.GET();
  if (httpCode > 0) {
    String payload = https.getString();
    Serial.println("Supabase response: " + payload);

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    if (doc.size() > 0) {
      const char* lastTime = doc[0]["time_collected"];
      struct tm tm;
      if (strptime(lastTime, "%Y-%m-%dT%H:%M:%S", &tm)) {
        time_t lastCollection = mktime(&tm);
        time_t now = time(nullptr);

        struct tm* nowTm = localtime(&now);
        struct tm* lastTm = localtime(&lastCollection);

        // Check if same calendar day
        if (nowTm->tm_year == lastTm->tm_year &&
            nowTm->tm_yday == lastTm->tm_yday) {
          return true;  // Already collected today
          sendToDisplay("show_error");
        }
      }
    }
  }

  https.end();
  return false;  // No collection today
}

// ====================== Setup =========================
void setup() {
  Serial.begin(115200);
  delay(200);
  mySerial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);

  displaySerial.begin(115200, SERIAL_8N1, DISPLAYRX, DISPLAYTX);

  pinMode(BUZZER_PIN, OUTPUT);

  connectToWiFi();
  syncTime();
 
  if (!finger.verifyPassword()) {
    Serial.println("Fingerprint sensor not found.");
    while (1) delay(10);
  }
}

// ================== Main Loop ========================
void loop() {
  static unsigned long lastCheck = 0;

  // Poll Supabase for control mode every 3 seconds
  if (millis() - lastCheck >= 3000) {
    lastCheck = millis();
    checkControlMode();
  }

  // Handle registration mode
  if (mode == "register" && staffidToRegister > 0) {
    Serial.println("Entering registration mode...");

    bool enrolled = enrollFingerprint(staffidToRegister, 30000); 

    if (enrolled) {
      Serial.println("Enrollment successful");
      mySerial.println("show_success");

    } else {
      Serial.println("Enrollment failed or timed out");
    }

    updateControlModeToCollection();  // Reset to collection mode either way
    staffidToRegister = -1;           // Clear current staff ID
    mode = "collection";              // Ensure mode is switched locally too
  }

  // Handle collection mode
  else if (mode == "collection") {
    verifyFingerprintAndLog();
    delay(500); // Small delay to avoid overloading sensor
  }

  // Reconnect WiFi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    delay(1000);
    connectToWiFi();
  }
}