/************************************************************
 GeoSurveyor Pro – V3.6 (Dual Storage Edition)
 ESP32 | GPS | Ultrasonic | OLED | SD + SPIFFS | WiFi
 SD Card Primary -> Fallback to Internal Flash
************************************************************/

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <TinyGPSPlus.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <esp_sleep.h>

/* ================= CONFIG ================= */
// #define MOCK_GPS // Comment this out for real field use
const char *GOOGLE_SCRIPT_URL = "xxxxxxxxxxxx";

#define SLEEP_TIMEOUT_MS 120000
#define SAT_BEEP_INTERVAL_MS 60000
#define SEARCH_PING_INTERVAL_MS 5000

/* ================= PINS ================= */
#define TRIG_PIN 26
#define ECHO_PIN 27
#define BUZZER 13
#define BTN_A 15
#define BTN_B 4

// SD SPI (VSPI)
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

#define RXD2 16
#define TXD2 17

/* ================= OLED ================= */
Adafruit_SH1106G display(128, 64, &Wire, -1);

/* ================= HARDWARE OBJECTS ================= */
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
SPIClass sdSPI(VSPI);

/* ================= STATES ================= */
String gpsBuffer = "";
bool gpsHold = false;
double holdLat = 0.0, holdLon = 0.0;
int holdSat = 0;
float radarAngle = 0.0;

Preferences prefs;
WebServer server(80);

// STORAGE STATE
bool storageOK = false;
bool usingSD = false; // True = SD, False = SPIFFS

bool gpsFix = false;
long distanceCM = 0;
int logID = 0;
bool muted = false;
String ssid = "";
String pass = "";

bool wifiConnected = false;
bool wifiConfigMode = false;
unsigned long lastInteractionTime = 0;
unsigned long lastSatBeepTime = 0;
unsigned long lastSearchPingTime = 0;
int satBeepCounter = 0;
unsigned long nextBeepTime = 0;

/* ================= UI ================= */
enum UIState {
  UI_BOOT,
  UI_LIVE,
  UI_MENU,
  UI_CAPTURE,
  UI_GPS_DETAIL,
  UI_LOG_VIEWER,
  UI_STATUS,
  UI_SETTINGS,
  UI_WIFI_CONFIG,
  UI_UPLOAD,
  UI_WIFI_SCAN,
  UI_LOG_DETAIL,
  UI_LOG_POINT_DETAIL
};
UIState uiState = UI_BOOT;

/* ================= MENUS ================= */
const char *menuItems[] = {"Live View", "Capture Mode", "GPS Detail",
                           "View Logs", "Upload Data",  "Status",
                           "Settings",  "Clear Logs"};
const int MENU_COUNT = 8;
int menuIndex = 0;
int menuTop = 0;

const char *setItems[] = {"Toggle Mute", "Connect WiFi", "Config WiFi", "Back"};
const int SET_COUNT = 4;
int setIndex = 0;
int setTop = 0;

int scrollOffset = 0;
int logScrollLine = 0; // Starting line in CSV
int logCursor = 0;     // Selection cursor (0-3)
unsigned long lastScrollTime = 0;
int lastSatCount = 0;
bool lastFix = false;

// Scan Vars
int scanCount = 0;
String scanSSIDs[10];
int scanIndex = 0;
int scanTop = 0;
bool scanOpen[10];

// Graphing
long depthHistory[50];
int depthPtr = 0;
float smoothedDist = 0.0; // Smoothing for distance
bool cloudOK = false;     // Cloud status check
unsigned long lastCloudCheck = 0;

/* ================= BUTTONS ================= */
unsigned long aTime = 0, bTime = 0;
bool aPressed = false, bPressed = false;
#define LONG_PRESS 800

/* ================= BUZZER ================= */
void playTone(unsigned int freq, unsigned long duration) {
  if (!muted)
    tone(BUZZER, freq, duration);
  else
    delay(duration);
}
void bz_tick() {
  playTone(2200, 30);
  delay(40);
}
void bz_ok() {
  playTone(2000, 80);
  delay(100);
}
void bz_double() {
  playTone(2000, 70);
  delay(120);
  playTone(2000, 70);
  delay(120);
}
void bz_enter() {
  playTone(1800, 60);
  delay(80);
  playTone(2200, 120);
  delay(140);
}
void bz_back() {
  playTone(2200, 60);
  delay(80);
  playTone(1600, 120);
  delay(140);
}
void bz_error() {
  playTone(900, 400);
  delay(420);
}
void bz_scifi_search() {
  // Sci-Fi Sweep Up
  for (int f = 800; f < 2500; f += 150) {
    if (!muted)
      tone(BUZZER, f, 30);
    delay(20);
  }
  noTone(BUZZER);
}
void bz_sat_found() { playTone(3500, 20); } // Tiny laser blip
void bz_gps_lock() {
  playTone(1800, 100);
  delay(50);
  playTone(2200, 100);
  delay(50);
  playTone(2800, 150);
}
void bz_sonar_ping() {
  playTone(3200, 20); // Fast high ping
}

