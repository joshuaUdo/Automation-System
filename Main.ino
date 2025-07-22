#include <Wire.h>
#include <WiFi.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>

#define BUTTON_PIN 13
#define DEBOUNCE_DELAY 300

#define MAX_RETRIES 3
#define FOOD_COLLECTION_LIMIT 1

bool isRegistrationMode = false;

HardwareSerial serialPort(2); // use UART2
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialPort);

// System states
enum SystemMode {
  COLLECTION_MODE,
  REGISTRATION_MODE
};
SystemMode currentMode = COLLECTION_MODE;
unsigned long lastButtonPress = 0;

// WiFi settings
const char *ssid = "iPhone 14 Plus";
const char *password = "jbgadget2023";

// Supabase settings
const char *supabaseURL = "https://cskdjbpsiupasdhynazt.supabase.co/rest/v1";
const char *supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImNza2RqYnBzaXVwYXNkaHluYXp0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTI3NDM2MTIsImV4cCI6MjA2ODMxOTYxMn0.n5V-Jl2njI3AdzWuXcFjFjqCdD4xdqUf7OCcfEA8Ahg";

void networkSetup(){
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected to WiFi");
}

void checkWiFi(){
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected - attempting to reconnect");
    networkSetup();
  }
}

void checkSensorStorage(){
  finger.begin(57600);
  if (finger.verifyPassword()){
    Serial.println("Found fingerprint sensor!");
    uint16_t count = finger.getTemplateCount();
    Serial.print("Sensor contains "); Serial.print(count); 
    Serial.println(" templates");
    
    // Check sensor storage type
    uint8_t ret = finger.getParameters();
    if (ret == FINGERPRINT_OK) {
      Serial.print("Status register: 0x"); Serial.println(finger.status_reg, HEX);
      Serial.print("Capacity: "); Serial.println(finger.capacity);
      Serial.print("Security level: "); Serial.println(finger.security_level);
    }
  }
}

uint8_t storeFingerprint(uint8_t id) {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);

  // Get first fingerprint image
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER)
    {
      Serial.print(".");
      continue;
    }
    else if (p != FINGERPRINT_OK)
    {
      Serial.println("Error capturing image");
      continue;
    }
  }

  // conver fingerprint image to template
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK)
    return p;

  Serial.println("Remove finger");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER)
    ;

  // Get second fingerprint image
  Serial.println("Place same finger again");
  while (finger.getImage() != FINGERPRINT_OK)
    ;
  finger.image2Tz(2);

  // create Model
  p = finger.createModel();
  if (p != FINGERPRINT_OK)
  {
    Serial.println("Fingerprints did not match.");
    return p;
  }

  // store Model
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Fingerprint stored!");
  }
  else
  {
    Serial.println("Failed to store fingerprint.");
  }

  return p;
}

int getFingerprintID(){
  uint8_t p = finger.getImage();
  switch (p)
  {
  case FINGERPRINT_OK:
    break;
  case FINGERPRINT_NOFINGER:
    return -1; // No finger detected is normal
  case FINGERPRINT_PACKETRECIEVEERR:
    Serial.println("Communication error");
    return -2;
  default:
    Serial.println("Unknown error");
    return -3;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)
  {
    Serial.println("Image conversion failed");
    return -4;
  }

  p = finger.fingerSearch();
  if (p != FINGERPRINT_OK)
  {
    if (p == FINGERPRINT_NOTFOUND)
    {
      Serial.println("No match found");
    }
    else
    {
      Serial.println("Search failed");
    }
    return -1;
  }

  return finger.fingerID;
}

void toggleMode(){
  if (millis() - lastButtonPress < DEBOUNCE_DELAY)
    return;
  lastButtonPress = millis();

  if (currentMode == COLLECTION_MODE)
  {
    currentMode = REGISTRATION_MODE;
    Serial.println("\n>> REGISTRATION MODE <<");
    Serial.println("Enter ID to enroll (1-127):");
  }
  else
  {
    currentMode = COLLECTION_MODE;
    Serial.println("\n>> COLLECTION MODE <<");
    Serial.println("Ready to scan fingerprints...");
  }
}

bool recordFoodCollection(int fingerprintID)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(" No WiFi connection - can't record collection");
    return false;
  }

  HTTPClient http;

  String fullUrl = String(supabaseURL);
  if (!fullUrl.endsWith("/"))
  {
    fullUrl += "/";
  }
  fullUrl += "food_collections";

  String endpoint = String(supabaseURL) + "food_collections";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");

  // Get current timestamp
  String timestamp = "now()"; // Supabase will interpret this

  String jsonData = "{\"fingerprint_id\":" + String(fingerprintID) +
                    ",\"collection_time\":\"" + timestamp + "\"}";

  int httpResponseCode = http.POST(jsonData);

  if (httpResponseCode == 201)
  {
    Serial.println(" Food collection recorded successfully");
    return true;
  }
  else
  {
    Serial.print(" Error recording collection: ");
    Serial.println(httpResponseCode);
    return false;
  }
}

