#include <Wire.h>
#include <WiFi.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>

#define BUTTON_PIN 0

bool isRegistrationMode = false;

HardwareSerial serialPort(2); // use UART2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialPort);

// WiFi settings
const char* ssid = "iPhone 14 Plus";
const char* password = "jbgadget2023";

// Supabase settings
const char* supabaseURL = "https://cskdjbpsiupasdhynazt.supabase.co/rest/v1/staff";
const char* supabaseKey = "ey..."; // truncated for security

void networkSetup() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected to WiFi");
}

void setup() {
  Serial.begin(9600);
  serialPort.begin(57600, SERIAL_8N1, 16, 17); // Customize RX/TX pins if needed

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!finger.begin()) {
    Serial.println("Could not find fingerprint sensor :(");
    while (1) delay(1);
  }

  networkSetup();

  Serial.println("Fingerprint sensor ready");
  finger.getTemplateCount();
  Serial.print("Stored templates: ");
  Serial.println(finger.templateCount);
}

void loop() {
  // Toggle mode
  if (digitalRead(BUTTON_PIN) == LOW) {
    isRegistrationMode = !isRegistrationMode;
    delay(500); // debounce
    Serial.println(isRegistrationMode ? ">> REGISTRATION MODE <<" : ">> COLLECTION MODE <<");
  }

  if (isRegistrationMode) {
    Serial.println("Enter ID to enroll:");
    while (!Serial.available());
    uint8_t id = Serial.parseInt();
    if (id > 0 && id <= 127) {
      getFingerprintEnroll(id);
    } else {
      Serial.println("Invalid ID.");
    }
    delay(1000);
  } else {
    int id = getFingerprintID();
    if (id >= 0) {
      Serial.print("User ID ");
      Serial.print(id);
      Serial.println(" collected food.");
      // You can call addStaffToSupabase() here if needed
    }
    delay(2000);
  }
}

uint8_t getFingerprintEnroll(uint8_t id) {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { Serial.print("."); continue; }
    else if (p != FINGERPRINT_OK) {
      Serial.println("Error capturing image");
      continue;
    }
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return p;

  Serial.println("Remove finger");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  Serial.println("Place same finger again");
  while (finger.getImage() != FINGERPRINT_OK);
  finger.image2Tz(2);

  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.println("Fingerprints did not match.");
    return p;
  }

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Fingerprint stored!");
  } else {
    Serial.println("Failed to store fingerprint.");
  }

  return p;
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerSearch();
  if (p != FINGERPRINT_OK) {
    Serial.println("No match.");
    return -1;
  }

  return finger.fingerID;
}

void addStaffToSupabase(int staffid, String staffname, int tag, int fingerprint_id) {
  HTTPClient http;
  http.begin(String(supabaseURL));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));

  String jsonData = "{\"staffid\":" + String(staffid) + 
                    ",\"staffname\":\"" + staffname + 
                    "\",\"tag\":" + String(tag) + 
                    ",\"fingerprint_id\":" + String(fingerprint_id) + "}";

  int httpResponseCode = http.POST(jsonData);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Supabase response: " + response);
  } else {
    Serial.println("HTTP POST failed: " + String(httpResponseCode));
  }

  http.end();
}