void bz_sat_count() { playTone(2800, 40); }
void bz_save() {
  playTone(2000, 100);
  delay(50);
  playTone(2500, 100);
  delay(50);
  playTone(3000, 150);
  delay(50);
}
void bz_sleep() {
  for (int i = 0; i < 3; i++) {
    playTone(1000, 200);
    delay(200);
    playTone(800, 200);
    delay(200);
  }
}

/* ================= NETWORK ================= */
void setupWiFiAP();
void drawHeader();

bool connectToWiFi() {
  String s = ssid;
  String p = pass;

  // Default Hotspot
  if (s == "") {
    s = "Hotspot";
    p = "12345678";
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.printf("SSID: %s\n", s.c_str());
  display.display();

  WiFi.begin(s.c_str(), p.c_str());
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 15) {
    delay(500);
    t++;
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected) {
    // Save as last known good
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    ssid = s;
    pass = p;
  }

  display.setCursor(0, 25);
  display.println(wifiConnected ? "Connected!" : "Failed!");
  display.display();
  delay(1000);

  return wifiConnected;
}

void sendSingleRecord(int id, long dist, double lat, double lon) {
  if (!wifiConnected)
    return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String json = "{\"latitude\":" + String(lat, 6) +
                ",\"longitude\":" + String(lon, 6) +
                ",\"distance\":" + String(dist) + "}";

  Serial.println("Attempting Upload...");
  Serial.print("URL: ");
  Serial.println(GOOGLE_SCRIPT_URL);
  Serial.print("Data: ");
  Serial.println(json);

  http.begin(client, GOOGLE_SCRIPT_URL);
  http.addHeader("Content-Type", "application/json");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpResponseCode = http.POST(json);

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response: " + response);
  } else {
    Serial.print("Error: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void sendBatchRecords(String jsonArray) {
  if (!wifiConnected)
    return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  Serial.println("Attempting Batch Upload...");
  Serial.print("Data: ");
  Serial.println(jsonArray);

  http.begin(client, GOOGLE_SCRIPT_URL);
  http.addHeader("Content-Type", "application/json");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpResponseCode = http.POST(jsonArray);

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    cloudOK = true;
    String response = http.getString();
    Serial.println("Response: " + response);
  } else {
    cloudOK = false;
    Serial.print("Error: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void deleteLogs() {
  fs::FS *fsPtr = usingSD ? (fs::FS *)&SD : (fs::FS *)&SPIFFS;
  if (fsPtr->exists("/geosurvey.csv")) {
    fsPtr->remove("/geosurvey.csv");
  }
  logID = 0;
  bz_error(); // Confirmation sound
}

/* ================= LOGIC ================= */
void logData() {
  if (!storageOK) {
    bz_error();
    return;
  }

  bool writeSuccess = false;

  // Use the appropriate filesystem via pointer
  fs::FS *fsPtr = usingSD ? (fs::FS *)&SD : (fs::FS *)&SPIFFS;
  File f = fsPtr->open("/geosurvey.csv", FILE_APPEND);
  if (f) {
    logID++;
    double lat = gpsHold ? holdLat : gps.location.lat();
    double lon = gpsHold ? holdLon : gps.location.lng();
    int sat = gpsHold ? holdSat : gps.satellites.value();
    f.printf("%d,%ld,%.6f,%.6f,%d\n", logID, distanceCM, lat, lon, sat);
    f.close();
    writeSuccess = true;
  }

  if (writeSuccess) {
    display.clearDisplay();
    display.fillRect(10, 20, 108, 25, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setTextSize(2);
    display.setCursor(30, 25);
    display.println("SAVED!");
    display.display();

    bz_save();
    delay(500);
    display.setTextColor(SH110X_WHITE);

    // Immediate Upload if Connected
    if (wifiConnected) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(20, 25);
      display.println("Uploading...");
      display.display();
      double lat = gpsHold ? holdLat : gps.location.lat();
      double lon = gpsHold ? holdLon : gps.location.lng();
      sendSingleRecord(logID, distanceCM, lat, lon);

      display.clearDisplay();
      display.setCursor(30, 25);
      display.println("SENT!");
      display.display();
      delay(500);
    }
  } else {
    bz_error();
  }
}

/* ================= HELPERS + SETUP ================= */
void uiBegin() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.setTextWrap(false);
}

long readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long d = pulseIn(ECHO_PIN, HIGH, 30000);
  return d ? d * 0.034 / 2 : -1;
}

