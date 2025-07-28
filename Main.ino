//Automation System for fingerprint-based food collection
//Designed for ESP32 with Adafruit Fingerprint Sensor
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Adafruit_Fingerprint.h>

// WiFi credentials
const char* ssid = "iPhone 14 Plus";
const char* password = "jbgadget2023";

// Supabase
const char* supabase_url = "https://cskdjbpsiupasdhynazt.supabase.co";
const char* supabase_apikey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImNza2RqYnBzaXVwYXNkaHluYXp0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTI3NDM2MTIsImV4cCI6MjA2ODMxOTYxMn0.n5V-Jl2njI3AdzWuXcFjFjqCdD4xdqUf7OCcfEA8Ahg";

// UART1 Pins
#define RX_PIN 16
#define TX_PIN 17

// Fingerprint sensor on UART1
HardwareSerial mySerial(1);
Adafruit_Fingerprint finger(&mySerial);

// State
String mode = "collection";
int staffidToRegister = -1;

// WiFi client
WiFiClientSecure client;
HTTPClient https;

// Forward declarations
int findNextAvailableID();
int getTagByFingerprint(int fid);
bool hasCollectedToday(int tag);

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

// =========== Check Supabase Control Mode =============
String checkControlMode() {
  client.setInsecure();
  HTTPClient http;

  String url = String(supabase_url) +
               "/rest/v1/control?select=mode,staffid&processed=eq.false&limit=1";

  Serial.println("Requesting: " + url);  // For debugging the full URL

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP request");
    return "";
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
  http.addHeader("Accept", "application/json");

  int responseCode = http.GET();

  if (responseCode != 200) {
    Serial.println("Failed to get control mode. HTTP code: " + String(responseCode));
    String error = http.getString();
    Serial.println("Error response: " + error);
    http.end();
    return "";
  }

  String payload = http.getString();
  Serial.println("Control GET payload: " + payload);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON deserialization failed: ");
    Serial.println(error.f_str());
    http.end();
    return "";
  }

  if (doc.size() == 0) {
    Serial.println("No control record found");
    http.end();
    return "";
  }

  JsonObject first = doc[0];
  mode = first["mode"] | "";
  staffidToRegister = first["staffid"] | -1;

  Serial.println("Control mode: " + mode + ", staffidToRegister: " + String(staffidToRegister));

  http.end();
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
bool enrollFingerprint(int staffid) {
  int id = findNextAvailableID();
  if (id == -1) {
    Serial.println("No available fingerprint slots");
    return false;
  }

  Serial.println("Assigning fingerprint ID: " + String(id));
  Serial.println("Place finger to enroll...");

  uint8_t p = -1;
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) continue;
    Serial.println("Error capturing image: " + String(p));
    return false;
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    Serial.println("Error creating template 1");
    return false;
  }

  Serial.println("Remove finger");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  Serial.println("Place same finger again");
  while ((p = finger.getImage()) != FINGERPRINT_OK);

  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    Serial.println("Error creating template 2");
    return false;
  }

  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("Fingerprints didn't match");
    return false;
  }

  if (finger.storeModel(id) != FINGERPRINT_OK) {
    Serial.println("Failed to store model");
    return false;
  }

  Serial.println("Fingerprint enrolled successfully!");

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
  doc["fingerprintid"] = id;
  String body;
  serializeJson(doc, body);

  int res = http.PATCH(body);
  Serial.println("Staff update response: " + String(res));

  if (res == HTTP_CODE_OK || res == HTTP_CODE_NO_CONTENT) {
    Serial.println("Successfully updated staff record");
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

  JsonDocument doc;
  doc["fingerprintid"] = fid;
  doc["tag"] = tag;
  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  Serial.println("Collection log response: " + String(code));

  if (code == HTTP_CODE_CREATED) {
    Serial.println("Successfully logged collection");
  } else {
    String response = http.getString();
    Serial.println("Failed to log collection. Response: " + response);
  }

  http.end();
}

// ============= Get Tag by Fingerprint ID ===============
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
bool hasCollectedToday(int tag) {
  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/food_collections?tag=eq." + String(tag) + "&select=tag&limit=1&order=created_at.desc";

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTP connection");
    return false;
  }

  http.addHeader("apikey", supabase_apikey);
  http.addHeader("Authorization", String("Bearer ") + supabase_apikey);

  int code = http.GET();
  bool hasCollected = false;

  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc.size() > 0) {
      hasCollected = true;
    }
  }

  http.end();
  return hasCollected;
}

// ====================== Setup =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  mySerial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(1000);

  Serial.println("Initializing system...");
  connectToWiFi();

  if(!finger.verifyPassword()){
    Serial.println("Fingerprint sensor not found.");
    while(1) delay(10);
  }
}

// ================== Main Loop ========================
void loop() {
  static unsigned long lastCheck = 0;

  if (millis() - lastCheck >= 3000) {
    lastCheck = millis();
    checkControlMode();
  }

  if (mode == "register" && staffidToRegister > 0) {
    if (enrollFingerprint(staffidToRegister)) {
      updateControlModeToCollection();
    }
    staffidToRegister = -1;
  } 
  else if (mode == "collection") {
    verifyFingerprintAndLog();
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost");
    delay(1000);
    connectToWiFi();
  }
}
