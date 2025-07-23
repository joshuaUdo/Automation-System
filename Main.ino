//Automation System for fingerprint-based food collection
//Designed for ESP32 with Adafruit Fingerprint Sensor
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>

#define BUTTON_PIN 13
#define DEBOUNCE_DELAY 500
#define FOOD_COLLECTION_LIMIT 1

HardwareSerial serialPort(2); // UART2
Adafruit_Fingerprint finger(&serialPort);

enum SystemMode { COLLECTION_MODE, REGISTRATION_MODE };
SystemMode currentMode = COLLECTION_MODE;

unsigned long lastButtonPress = 0;

// WiFi credentials
const char *ssid = "iPhone 14 Plus"; //replace with your WiFi SSID
const char *password = "jbgadget2023"; //replace with your WiFi password

// Supabase
const char *supabaseURL = "https://cskdjbpsiupasdhynazt.supabase.co/rest/v1";
const char *supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImNza2RqYnBzaXVwYXNkaHluYXp0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTI3NDM2MTIsImV4cCI6MjA2ODMxOTYxMn0.n5V-Jl2njI3AdzWuXcFjFjqCdD4xdqUf7OCcfEA8Ahg";

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}

void toggleMode() {
  //Might need to replace this when the website is ready
  if (millis() - lastButtonPress < DEBOUNCE_DELAY) return;
  lastButtonPress = millis();

  currentMode = (currentMode == COLLECTION_MODE) ? REGISTRATION_MODE : COLLECTION_MODE;
  Serial.println(currentMode == REGISTRATION_MODE ? ">> REGISTRATION MODE <<" : ">> COLLECTION MODE <<");
}

int getFingerprintID() {
  //This function checks for a fingerprint and returns its ID
  if (finger.getImage() != FINGERPRINT_OK) return -1;
  if (finger.image2Tz() != FINGERPRINT_OK) return -1;
  if (finger.fingerSearch() != FINGERPRINT_OK) return -1;
  return finger.fingerID;
}

uint8_t enrollFingerprint(uint8_t id) {
  //This function saves the fingerprint and returns its ID
  int p = -1;
  Serial.println("Waiting for finger...");

  while ((p = finger.getImage()) != FINGERPRINT_OK) delay(50);
  if (finger.image2Tz(1) != FINGERPRINT_OK) return p;
  Serial.println("Remove finger");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(50);
  Serial.println("Place same finger again");
  while ((p = finger.getImage()) != FINGERPRINT_OK) delay(50);
  if (finger.image2Tz(2) != FINGERPRINT_OK) return p;
  if (finger.createModel() != FINGERPRINT_OK) return p;
  return finger.storeModel(id);
}

bool recordCollection(int id) {
  if (WiFi.status() != WL_CONNECTED) return false; //checks wifi status.

  HTTPClient http;
  String endpoint = String(supabaseURL) + "/food_collections";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");

  String payload = "{\"staffid\":" + String(id) + "}";
  int code = http.POST(payload); //pushes data to the supabase
  String response = http.getString();
  
  Serial.print("HTTP Response code: ");
  Serial.println(code);
  Serial.println("Response: " + response);

  http.end();
  return code == 201;
}

void addStaff(int id, String name, int tag) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(supabaseURL) + "/staff");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));

  String json = "{\"staffid\":" + String(id) + ",\"staffname\":\"" + name + "\",\"tag\":" + tag + ",\"fingerprint_id\":" + id + "}";
  http.POST(json); //pushes data to the supabase
  http.end();
}

String getTodayDate(){
  //Literally does what the function is named after.
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "";

  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
  return String(dateStr);
}

bool preventDoubleCollection(int staffid){
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(supabaseURL) +
               "/food_collections?staffid=eq." + String(staffid) +
               "&select=timecollected";

  http.begin(url);
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));

  int code = http.GET();
  String response = http.getString();
  http.end();

  if (code == 200) {
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, response);

    for (JsonObject obj : doc.as<JsonArray>()) {
      String ts = obj["timecollected"];
      if (ts.startsWith(getTodayDate())) {
        return true; // already collected today
      }
    }
  }

  return false; // not found or error
}

int getNextAvailableFingerprintID(){
  //auto assigns the id to store fingerprint templates.
  for (int id = 1; id <= 127; id++) {
    if (finger.loadModel(id) != FINGERPRINT_OK) {
      return id; 
    }
  }
  return -1;
}

int getNextAvailableTag(){
  //Auto assigns tag values from 101 upwards.
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(supabaseURL) + "/staff?select=tag&order=tag.asc&limit=1";

  http.begin(url);
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));

  int httpCode = http.GET();
  int nextTag = 101;  //Starting Tag Number

  if (httpCode == 200) {
    String response = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);

    if (!error && doc.size() > 0) {
      int lastTag = doc[0]["tag"];
      nextTag = lastTag + 1;
    }
  } else {
    Serial.println("Failed to fetch tag from Supabase: " + String(httpCode));
  }

  http.end();
  return nextTag;
}

void setup() {
  Serial.begin(9600); //port communication
  serialPort.begin(57600, SERIAL_8N1, 16, 17); 
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  finger.begin(57600); //baud rate for the sensor.
  if (!finger.verifyPassword()) {
    Serial.println("Fingerprint sensor not found.");
    while (1) delay(10);
  }

  connectWiFi(); //connect WiFi
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); //local Time and date
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
  } else {
    Serial.println("Time synced");
  }
  Serial.println(">> COLLECTION MODE <<");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) toggleMode();
  
  //Mode Switch for staff registration
  if (currentMode == REGISTRATION_MODE) {
    Serial.println("Enter staff name: ");
    while (!Serial.available()) {
      if (digitalRead(BUTTON_PIN) == LOW) {
        toggleMode();
        return;
      }
    }
    //User Input
    String name = Serial.readStringUntil('\n');
    name.trim();

    int id = getNextAvailableFingerprintID();
    int tag = getNextAvailableTag();

    if(id == -1 || tag == -1){
      Serial.println("Error assigning ID or Tag");
      return;
    }

    Serial.println("Registering " + name + " | ID: " + String(id) + "| Tag: " + String(tag));
    if (enrollFingerprint(id) == FINGERPRINT_OK) {
      Serial.println("Enrollment successful");
      addStaff(id, name, tag); //stores the staff details.
    } else {
      Serial.println("Enrollment failed");
    }

    while (Serial.available()) Serial.read();
    currentMode = COLLECTION_MODE;
  }
  else { //pretty straight forward - Handles the Collectionn process.
    int id = getFingerprintID();
    if (id >= 0) {
      Serial.println("Fingerprint ID: " + String(id));
      
      if (preventDoubleCollection(id)) {
        Serial.println("Already collected today.");
      } else if(recordCollection(id)) {
        Serial.println("Collection recorded");
      } else {
        Serial.println("Failed to record collection");
      }
      delay(2000);
    }
  }
}