void registerInteraction() { lastInteractionTime = millis(); }

void goToSleep() {
  uiBegin();
  display.drawRect(10, 20, 108, 30, SH110X_WHITE);
  display.setCursor(35, 30);
  display.println("SLEEPING...");
  display.display();
  bz_sleep();
  delay(1000);
  display.clearDisplay();
  display.display();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_A, 0);
  esp_deep_sleep_start();
}

const char *htmlForm = "<html><body><h1>GeoSurveyor Setup</h1>"
                       "<form action='/save' method='POST'>"
                       "SSID: <br><input type='text' name='ssid'><br>"
                       "PASS: <br><input type='password' name='pass'><br><br>"
                       "<input type='submit' value='Save & Reboot'>"
                       "</form></body></html>";

void handleRoot() { server.send(200, "text/html", htmlForm); }
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    prefs.putString("ssid", server.arg("ssid"));
    prefs.putString("pass", server.arg("pass"));
    server.send(200, "text/html", "Saved. Rebooting...");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing args");
  }
}
void setupWiFiAP() {
  WiFi.softAP("GeoSurveyor-Setup", "12345678");
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  prefs.begin("geo", false);
  muted = prefs.getBool("muted", false);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);

  gpsSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Wire.begin(21, 22);
  display.begin(0x3C, true);

  // 1. TRY SD CARD
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (SD.begin(SD_CS, sdSPI)) {
    usingSD = true;
    storageOK = true;

    // Count existing records to set logID
    if (SD.exists("/geosurvey.csv")) {
      File f = SD.open("/geosurvey.csv", FILE_READ);
      while (f.available()) {
        if (f.read() == '\n')
          logID++;
      }
      if (logID > 0)
        logID--; // Subtract header
      f.close();
    } else {
      File f = SD.open("/geosurvey.csv", FILE_WRITE);
      if (f) {
        f.println("ID,DIST,LAT,LON,SAT");
        f.close();
      }
    }

  }
  // 2. FALLBACK TO SPIFFS
  else {
    Serial.println("SD Failed! Trying SPIFFS...");
    if (SPIFFS.begin(true)) {
      usingSD = false;
      storageOK = true;

      // Count existing records to set logID
      if (SPIFFS.exists("/geosurvey.csv")) {
        File f = SPIFFS.open("/geosurvey.csv", FILE_READ);
        while (f.available()) {
          if (f.read() == '\n')
            logID++;
        }
        if (logID > 0)
          logID--; // Subtract header
        f.close();
      } else {
        File f = SPIFFS.open("/geosurvey.csv", FILE_WRITE);
        if (f) {
          f.println("ID,DIST,LAT,LON,SAT");
          f.close();
        }
      }
    }
  }

  delay(100);
  uiBegin();
  display.setCursor(30, 25);
  display.println("GeoSurveyor");
  display.display();
  bz_ok();

  // Background Auto-connect
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), pass.c_str());
    // Non-blocking attempt check later in loop
  }

  delay(500);
  uiState = UI_LIVE;
  lastInteractionTime = millis();
}

/* ================= DRAWING ================= */
void drawStatusIcon(int x, int y) {
  if (wifiConnected) {
    long rssi = WiFi.RSSI();
    // Signal bars
    display.fillRect(x, y + 6, 2, 2, SH110X_WHITE);
    if (rssi > -80)
      display.fillRect(x + 3, y + 4, 2, 4, SH110X_WHITE);
    if (rssi > -67)
      display.fillRect(x + 6, y + 2, 2, 6, SH110X_WHITE);
    if (rssi > -50)
      display.fillRect(x + 9, y, 2, 8, SH110X_WHITE);
  } else {
    display.drawLine(x, y + 4, x + 4, y + 8, SH110X_WHITE); // Simplified X
    display.drawLine(x + 4, y + 4, x, y + 8, SH110X_WHITE);
  }

  // Cloud Dot
  if (cloudOK) {
    display.fillCircle(x + 15, y + 4, 2, SH110X_WHITE);
  } else {
    display.drawCircle(x + 15, y + 4, 2, SH110X_WHITE);
  }
}
void drawSatIcon(int x, int y) {
  display.fillRect(x + 1, y + 1, 3, 3, SH110X_WHITE);
  display.drawRect(x, y, 5, 5, SH110X_WHITE);
  display.drawPixel(x + 2, y + 2, SH110X_BLACK);
}
void drawHeader() {
  display.drawLine(0, 11, 128, 11, SH110X_WHITE);
  drawStatusIcon(2, 1);
  drawSatIcon(50, 2);
  display.setCursor(58, 2);
  display.printf("%02d", gpsHold ? holdSat : gps.satellites.value());

  display.setCursor(85, 2);
  if (storageOK) {
    if (usingSD)
      display.print("SD");
    else
      display.print("IN");
    display.printf("#%02d", logID);

    // Accuracy Dot
    int accColor = SH110X_WHITE;
    if (!gpsFix)
      accColor = SH110X_BLACK;
    else if (gps.hdop.hdop() > 2.0)
      accColor = SH110X_WHITE;
    display.fillCircle(124, 4, 2, accColor);

  } else
    display.print("NO MEM");
}
void drawFooter(const char *a, const char *b) {
  display.drawLine(0, 54, 128, 54, SH110X_WHITE);
  display.setCursor(2, 56);
  display.print(a);
  int xPos = 127 - (strlen(b) * 6);
  display.setCursor(xPos, 56);
  display.print(b);
}

