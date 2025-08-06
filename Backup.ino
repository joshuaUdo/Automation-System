// Automation System for fingerprint-based food collection
// ESP32 as controller, talking to fingerprint sensor (UART1) and smart TFT (UART2)

#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <WiFiClientSecure.h>
#include <Adafruit_Fingerprint.h>

// WiFi credentials
const char* ssid = "iPhone 14 Plus";
const char* password = "jbgadget2023";

// Supabase details
const char* supabase_url = "https://cskdjbpsiupasdhynazt.supabase.co";
const char* supabase_apikey = "YOUR_SUPABASE_API_KEY";

// Pin configuration
#define RX_FP 16
#define TX_FP 17
#define RX_TFT 4
#define TX_TFT 2

// Serial objects
HardwareSerial fingerSerial(1);
HardwareSerial tftSerial(2);
WiFiClientSecure client;
HTTPClient https;

Adafruit_Fingerprint finger(&fingerSerial);

// State
String mode = "collection";
int staffidToRegister = -1;

// Setup functions
void connectToWiFi() {
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED) {
delay(500);
Serial.print(".");
}
Serial.println("WiFi connected");
}

void syncTime() {
configTzTime("WAT-1", "pool.ntp.org", "time.nist.gov");
time_t now = time(nullptr);
int attempts = 0;

while (now < 100000 && attempts++ < 20) {
delay(500);
Serial.print(".");
now = time(nullptr);
}

if (now > 100000) {
struct tm* timeinfo = localtime(&now);
Serial.printf("\nTime synced: %04d-%02d-%02d %02d:%02d:%02d\n",
timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
timeinfo->tm_mday, timeinfo->tm_hour,
timeinfo->tm_min, timeinfo->tm_sec);
} else {
Serial.println("\nTime sync failed.");
}
}

String checkControlMode() {
client.setInsecure();
HTTPClient http;
String url = String(supabase_url) + "/rest/v1/control?select=mode,staffid&processed=eq.false&limit=1";

if (!http.begin(client, url)) return "";
http.addHeader("apikey", supabase_apikey);
http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
http.addHeader("Accept", "application/json");

int responseCode = http.GET();
if (responseCode != 200) {
http.end();
return "";
}

String payload = http.getString();
StaticJsonDocument<512> doc;
DeserializationError error = deserializeJson(doc, payload);
http.end();

if (error || doc.size() == 0) {
mode = "collection";
staffidToRegister = -1;
return mode;
}

JsonObject first = doc[0];
mode = first["mode"] | "";
staffidToRegister = first["staffid"] | -1;
return mode;
}

void updateControlModeToCollection() {
if (staffidToRegister <= 0) return;
HTTPClient http;
String url = String(supabase_url) + "/rest/v1/control?staffid=eq." + String(staffidToRegister);

if (!http.begin(client, url)) return;
http.addHeader("apikey", supabase_apikey);
http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
http.addHeader("Content-Type", "application/json");
http.addHeader("Prefer", "return=minimal");

StaticJsonDocument<256> doc;
doc["processed"] = true;
doc["mode"] = "collection";
String payload;
serializeJson(doc, payload);
http.PATCH(payload);
http.end();
staffidToRegister = -1;
}

bool staffExists(int staffid) {
HTTPClient http;
String url = String(supabase_url) + "/rest/v1/staff?staffid=eq." + String(staffid) + "&select=staffid";
if (!http.begin(client, url)) return false;
http.addHeader("apikey", supabase_apikey);
http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
int code = http.GET();
bool exists = (code == HTTP_CODE_OK);
http.end();
return exists;
}

bool updateStaffFingerprint(int staffid, int fid) {
HTTPClient http;
String url = String(supabase_url) + "/rest/v1/staff?staffid=eq." + String(staffid);
if (!http.begin(client, url)) return false;
http.addHeader("apikey", supabase_apikey);
http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
http.addHeader("Content-Type", "application/json");
http.addHeader("Prefer", "return=minimal");

StaticJsonDocument<128> doc;
doc["fingerprintid"] = fid;
String body;
serializeJson(doc, body);
int res = http.PATCH(body);
http.end();
return (res == HTTP_CODE_OK || res == HTTP_CODE_NO_CONTENT);
}

int findNextAvailableID() {
for (int id = 1; id <= 127; id++) {
if (finger.loadModel(id) != FINGERPRINT_OK) {
return id;
}
}
return -1;
}

