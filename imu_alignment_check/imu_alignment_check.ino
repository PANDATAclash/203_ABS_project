#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

#define BNO08X_RESET -1

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

const float G0 = 9.80665f;
const float ALPHA = 0.08f;

float ax = 0.0f, ay = 0.0f, az = 0.0f;
float axFilt = 0.0f, ayFilt = 0.0f, azFilt = 0.0f;
bool accelValid = false;
bool filtInit = false;

uint32_t lastPrintMs = 0;

static float clampf_local(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool enableReports() {
  if (!bno08x.enableReport(SH2_ACCELEROMETER, 20000)) {
    Serial.println("ERR: cannot enable accelerometer report");
    return false;
  }
  return true;
}

void updateFilter() {
  if (!filtInit) {
    axFilt = ax;
    ayFilt = ay;
    azFilt = az;
    filtInit = true;
  } else {
    axFilt += ALPHA * (ax - axFilt);
    ayFilt += ALPHA * (ay - ayFilt);
    azFilt += ALPHA * (az - azFilt);
  }
}

void printStatus() {
  float amag = sqrtf(axFilt * axFilt + ayFilt * ayFilt + azFilt * azFilt);
  float xRatio = clampf_local(axFilt / G0, -1.0f, 1.0f);
  float yRatio = clampf_local(ayFilt / G0, -1.0f, 1.0f);

  // When the bike is upright and still, these should be near zero if:
  // X = bike forward, Y = bike left/right, Z = bike up/down.
  float xTiltDeg = asinf(xRatio) * 180.0f / M_PI;
  float yTiltDeg = asinf(yRatio) * 180.0f / M_PI;

  bool gravityOk = fabs(amag - G0) < 0.7f;
  bool good = gravityOk && fabs(xTiltDeg) <= 3.0f && fabs(yTiltDeg) <= 3.0f;
  bool ok = gravityOk && fabs(xTiltDeg) <= 6.0f && fabs(yTiltDeg) <= 6.0f;

  Serial.print("ax="); Serial.print(axFilt, 3);
  Serial.print(" ay="); Serial.print(ayFilt, 3);
  Serial.print(" az="); Serial.print(azFilt, 3);
  Serial.print(" | |a|="); Serial.print(amag, 3);
  Serial.print(" | x_tilt="); Serial.print(xTiltDeg, 1);
  Serial.print(" deg");
  Serial.print(" | y_tilt="); Serial.print(yTiltDeg, 1);
  Serial.print(" deg");
  Serial.print(" | ");

  if (good) {
    Serial.println("GOOD - board is aligned well enough");
  } else if (ok) {
    Serial.println("OK - usable, but you can align it better");
  } else {
    Serial.println("NOT GOOD - rotate board until x_tilt and y_tilt are near 0 deg");
  }

  if (!gravityOk) {
    Serial.println("  Hold the bike upright and keep it still while checking alignment.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("IMU alignment check");
  Serial.println("Keep the motorcycle upright and still on level ground.");
  Serial.println("Target: x_tilt ~= 0 deg, y_tilt ~= 0 deg");
  Serial.println("Assumed axes: X=forward, Y=side, Z=up");

  Wire.begin();
  Wire.setClock(400000);

  if (!bno08x.begin_I2C()) {
    Serial.println("ERR: IMU not found");
    while (1) {}
  }

  if (!enableReports()) {
    while (1) {}
  }

  Serial.println("Ready");
}

void loop() {
  if (bno08x.wasReset()) {
    enableReports();
  }

  while (bno08x.getSensorEvent(&sensorValue)) {
    if (sensorValue.sensorId == SH2_ACCELEROMETER) {
      ax = sensorValue.un.accelerometer.x;
      ay = sensorValue.un.accelerometer.y;
      az = sensorValue.un.accelerometer.z;
      accelValid = true;
      updateFilter();
    }
  }

  if (accelValid) {
    uint32_t now = millis();
    if (now - lastPrintMs >= 250) {
      lastPrintMs = now;
      printStatus();
    }
  }
}