void printScrolling(const char *text, int x, int y, bool selected) {
  if (!selected) {
    display.setCursor(x, y);
    display.print(text);
    return;
  }
  int len = strlen(text);
  if (len * 6 <= (128 - x)) {
    display.setCursor(x, y);
    display.print(text);
  } else {
    int charOffset = scrollOffset / 6;
    if (charOffset > len) {
      scrollOffset = -30;
      charOffset = 0;
    }
    if (charOffset < 0)
      charOffset = 0;
    String s = String(text) + "   " + text;
    display.setCursor(x, y);
    display.print(s.substring(charOffset));
  }
}
void drawList(const char *title, const char **items, int count, int selected,
              int *topIndex) {
  uiBegin();
  display.println(title);
  display.drawLine(0, 9, 128, 9, SH110X_WHITE);
  const int VISIBLE = 3;
  if (selected < *topIndex)
    *topIndex = selected;
  if (selected >= *topIndex + VISIBLE)
    *topIndex = selected - VISIBLE + 1;
  for (int i = 0; i < VISIBLE; i++) {
    int idx = *topIndex + i;
    if (idx >= count)
      break;
    int y = 14 + (i * 12);
    display.setCursor(0, y);
    if (idx == selected) {
      display.print("> ");
      printScrolling(items[idx], 12, y, true);
    } else {
      display.print("  ");
      printScrolling(items[idx], 12, y, false);
    }
  }
}

/* ================= SCREENS ================= */
void drawMenu() {
  drawList("MAIN MENU", menuItems, MENU_COUNT, menuIndex, &menuTop);
  drawFooter("Sel", "Next");
  display.display();
}
void drawSettings() {
  drawList("SETTINGS", setItems, SET_COUNT, setIndex, &setTop);
  if (setIndex == 0) {
    display.setCursor(80, 14);
    display.print(muted ? "ON" : "OFF");
  }
  if (setIndex == 1) {
    display.setCursor(80, 26);
    display.print(wifiConnected ? "CON" : "DIS");
  }
  drawFooter("Next", "Hold:Set");
  display.display();
}
void drawWiFiConfig() {
  uiBegin();
  display.println("WIFI SETUP");
  display.println("AP: GeoSurveyor");
  display.println("IP: 192.168.4.1");
  drawFooter("-", "Back");
  display.display();
}
void drawLive() {
  uiBegin();
  drawHeader();

  // 1. MAIN DATA ZONE (Depth & Graph)
  display.setTextSize(3);
  int dValue = (int)smoothedDist;
  int dOff = (dValue < 100) ? 18 : 0;
  display.setCursor(5 + dOff, 14);
  display.print(dValue);
  display.setTextSize(1);
  display.print("cm");

  // Sparkline on the right (Vertical: 14 to 36)
  int gx = 80;
  int gy = 36;
  int gh = 22;
  display.drawRect(gx - 1, gy - gh - 1, 46, gh + 2, SH110X_WHITE);
  for (int i = 0; i < 44; i++) {
    int p = (depthPtr + i) % 50;
    int y = gy - map(constrain(depthHistory[p], 0, 500), 0, 500, 0, gh);
    display.drawPixel(gx + i, y, SH110X_WHITE);
  }

  // 2. GPS INFO STRIP (Separator at 38)
  display.drawLine(0, 39, 128, 39, SH110X_WHITE);
  display.setCursor(10, 43);

#ifdef MOCK_GPS
  // Show Fake Mumbai Coordinates for Testing
  display.printf("L:19.0760 O:72.8777 (SIM)");
#else
  if (gps.location.isValid()) {
    display.printf("L:%.4f O:%.4f", gps.location.lat(), gps.location.lng());
  } else {
    display.print("WAITING FOR GPS FIX...");
  }
#endif

  // 3. FOOTER
  drawFooter("Cap", "Menu");
  display.display();
}
void drawLogViewer() {
  uiBegin();
  drawHeader();
  display.setCursor(0, 20);
  display.print("Total Logs: ");
  display.println(logID);
  display.println(usingSD ? "Storage: SD Card" : "Storage: Internal");
  display.setCursor(0, 45);
  display.println("Hold A: View Lines");
  drawFooter("Back", "Deep");
  display.display();
}

