#include <TFT_eSPI.h>

//Import your images here.
#include <success.h>
#include <failed.h>
#include <3rd_one.h>
#include <2nd_one.h>
#include <1st_one.h>
#include <try_again.h>
#include <font.h>
#include <fingerprint.h>
#include <placefinger.h>

#define DISPLAYRX 16
#define DISPLAYTX 17

TFT_eSPI tft = TFT_eSPI();
String incoming = "";
HardwareSerial displaySerial(2);

unsigned long lastUpdateTime = 0;
unsigned long displayDuration = 10000;
bool shouldClear = false;

void handleCommand(String cmd) {
  cmd.trim();  // Clean up any whitespace
  tft.fillScreen(TFT_BLACK);

  if (cmd == "show_register_1") {
    tft.pushImage(60, 40, 72, 80, placefinger);
    tft.drawString("Register Finger", 60, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_register_2") {
    tft.pushImage(60, 40, 72, 80, placefinger);
    tft.drawString("Place Again", 80, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_scanning_1") {
    tft.pushImage(60, 40, 84, 141, scanOne);
    tft.drawString("Scanning...", 80, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_scanning_2") {
    tft.pushImage(60, 40, 86, 108, scanTwo);
    tft.drawString("Scanning...", 90, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_scanning_3") {
    tft.pushImage(60, 40, 86, 108, scanthree);
    tft.drawString("Scanning...", 90, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_collection_1") {
    tft.pushImage(60, 40, 72, 80, Fingerprint);
    tft.drawString("Collecting...", 70, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_collection_2") {
    tft.pushImage(60, 40, 72, 80, Fingerprint);
    tft.drawString("Done!", 110, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_success") {
    tft.pushImage(60, 40, 100, 100, success);
    tft.drawString("Success", 100, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd == "show_error") {
    tft.pushImage(60, 40, 99, 99, failed);
    tft.drawString("Try Again", 90, 180);
    lastUpdateTime = millis();
    shouldClear = true;

  } else if (cmd.startsWith("show_name:")) {
    String name = cmd.substring(cmd.indexOf(":") + 1);
    tft.drawString("Welcome", 90, 80);
    tft.drawString(name, 60, 130);  // Adjust X if name is too short/long
    lastUpdateTime = millis();
    shouldClear = true;

  } else {
    tft.drawString("Unknown Command", 60, 120);
  }
}

void setup() {
  Serial.begin(115200);  // Optional debug via USB
  displaySerial.begin(115200, SERIAL_8N1, DISPLAYRX, DISPLAYTX ); // RX, TX connected to main ESP32

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  //tft.loadFont("Exo2_VariableFont_wght20pt7bGlyphs");  // Use your font
}

void loop() {
  while (displaySerial.available()) {
    char c = displaySerial.read();
    if (c == '\n') {
      handleCommand(incoming);
      incoming = "";
    } else {
      incoming += c;
    }
  }

  if (shouldClear && millis() - lastUpdateTime > displayDuration) {
    tft.fillScreen(TFT_BLACK);
    shouldClear = false;
  }

}