void addStaffToSupabase(int staffid, String staffname, int tag, int fingerprint_id)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected - cannot add staff to Supabase");
    return;
  }

  HTTPClient http;

  String fullUrl = String(supabaseURL);
  if (!fullUrl.endsWith("/"))
  {
    fullUrl += "/";
  }
  fullUrl += "staff";

  http.begin(String(supabaseURL));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));

  String jsonData = "{\"staffid\":" + String(staffid) +
                    ",\"staffname\":\"" + staffname +
                    "\",\"tag\":" + String(tag) +
                    ",\"fingerprint_id\":" + String(fingerprint_id) + "}";

  int httpResponseCode = http.POST(jsonData);

  if (httpResponseCode > 0)
  {
    String response = http.getString();
    Serial.println("Supabase response (" + String(httpResponseCode) + "): " + response);
  }
  else
  {
    Serial.println("Error in HTTP request: " + String(httpResponseCode));
    if (httpResponseCode == -1)
    {
      Serial.println("Is the Supabase URL and API key correct?");
    }
  }

  http.end();
}

void setup(){
  Serial.begin(9600);
  while (!Serial)
    ;
  serialPort.begin(57600, SERIAL_8N1, 16, 17);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Verify sensor connection!
  finger.begin(57600);
  if (finger.verifyPassword())
  {
    Serial.println("Found fingerprint sensor!");
  }
  else
  {
    Serial.println("Did not find fingerprint sensor");
    while (1)
    {
      delay(1);
    }
  }

  checkWiFi();
  checkSensorStorage();

  bool verifyFingerprintAndStaff(int &matchedID);
  void collectionMode();

  Serial.println("\nSystem initialized");
  Serial.print("Fingerprint templates stored: ");
  Serial.println(finger.templateCount);
  Serial.println(">> COLLECTION MODE <<");
  Serial.println("Press button to switch modes");
}

void loop(){
  // Handle mode toggle with improved debouncing
  static unsigned long lastDebounceTime = 0;
  if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastDebounceTime) > DEBOUNCE_DELAY)
  {
    lastDebounceTime = millis();
    toggleMode();
    return;
  }

  if (currentMode == REGISTRATION_MODE)
  {
    Serial.println("\n=== Registration Mode ===");
    Serial.println("Enter staff details in this format:");
    Serial.println("ID,Name,Tag (e.g., \"101,John Doe,456\")");
    Serial.println("Or just ID for default values (e.g., \"101\")");

    while (!Serial.available())
    {
      delay(100);
      if (digitalRead(BUTTON_PIN) == LOW)
      { // Allow mode switch while waiting
        toggleMode();
        return;
      }
    }

    String input = Serial.readStringUntil('\n');
    input.trim();

    // Parse input
    int id = 0;
    String name = "Staff " + String(id);
    int tag = 0;

    // Try to parse CSV input
    int firstComma = input.indexOf(',');
    if (firstComma != -1)
    {
      // Extended format: ID,Name,Tag
      int secondComma = input.indexOf(',', firstComma + 1);

      id = input.substring(0, firstComma).toInt();
      if (secondComma != -1)
      {
        name = input.substring(firstComma + 1, secondComma);
        tag = input.substring(secondComma + 1).toInt();
      }
      else
      {
        name = input.substring(firstComma + 1);
      }
    }
    else
    {
      // Simple format: just ID
      id = input.toInt();
      name = "Staff " + input;
    }

    // Validate ID
    if (id <= 0 || id > 127)
    {
      Serial.println("Error: ID must be between 1-127");
      return;
    }

    // Enroll fingerprint
    Serial.println("Ready to enroll fingerprint for ID: " + String(id));
    uint8_t result = getFingerprintEnroll(id);

    if (result == FINGERPRINT_OK)
    {
      Serial.println("Enrollment successful! Adding to database...");

      // Add to Supabase
      if (WiFi.status() == WL_CONNECTED)
      {
        addStaffToSupabase(id, name, tag, id);
      }
      else
      {
        Serial.println("Warning: Not connected to WiFi. Staff not added to database.");
        Serial.println("Fingerprint stored locally but will need cloud sync later.");
      }
    }
    else
    {
      Serial.println("Enrollment failed with error code: " + String(result));
    }

    // Clear any remaining serial input
    while (Serial.available())
      Serial.read();
  }
  else
  { // COLLECTION_MODE
    int fingerprintID = getFingerprintID();
    if (fingerprintID >= 0)
    {
      Serial.print("User #");
      Serial.print(fingerprintID);
      Serial.println(" collected food");

      if (WiFi.status() == WL_CONNECTED)
      {
        recordFoodCollection(fingerprintID);
      }
      else
      {
        Serial.println("Warning: Not connected to WiFi. Collection not recorded in database.");
      }

      delay(2000); // Prevent multiple reads
    }
  }
}

bool verifyFingerprintAndStaff(int &matchedID){
  matchedID = getFingerprintID();
  if (matchedID < 0) {
    Serial.println("No fingerprint match");
    return false;
  }

  HTTPClient http;
  String endpoint = String(supabaseURL) + "/staff?fingerprint_id=eq." + String(matchedID);
  http.begin(endpoint);
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));

  int httpCode = http.GET();
  if(httpCode != 200){
    Serial.println("Staff verfication failed");
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  if(payload == "[]"){
    Serial.println("Staff not registered");
    return false;
  }

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, payload);
  int collectionsToday = doc.size();

  if (collectionsToday >= FOOD_COLLECTION_LIMIT) {
    Serial.print("Staff already collected");
    Serial.print(collectionsToday);
    Serial.println(" meals today");
    return false;
  }
  return true;
}

void collectionMode(){
  int matchedID = -1;
  if (verifyFingerprintAndStaff(matchedID)) {
    Serial.print("Verified staff #");
    Serial.println(matchedID);
    
    if (recordFoodCollection(matchedID)) {
      Serial.println("Meal recorded successfully");
    } else {
      Serial.println("Failed to record meal");
    }
  } else {
    Serial.println("Verification failed");
  }
  delay(2000);
}