void drawLogDetail() {
  uiBegin();
  display.println("SELECT ENTRY");
  display.drawLine(0, 9, 128, 9, SH110X_WHITE);

  fs::FS *fsPtr = usingSD ? (fs::FS *)&SD : (fs::FS *)&SPIFFS;
  File f = fsPtr->open("/geosurvey.csv", FILE_READ);
  if (!f) {
    display.println("File Error!");
    display.display();
    return;
  }

  f.readStringUntil('\n'); // Skip header
  int current = 0;
  int linesShown = 0;

  while (f.available() && linesShown < 4) {
    String line = f.readStringUntil('\n');
    if (line.length() < 5)
      continue;

    if (current >= logScrollLine) {
      int y = 12 + (linesShown * 10);
      if (linesShown == logCursor) {
        display.setCursor(0, y);
        display.print(">");
      }

      int c1 = line.indexOf(',');
      int c2 = line.indexOf(',', c1 + 1);
      int c3 = line.indexOf(',', c2 + 1);

      if (c1 != -1 && c2 != -1) {
        String idStr = line.substring(0, c1);
        String distStr = line.substring(c1 + 1, c2);
        String latStr = (c3 != -1) ? line.substring(c2 + 1, c2 + 6) : "?.?";

        display.setCursor(8, y);
        display.printf("#%s %scm L:%s", idStr.c_str(), distStr.c_str(),
                       latStr.c_str());
        linesShown++;
      }
    }
    current++;
  }
  f.close();

  drawFooter("View", "Down");
  display.display();
}

void drawLogPointDetail() {
  uiBegin();
  display.println("RECORD DETAIL");
  display.drawLine(0, 9, 128, 9, SH110X_WHITE);

  fs::FS *fsPtr = usingSD ? (fs::FS *)&SD : (fs::FS *)&SPIFFS;
  File f = fsPtr->open("/geosurvey.csv", FILE_READ);
  if (!f)
    return;

  f.readStringUntil('\n');
  int targetLine = logScrollLine + logCursor;
  for (int i = 0; i < targetLine; i++)
    f.readStringUntil('\n');

  String line = f.readStringUntil('\n');
  f.close();

  // Parse: ID,DIST,LAT,LON,SAT
  int c1 = line.indexOf(',');
  int c2 = line.indexOf(',', c1 + 1);
  int c3 = line.indexOf(',', c2 + 1);
  int c4 = line.indexOf(',', c3 + 1);

  String sSat = line.substring(c4 + 1);
  sSat.trim();
  display.setCursor(95, 0);
  display.printf("S:%s", sSat.c_str());

  display.setCursor(0, 15);
  display.printf("ID:   #%s", line.substring(0, c1).c_str());
  display.setCursor(0, 25);
  display.printf("DEPTH: %s cm", line.substring(c1 + 1, c2).c_str());
  display.setCursor(0, 35);
  display.printf("LAT:   %s", line.substring(c2 + 1, c3).c_str());
  display.setCursor(0, 45);
  display.printf("LON:   %s", line.substring(c3 + 1, c4).c_str());

  drawFooter("Back", "Exit");
  display.display();
}
void drawCapture() {
  uiBegin();
  drawHeader();
  display.setCursor(0, 20);
  display.printf("DIST: %ld cm\n", distanceCM);
  display.setCursor(0, 32);
  display.printf("SATS: %d ", gps.satellites.value());
  display.print(gpsFix ? "(FIX)" : "(NO FIX)");
  display.drawRect(20, 42, 88, 12, SH110X_WHITE);
  display.setCursor(35, 44);
  display.print("HOLD 'A'");
  drawFooter("Save", "Back");
  display.display();
}
void drawGPSDetail() {
  uiBegin();
  if (gpsHold) {
    display.setCursor(90, 0);
    display.print("[HOLD]");
  }
  display.setCursor(0, 0);
  display.print("Lat: ");
  display.print(gpsHold ? holdLat : gps.location.lat(), 6);

  display.setCursor(0, 12);
  display.print("Lon: ");
  display.print(gpsHold ? holdLon : gps.location.lng(), 6);

  display.setCursor(0, 24);
  display.print("Sat: ");
  display.print(gpsHold ? holdSat : gps.satellites.value());

  display.setCursor(0, 36);
  display.print("HDOP: ");

  if (gps.hdop.hdop() < 1.2)
    display.print(" (PRO)");
  else if (gps.hdop.hdop() < 2.0)
    display.print(" (OK)");
  else
    display.print(" (POOR)");

  int cx = 110;
  int cy = 35;
  int r = 12;
  display.drawCircle(cx, cy, r, SH110X_WHITE);
  if (!gpsHold) {
    int lx = cx + (r - 2) * cos(radarAngle);
    int ly = cy + (r - 2) * sin(radarAngle);
    display.drawLine(cx, cy, lx, ly, SH110X_WHITE);
    radarAngle += 0.2;
    if (radarAngle > 6.28)
      radarAngle = 0;
  }
  display.drawRect(0, 38, 128, 15, SH110X_WHITE);
  display.setCursor(2, 41);
  int len = gpsBuffer.length();
  if (len > 20)
    display.print(gpsBuffer.substring(len - 20));
  else
    display.print(gpsBuffer);
  drawFooter(gpsHold ? "LIVE" : "HOLD", "BACK");
  display.display();
}
void drawStatus() {
  uiBegin();
  display.println("STATUS");
  display.drawLine(0, 9, 128, 9, SH110X_WHITE);
  display.printf("MEM: %s\n", usingSD ? "SD CARD" : "INTERNAL");
  display.printf("GPS: %s\n", gpsFix ? "FIX" : "NO");
  display.printf("MUT: %s\n", muted ? "Y" : "N");
  display.printf("WIFI:%s\n", wifiConnected ? "OK" : "NO");
  drawFooter("-", "Back");
  display.display();
}

