/************************************************************
 * GeoSurveyor Pro – Hardware Diagnostic
 * This script tests all components sequentially to ensure
 * hardware is correctly wired and functional.
 ************************************************************/

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Wire.h>

/* ================= PINS (Matched to Main.ino) ================= */
#define TRIG_PIN 26
#define ECHO_PIN 27
#define BUZZER 13
#define BTN_A 15
#define BTN_B 4
#define SD_CS 5
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23
#define RXD2 16
#define TXD2 17

Adafruit_SH1106G display(128, 64, &Wire, -1);
SPIClass sdSPI(VSPI);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- GeoSurveyor Pro Hardware Diagnostic ---");

  // 1. OLED Test
  Wire.begin(21, 22);
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED Failed!");
  } else {
    Serial.println("OLED OK");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("DIAGNOSTIC MODE");
    display.display();
  }

  // 2. Buzzer & Buttons
  pinMode(BUZZER, OUTPUT);
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  tone(BUZZER, 2000, 100);
  Serial.println("Buzzer Beeped. Press BTN_A or BTN_B to test.");

  // 3. SD Card
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSPI)) {
    Serial.println("SD Card: OK");
  } else {
    Serial.println("SD Card: FAILED");
  }

  // 4. SPIFFS
  if (SPIFFS.begin(true)) {
    Serial.println("SPIFFS: OK");
  } else {
    Serial.println("SPIFFS: FAILED");
  }

  // 5. Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.println("Ultrasonic ready.");

  // 6. GPS Serial
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("GPS Serial (Serial2) opened at 9600.");
}

void loop() {
  // Test Button A
  if (digitalRead(BTN_A) == LOW) {
    Serial.println("Button A Pressed!");
    tone(BUZZER, 1000, 50);
    delay(200);
  }

  // Test Button B
  if (digitalRead(BTN_B) == LOW) {
    Serial.println("Button B Pressed!");
    tone(BUZZER, 1500, 50);
    delay(200);
  }

  // Test Ultrasonic
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  long distance = duration * 0.034 / 2;

  // Update Display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("DIAGNOSTIC");
  display.printf("Dist: %ld cm\n", distance);
  display.printf("GPS raw: %d bytes\n", Serial2.available());

  if (Serial2.available()) {
    char c = Serial2.read();
    // Potentially print NMEA to serial for debugging
  }

  display.display();
  delay(100);
}
