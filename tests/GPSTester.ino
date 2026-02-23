/************************************************************
 * GeoSurveyor Pro – GPS Satellite Tester
 * Use this to verify GPS NMEA data and signal strength.
 ************************************************************/

#include <TinyGPS++.h> // Ensure TinyGPSPlus is installed

#define RXD2 16
#define TXD2 17

TinyGPSPlus gps;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  Serial.println("--- GPS Tester ---");
  Serial.println("Waiting for NMEA data...");
}

void loop() {
  while (Serial2.available() > 0) {
    char c = Serial2.read();
    Serial.write(c); // Raw NMEA output to Serial Monitor
    gps.encode(c);
  }

  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println("\nWARNING: No GPS data received. Check wiring (RX/TX).");
    delay(2000);
  } else if (gps.location.isUpdated()) {
    Serial.print("\n--- FIX ACQUIRED ---");
    Serial.print("\nLat: ");
    Serial.println(gps.location.lat(), 6);
    Serial.print("Lon: ");
    Serial.println(gps.location.lng(), 6);
    Serial.print("Sats: ");
    Serial.println(gps.satellites.value());
    Serial.print("HDOP: ");
    Serial.println(gps.hdop.hdop());
    Serial.println("--------------------\n");
  }
}
