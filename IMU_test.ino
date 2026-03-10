#include <Wire.h>
#include <Adafruit_BNO08x.h>

#define BNO08X_RESET -1

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

void enableReports() {
  Serial.println("Enabling reports...");

  if (!bno08x.enableReport(SH2_ACCELEROMETER, 10000)) {
    Serial.println("Could not enable accelerometer");
  }
  if (!bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000)) {
    Serial.println("Could not enable gyroscope");
  }
  if (!bno08x.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED, 10000)) {
    Serial.println("Could not enable magnetometer");
  }
  if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION, 10000)) {
    Serial.println("Could not enable linear acceleration");
  }
  if (!bno08x.enableReport(SH2_GRAVITY, 10000)) {
    Serial.println("Could not enable gravity");
  }
  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, 10000)) {
    Serial.println("Could not enable rotation vector");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\nBNO085 Teensy 4.1 test");

  Wire.begin();
  Wire.setClock(100000); // start conservative

  if (!bno08x.begin_I2C()) {
    Serial.println("Failed to find BNO08x over I2C");
    while (1) {
      delay(100);
    }
  }

  Serial.println("BNO08x found!");

  for (int n = 0; n < bno08x.prodIds.numEntries; n++) {
    Serial.print("Part ");
    Serial.print(bno08x.prodIds.entry[n].swPartNumber);
    Serial.print(" Version ");
    Serial.print(bno08x.prodIds.entry[n].swVersionMajor);
    Serial.print(".");
    Serial.print(bno08x.prodIds.entry[n].swVersionMinor);
    Serial.print(".");
    Serial.print(bno08x.prodIds.entry[n].swVersionPatch);
    Serial.print(" Build ");
    Serial.println(bno08x.prodIds.entry[n].swBuildNumber);
  }

  enableReports();
  Serial.println("Streaming sensor data...");
}

void loop() {
  if (bno08x.wasReset()) {
    Serial.println("Sensor reset, re-enabling reports");
    enableReports();
  }

  if (!bno08x.getSensorEvent(&sensorValue)) {
    return;
  }

  switch (sensorValue.sensorId) {
    case SH2_ACCELEROMETER:
      Serial.print("ACC  m/s^2   X: ");
      Serial.print(sensorValue.un.accelerometer.x, 3);
      Serial.print("  Y: ");
      Serial.print(sensorValue.un.accelerometer.y, 3);
      Serial.print("  Z: ");
      Serial.println(sensorValue.un.accelerometer.z, 3);
      break;

    case SH2_GYROSCOPE_CALIBRATED:
      Serial.print("GYRO rad/s   X: ");
      Serial.print(sensorValue.un.gyroscope.x, 3);
      Serial.print("  Y: ");
      Serial.print(sensorValue.un.gyroscope.y, 3);
      Serial.print("  Z: ");
      Serial.println(sensorValue.un.gyroscope.z, 3);
      break;

    case SH2_MAGNETIC_FIELD_CALIBRATED:
      Serial.print("MAG  uT?     X: ");
      Serial.print(sensorValue.un.magneticField.x, 3);
      Serial.print("  Y: ");
      Serial.print(sensorValue.un.magneticField.y, 3);
      Serial.print("  Z: ");
      Serial.println(sensorValue.un.magneticField.z, 3);
      break;

    case SH2_LINEAR_ACCELERATION:
      Serial.print("LIN  m/s^2   X: ");
      Serial.print(sensorValue.un.linearAcceleration.x, 3);
      Serial.print("  Y: ");
      Serial.print(sensorValue.un.linearAcceleration.y, 3);
      Serial.print("  Z: ");
      Serial.println(sensorValue.un.linearAcceleration.z, 3);
      break;

    case SH2_GRAVITY:
      Serial.print("GRAV m/s^2   X: ");
      Serial.print(sensorValue.un.gravity.x, 3);
      Serial.print("  Y: ");
      Serial.print(sensorValue.un.gravity.y, 3);
      Serial.print("  Z: ");
      Serial.println(sensorValue.un.gravity.z, 3);
      break;

    case SH2_ROTATION_VECTOR:
      Serial.print("QUAT         r: ");
      Serial.print(sensorValue.un.rotationVector.real, 4);
      Serial.print("  i: ");
      Serial.print(sensorValue.un.rotationVector.i, 4);
      Serial.print("  j: ");
      Serial.print(sensorValue.un.rotationVector.j, 4);
      Serial.print("  k: ");
      Serial.println(sensorValue.un.rotationVector.k, 4);
      break;
  }
}