bool enrollFingerprintWithTimeout(int staffid, unsigned long timeout = 30000) {
if (!staffExists(staffid)) return false;
int id = findNextAvailableID();
if (id == -1) return false;

tftSerial.println("page enroll");
tftSerial.println("t0.txt=\"Place finger...\"");

unsigned long start = millis();
uint8_t p;

while ((p = finger.getImage()) != FINGERPRINT_OK) {
if (millis() - start > timeout) return false;
delay(100);
}
if (finger.image2Tz(1) != FINGERPRINT_OK) return false;

tftSerial.println("t0.txt=\"Remove finger\"");
while (finger.getImage() != FINGERPRINT_NOFINGER) {
if (millis() - start > timeout) return false;
delay(50);
}

tftSerial.println("t0.txt=\"Place again\"");
while ((p = finger.getImage()) != FINGERPRINT_OK) {
if (millis() - start > timeout) return false;
delay(100);
}
if (finger.image2Tz(2) != FINGERPRINT_OK) return false;
if (finger.createModel() != FINGERPRINT_OK) return false;
if (finger.storeModel(id) != FINGERPRINT_OK) return false;

return updateStaffFingerprint(staffid, id);
}

int getStaffIdByFingerprint(int fid) {
HTTPClient http;
String url = String(supabase_url) + "/rest/v1/staff?fingerprintid=eq." + String(fid) + "&select=staffid";
if (!http.begin(client, url)) return -1;
http.addHeader("apikey", supabase_apikey);
http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
int code = http.GET();
if (code == HTTP_CODE_OK) {
String payload = http.getString();
JsonDocument doc;
DeserializationError error = deserializeJson(doc, payload);
if (!error && doc.size() > 0) return doc[0]["staffid"];
}
http.end();
return -1;
}

bool hasCollectedToday(int staffid) {
String url = String(supabase_url) + "/rest/v1/food_collections?staffid=eq." + staffid +
"&select=time_collected&order=time_collected.desc&limit=1";
https.begin(client, url);
https.addHeader("apikey", supabase_apikey);
https.addHeader("Authorization", "Bearer " + String(supabase_apikey));

int httpCode = https.GET();
if (httpCode > 0) {
String payload = https.getString();
DynamicJsonDocument doc(1024);
deserializeJson(doc, payload);
if (doc.size() > 0) {
const char* createdAt = doc[0]["time_collected"];
struct tm tm;
if (strptime(createdAt, "%Y-%m-%dT%H:%M:%S", &tm)) {
time_t lastTime = mktime(&tm);
time_t now = time(nullptr);
struct tm* nowTm = localtime(&now);
struct tm* lastTm = localtime(&lastTime);
if (nowTm->tm_year == lastTm->tm_year && nowTm->tm_yday == lastTm->tm_yday) return true;
}
}
}
https.end();
return false;
}

void verifyFingerprintAndLog() {
uint8_t p = finger.getImage();
if (p != FINGERPRINT_OK) return;

p = finger.image2Tz();
if (p != FINGERPRINT_OK) return;

p = finger.fingerFastSearch();
if (p != FINGERPRINT_OK) return;

int fid = finger.fingerID;
int staffid = getStaffIdByFingerprint(fid);
if (staffid == -1) return;

if (hasCollectedToday(staffid)) {
tftSerial.println("page warning");
tftSerial.println("t0.txt=\"Already Collected\"");
return;
}

HTTPClient http;
String url = String(supabase_url) + "/rest/v1/food_collections";
if (!http.begin(client, url)) return;

http.addHeader("apikey", supabase_apikey);
http.addHeader("Authorization", String("Bearer ") + supabase_apikey);
http.addHeader("Content-Type", "application/json");
http.addHeader("Prefer", "return=minimal");

time_t now = time(nullptr);
struct tm* timeinfo = localtime(&now);
char timeStr[25];
strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S+01:00", timeinfo);

JsonDocument doc;
doc["fingerprintid"] = fid;
doc["staffid"] = staffid;
doc["time_collected"] = timeStr;

String body;
serializeJson(doc, body);
int code = http.POST(body);
http.end();

if (code == HTTP_CODE_CREATED) {
tftSerial.println("page success");
tftSerial.println("t0.txt=\"Collection logged\"");
}
}

void setup() {
Serial.begin(115200);
fingerSerial.begin(57600, SERIAL_8N1, RX_FP, TX_FP);
tftSerial.begin(9600, SERIAL_8N1, RX_TFT, TX_TFT);
connectToWiFi();
syncTime();
if (!finger.verifyPassword()) {
Serial.println("Fingerprint sensor not found.");
while (true) delay(10);
}
tftSerial.println("page home");
}

void loop() {
static unsigned long lastCheck = 0;
if (millis() - lastCheck >= 3000) {
lastCheck = millis();
checkControlMode();
}

if (mode == "register" && staffidToRegister > 0) {
bool enrolled = enrollFingerprintWithTimeout(staffidToRegister);
if (enrolled) {
tftSerial.println("page success");
tftSerial.println("t0.txt=\"Registered\"");
} else {
tftSerial.println("page warning");
tftSerial.println("t0.txt=\"Timeout or error\"");
}
updateControlModeToCollection();
staffidToRegister = -1;
mode = "collection";
} else if (mode == "collection") {
verifyFingerprintAndLog();
delay(500);
}

if (WiFi.status() != WL_CONNECTED) {
connectToWiFi();
}
}
