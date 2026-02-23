/************************************************************
 * GeoSurveyor Pro – Ultrasonic Calibration/Tester
 * Use this to verify distance readings and stability.
 ************************************************************/

#define TRIG_PIN 26
#define ECHO_PIN 27

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.println("--- Ultrasonic Tester ---");
}

void loop() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    Serial.println("Out of range or sensor error!");
  } else {
    float distanceCM = duration * 0.034 / 2;
    float distanceInch = distanceCM * 0.393701;

    Serial.print("Distance: ");
    Serial.print(distanceCM);
    Serial.print(" cm | ");
    Serial.print(distanceInch);
    Serial.println(" in");
  }

  delay(200);
}