void uploadData() {
  if (!wifiConnected) {
    if (!connectToWiFi()) {
      uiBegin();
      display.println("WiFi Fail!");
      display.display();
      delay(1500);
      return;
    }
  }

  uiBegin();
  display.println("Uploading...");
  display.drawRect(10, 30, 108, 10, SH110X_WHITE); // Progress bar container
  display.display();

  fs::FS *fsPtr = usingSD ? (fs::FS *)&SD : (fs::FS *)&SPIFFS;
  File f = fsPtr->open("/geosurvey.csv", FILE_READ);
  if (!f) {
    uiBegin();
    display.println("No File!");
    display.display();
    delay(1500);
    return;
  }

  // Skip Header
  if (f.available()) {
    String header = f.readStringUntil('\n');
  }

  int sent = 0;
  int total = logID;
  String batch = "[";
  int batchCount = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() < 5)
      continue;

    // Parse CSV: ID,DIST,LAT,LON,SAT
    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);
    int comma3 = line.indexOf(',', comma2 + 1);
    int comma4 = line.indexOf(',', comma3 + 1);

    String sDist = line.substring(comma1 + 1, comma2);
    String sLat = line.substring(comma2 + 1, comma3);
    String sLon = line.substring(comma3 + 1, comma4);

    if (batchCount > 0)
      batch += ",";
    batch += "{\"latitude\":" + sLat + ",\"longitude\":" + sLon +
             ",\"distance\":" + sDist + "}";
    batchCount++;
    sent++;

    if (batchCount >= 10) {
      batch += "]";
      uiBegin();
      display.printf("Syncing... %d/%d\n", sent, total);
      int barW = map(sent, 0, total, 0, 106);
      display.drawRect(10, 30, 108, 10, SH110X_WHITE);
      display.fillRect(11, 31, barW, 8, SH110X_WHITE);
      display.display();

      sendBatchRecords(batch);
      batch = "[";
      batchCount = 0;
      delay(300); // Polite delay
    }
  }

  // Final partial batch
  if (batchCount > 0) {
    batch += "]";
    sendBatchRecords(batch);
  }

  f.close();

  uiBegin();
  display.println("Upload Done!");
  display.printf("Total: %d\n", sent);
  display.display();
  bz_save();
  delay(2000);
  uiState = UI_MENU;
}

