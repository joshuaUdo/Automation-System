#include <TFT_eSPI.h>
#include <success.h>
#include <failed.h>
#include <3rd_one.h>
#include <2nd_one.h>
#include <1st_one.h>
#include <try_again.h>
#include <font.h>
#include <fingerprint.h>
#include <placefinger.h>

TFT_eSPI tft = TFT_eSPI();
String incoming = "";

void handleCommand(String cmd) {
  cmd.trim();  // Clean up any whitespace
  tft.fillScreen(TFT_BLACK);

  if (cmd == "show_register_1") {
    tft.pushImage(60, 40, 120, 120, placefinger);
    tft.drawString("Register Finger", 60, 180);

  } else if (cmd == "show_register_2") {
    tft.pushImage(60, 40, 120, 120, placefinger);
    tft.drawString("Place Again", 80, 180);

  } else if (cmd == "show_scanning_1") {
    tft.pushImage(60, 40, 120, 120, scanOne);
    tft.drawString("Scanning...", 80, 180);

  } else if (cmd == "show_scanning_2") {
    tft.pushImage(60, 40, 120, 120, scanTwo);
    tft.drawString("Scanning 2", 90, 180);

  } else if (cmd == "show_scanning_3") {
    tft.pushImage(60, 40, 120, 120, scanthree);
    tft.drawString("Scanning 3", 90, 180);

  } else if (cmd == "show_collection_1") {
    tft.pushImage(60, 40, 120, 120, Fingerprint);
    tft.drawString("Collecting...", 70, 180);

  } else if (cmd == "show_collection_2") {
    tft.pushImage(60, 40, 120, 120, Fingerprint);
    tft.drawString("Done!", 110, 180);

  } else if (cmd == "show_success") {
    tft.pushImage(60, 40, 120, 120, success);
    tft.drawString("Success", 100, 180);

  } else if (cmd == "show_error") {
    tft.pushImage(60, 40, 120, 120, failed);
    tft.drawString("Try Again", 90, 180);

  } else if (cmd.startsWith("show_name:")) {
    String name = cmd.substring(cmd.indexOf(":") + 1);
    tft.drawString("Welcome", 90, 80);
    tft.drawString(name, 60, 130);  // Adjust X if name is too short/long

  } else {
    tft.drawString("Unknown Command", 60, 120);
  }
}

void setup() {
  Serial.begin(115200);  // Optional debug via USB
  Serial2.begin(115200, SERIAL_8N1, 2, 4); // RX, TX connected to main ESP32

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.loadFont("Exo2_VariableFont_wght20pt7bBitmaps");  // Use your font
}

void loop() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      handleCommand(incoming);
      incoming = "";
    } else {
      incoming += c;
    }
  }
}