void scanNetworks() {
  uiBegin();
  display.println("Scanning...");
  display.display();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  scanCount = n;
  if (scanCount > 10)
    scanCount = 10;
  for (int i = 0; i < scanCount; ++i) {
    scanSSIDs[i] = WiFi.SSID(i);
    scanOpen[i] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
}

void drawWiFiScan() {
  uiBegin();
  display.println("SELECT WIFI");
  display.drawLine(0, 9, 128, 9, SH110X_WHITE);

  const int VISIBLE = 3;
  if (scanIndex < scanTop)
    scanTop = scanIndex;
  if (scanIndex >= scanTop + VISIBLE)
    scanTop = scanIndex - VISIBLE + 1;

  if (scanCount == 0) {
    display.setCursor(0, 20);
    display.println("No Networks");
  } else {
    for (int i = 0; i < VISIBLE; i++) {
      int idx = scanTop + i;
      if (idx >= scanCount)
        break;
      int y = 14 + (i * 12);
      display.setCursor(0, y);
      if (idx == scanIndex) {
        display.print("> ");
        String s = scanSSIDs[idx];
        if (scanOpen[idx])
          s += " *";
        if (s.length() > 14)
          s = s.substring(0, 14);
        display.print(s);
      } else {
        display.print("  ");
        String s = scanSSIDs[idx];
        if (scanOpen[idx])
          s += " *";
        if (s.length() > 14)
          s = s.substring(0, 14);
        display.print(s);
      }
    }
  }
  drawFooter("Conn", "Next");
  display.display();
}

/* ================= LOOP ================= */
void handleButtons() {
  // Use shorter long press for better responsiveness
  const int LP_TIME = 600;

  bool curA = (digitalRead(BTN_A) == LOW);
  if (curA && !aPressed) {
    aPressed = true;
    aTime = millis();
    registerInteraction();
  }
  if (!curA && aPressed) {
    aPressed = false;
    unsigned long duration = millis() - aTime;

    // Logic Split
    if (duration < LP_TIME) {
      // SHORT PRESS: ENTER / SELECT / ACTION
      bz_ok();
      switch (uiState) {
      case UI_LIVE:
        uiState = UI_CAPTURE;
        break;
      case UI_MENU:
        if (menuIndex == 0)
          uiState = UI_LIVE;
        else if (menuIndex == 1)
          uiState = UI_CAPTURE;
        else if (menuIndex == 2)
          uiState = UI_GPS_DETAIL;
        else if (menuIndex == 3)
          uiState = UI_LOG_VIEWER;
        else if (menuIndex == 4)
          uploadData(); // Direct call
        else if (menuIndex == 5)
          uiState = UI_STATUS;
        else if (menuIndex == 6)
          uiState = UI_SETTINGS;
        else if (menuIndex == 7) {
          deleteLogs();
          uiState = UI_MENU;
        }
        if (uiState != UI_MENU)
          bz_enter(); // Play sound if changed
        break;
      case UI_GPS_DETAIL:
        gpsHold = !gpsHold;
        if (gpsHold) {
          holdLat = gps.location.lat();
          holdLon = gps.location.lng();
          bz_double();
        }
        break;
      case UI_SETTINGS:
        setIndex = (setIndex + 1) % SET_COUNT;
        bz_tick();
        break;
      case UI_CAPTURE:
        logData();
        break;
      case UI_WIFI_SCAN:
        // A Selects Network
        if (scanCount > 0) {
          String selected = scanSSIDs[scanIndex];
          if (scanOpen[scanIndex]) {
            ssid = selected;
            pass = "";
          } else if (selected == "Hotspot") {
            ssid = "Hotspot";
            pass = "12345678";
          } else {
            ssid = selected;
          }
          connectToWiFi();
          bz_double();
          uiState = UI_SETTINGS;
        }
        break;
      case UI_LOG_VIEWER:
        uiState = UI_MENU; // A Short: Back to Menu
        bz_back();
        break;
      case UI_LOG_DETAIL:
        uiState = UI_LOG_POINT_DETAIL; // Select Entry
        bz_enter();
        break;
      case UI_LOG_POINT_DETAIL:
        uiState = UI_LOG_DETAIL; // Back to List
        bz_back();
        break;
      default:
        break;
      }
    } else {
      // LONG PRESS
      if (uiState == UI_LOG_VIEWER) {
        uiState = UI_LOG_DETAIL;
        logScrollLine = 0;
        bz_enter();
      }
      if (uiState == UI_SETTINGS) {
        if (setIndex == 0) {
          muted = !muted;
          prefs.putBool("muted", muted);
          bz_double();
        }
        if (setIndex == 1) {
          // Trigger Scan instead of immediate connect
          scanNetworks();
          scanIndex = 0;
          scanTop = 0;
          uiState = UI_WIFI_SCAN;
          bz_enter();
        }
        if (setIndex == 2) {
          setupWiFiAP();
          wifiConfigMode = true;
          uiState = UI_WIFI_CONFIG;
        }
        if (setIndex == 3) {
          uiState = UI_MENU;
          bz_back();
        }
      }
    }
  }

  bool curB = (digitalRead(BTN_B) == LOW);
  if (curB && !bPressed) {
    bPressed = true;
    bTime = millis();
    registerInteraction();
  }
  if (!curB && bPressed) {
    bPressed = false;
    bz_tick();

    // B BUTTON: NEXT / BACK
    if (uiState == UI_MENU) {
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      scrollOffset = 0;
    } else if (uiState == UI_WIFI_CONFIG) {
      WiFi.softAPdisconnect(true);
      wifiConfigMode = false;
      uiState = UI_SETTINGS;
      bz_back();
    } else if (uiState == UI_LOG_VIEWER) {
      uiState = UI_LOG_DETAIL;
      logScrollLine = 0;
      bz_enter();
    } else if (uiState == UI_LOG_VIEWER) {
      uiState = UI_LOG_DETAIL;
      logScrollLine = 0;
      bz_enter();
      return; // Exit to avoid immediate scroll in next else if
    } else if (uiState == UI_LOG_DETAIL) {
      logCursor++;
      if (logCursor >= 4 || (logScrollLine + logCursor) >= logID) {
        if ((logScrollLine + 4) < logID) {
          logScrollLine += 4;
          logCursor = 0;
        } else {
          logScrollLine = 0;
          logCursor = 0;
        }
      }
      bz_tick();
    } else if (uiState == UI_LOG_POINT_DETAIL) {
      uiState = UI_MENU; // Full exit
      bz_back();
    } else if (uiState == UI_WIFI_SCAN) {
      scanIndex = (scanIndex + 1) % (scanCount > 0 ? scanCount : 1);
      bz_tick();
    } else {
      // Back to Menu from sub-screens
      uiState = UI_MENU;
      bz_back();
    }
  }
}

void loop() {
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    gps.encode(c);
  }

#ifdef MOCK_GPS
  gpsFix = true;
#else
  gpsFix = gps.location.isValid();
#endif
  distanceCM = readDistance();

  // EMA Smoothing: dist = (current * 0.3) + (old * 0.7)
  if (smoothedDist <= 0)
    smoothedDist = distanceCM;
  else
    smoothedDist = (distanceCM * 0.3) + (smoothedDist * 0.7);

  // Update Graph (Sparkline)
  if (millis() % 200 < 30) {
    depthHistory[depthPtr] = (long)smoothedDist;
    depthPtr = (depthPtr + 1) % 50;
  }

  if (millis() - lastScrollTime > 150) {
    scrollOffset++;
    lastScrollTime = millis();
  }
  if (millis() - lastInteractionTime > SLEEP_TIMEOUT_MS)
    goToSleep();

  // Background WiFi Monitor
  if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    bz_double();
  }

  // Sci-Fi Audio Logic
  if (gpsFix && !lastFix) {
    bz_gps_lock();
  }
  lastFix = gpsFix;

  int currentSats = gps.satellites.value();
  if (currentSats > lastSatCount) {
    bz_sat_found();
  }
  lastSatCount = currentSats;

  // Audio Tasks
  if (millis() - lastSatBeepTime > SAT_BEEP_INTERVAL_MS) {
    satBeepCounter = gps.satellites.value();
    lastSatBeepTime = millis();
    nextBeepTime = millis();
  }
  if (satBeepCounter > 0 && millis() > nextBeepTime) {
    bz_sat_count();
    satBeepCounter--;
    nextBeepTime = millis() + 150;
  }
  if (!gpsFix && uiState == UI_GPS_DETAIL &&
      millis() - lastSearchPingTime > 2000) {
    bz_scifi_search();
    lastSearchPingTime = millis();
  }

  if (wifiConfigMode)
    server.handleClient();
  handleButtons(); // Always handle buttons

  // 10s Auto-Return to Live from Menu
  if (uiState != UI_LIVE && uiState != UI_BOOT && uiState != UI_WIFI_CONFIG &&
      !wifiConfigMode) {
    if (millis() - lastInteractionTime > 10000) {
      uiState = UI_LIVE;
      bz_back();
    }
  }

  switch (uiState) {
  case UI_MENU:
    drawMenu();
    break;
  case UI_LIVE:
    drawLive();
    break;
  case UI_CAPTURE:
    drawCapture();
    break;
  case UI_GPS_DETAIL:
    drawGPSDetail();
    break;
  case UI_SETTINGS:
    drawSettings();
    break;
  case UI_STATUS:
    drawStatus();
    break;
  case UI_LOG_VIEWER:
    drawLogViewer();
    break;
  case UI_WIFI_CONFIG:
    drawWiFiConfig();
    break;
  case UI_WIFI_SCAN:
    drawWiFiScan();
    break;
  case UI_LOG_DETAIL:
    drawLogDetail();
    break;
  case UI_LOG_POINT_DETAIL:
    drawLogPointDetail();
    break;
  default:
    break;
  }
  delay(30);
}
