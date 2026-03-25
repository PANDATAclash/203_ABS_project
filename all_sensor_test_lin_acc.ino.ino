#include <Wire.h>
#include <SD.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_GPS.h>
#include <math.h>

#define BNO08X_RESET -1
#define BLE Serial1
#define GPSSerial Serial2

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;
Adafruit_GPS GPS(&GPSSerial);

File logFile;

const uint32_t LOG_INTERVAL_MS = 100;
const uint8_t MAX_MEASUREMENTS = 3;

// Exponential smoothing factor
// smaller = smoother, but more lag
const float alpha = 0.1f;

uint32_t startMs = 0;
uint32_t lastLogMs = 0;

bool loggingActive = false;
bool statsReady = false;
bool statsSent = false;

uint8_t measurementIndex = 0;
char currentFilename[13] = "";

// Raw linear acceleration from sensor
float axRaw = 0.0f;
float ayRaw = 0.0f;

// Smoothed linear acceleration used for logging/stats
float ax = 0.0f;
float ay = 0.0f;

float rollDeg = 0.0f;
float pitchDeg = 0.0f;
float speedKmh = 0.0f;

bool accelValid = false;
bool rotValid = false;
bool gpsValid = false;

bool lastFixState = false;
bool fixStateKnown = false;

uint32_t sampleCount = 0;

float maxSpeed = 0.0f;
float maxAcc = -1000000.0f;
float maxDecc = 1000000.0f;
float maxRollAbs = 0.0f;
float maxPitchAbs = 0.0f;

void enableReports() {
  if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION, 10000)) {
    BLE.println("ERR acc");
    while (1) {}
  }

  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, 10000)) {
    BLE.println("ERR rot");
    while (1) {}
  }
}

void quaternionToRollPitch(float qr, float qi, float qj, float qk, float &roll, float &pitch) {
  float sinr_cosp = 2.0f * (qr * qi + qj * qk);
  float cosr_cosp = 1.0f - 2.0f * (qi * qi + qj * qj);
  roll = atan2f(sinr_cosp, cosr_cosp);

  float sinp = 2.0f * (qr * qj - qk * qi);
  if (fabs(sinp) >= 1.0f) {
    pitch = copysignf(M_PI / 2.0f, sinp);
  } else {
    pitch = asinf(sinp);
  }

  roll *= 180.0f / M_PI;
  pitch *= 180.0f / M_PI;
}

void resetStats() {
  sampleCount = 0;
  maxSpeed = 0.0f;
  maxAcc = -1000000.0f;
  maxDecc = 1000000.0f;
  maxRollAbs = 0.0f;
  maxPitchAbs = 0.0f;
  statsReady = false;
  statsSent = false;
}

void updateStats() {
  sampleCount++;

  if (speedKmh > maxSpeed) {
    maxSpeed = speedKmh;
  }

  if (ax > maxAcc) {
    maxAcc = ax;
  }

  if (ax < maxDecc) {
    maxDecc = ax;
  }

  float rollAbs = fabs(rollDeg);
  float pitchAbs = fabs(pitchDeg);

  if (rollAbs > maxRollAbs) {
    maxRollAbs = rollAbs;
  }

  if (pitchAbs > maxPitchAbs) {
    maxPitchAbs = pitchAbs;
  }
}

void sendStatsOnce() {
  BLE.print("F:");
  BLE.print(currentFilename);
  BLE.print(" N:");
  BLE.println(sampleCount);
  delay(120);

  BLE.print("Vmax:");
  BLE.print(maxSpeed, 2);
  BLE.println("kmh");
  delay(120);

  BLE.print("Amax:");
  BLE.print(maxAcc, 2);
  BLE.print(" Dmax:");
  BLE.println(maxDecc, 2);
  delay(120);

  BLE.print("Rmax:");
  BLE.print(maxRollAbs, 2);
  BLE.print(" Pmax:");
  BLE.println(maxPitchAbs, 2);

  statsSent = true;
}

bool prepareNextFilename() {
  if (measurementIndex >= MAX_MEASUREMENTS) {
    BLE.println("ERR max3");
    return false;
  }

  measurementIndex++;
  snprintf(currentFilename, sizeof(currentFilename), "LOG%u.CSV", measurementIndex);
  return true;
}

void startLogging() {
  if (loggingActive) {
    BLE.println("ERR run");
    return;
  }

  if (!gpsValid || !lastFixState) {
    BLE.println("ERR nofix");
    return;
  }

  if (!prepareNextFilename()) {
    return;
  }

  SD.remove(currentFilename);

  logFile = SD.open(currentFilename, FILE_WRITE);
  if (!logFile) {
    BLE.println("ERR file");
    measurementIndex--;
    currentFilename[0] = '\0';
    return;
  }

  logFile.println("t_s,ax_mps2,ay_mps2,roll_deg,pitch_deg,gps_speed_kmh");
  logFile.flush();

  accelValid = false;
  rotValid = false;

  axRaw = 0.0f;
  ayRaw = 0.0f;
  ax = 0.0f;
  ay = 0.0f;

  speedKmh = 0.0f;
  lastLogMs = 0;
  startMs = millis();

  resetStats();

  loggingActive = true;

  BLE.print("ST ");
  BLE.println(currentFilename);
}

void stopLogging() {
  if (!loggingActive) {
    BLE.println("ERR no run");
    return;
  }

  logFile.flush();
  logFile.close();
  loggingActive = false;
  statsReady = true;

  BLE.print("SP ");
  BLE.println(currentFilename);

  if (!statsSent) {
    sendStatsOnce();
  }
}

void setup() {
  Serial.begin(115200);
  BLE.begin(9600);
  GPSSerial.begin(9600);
  delay(1500);

  BLE.println("BOOT");
  delay(120);

  Wire.begin();
  Wire.setClock(100000);
  BLE.println("I2C");
  delay(120);

  if (!bno08x.begin_I2C()) {
    BLE.println("ERR IMU");
    while (1) {}
  }
  BLE.println("IMU");
  delay(120);

  enableReports();
  BLE.println("REP");
  delay(120);

  GPS.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_5HZ);
  BLE.println("GPS");
  delay(120);

  if (!SD.begin(BUILTIN_SDCARD)) {
    BLE.println("ERR SD");
    while (1) {}
  }
  BLE.println("SD");
  delay(120);

  BLE.println("RDY 1/0");
  delay(120);
  BLE.println("FIX:0");
}

void loop() {
  while (BLE.available()) {
    char cmd = BLE.read();

    if (cmd == '1') {
      startLogging();
    } else if (cmd == '0') {
      stopLogging();
    }
  }

  GPS.read();

  if (GPS.newNMEAreceived()) {
    if (GPS.parse(GPS.lastNMEA())) {
      bool currentFix = GPS.fix;

      if (currentFix) {
        speedKmh = GPS.speed * 1.852f;
      } else {
        speedKmh = 0.0f;
      }
      gpsValid = true;

      if (!fixStateKnown || currentFix != lastFixState) {
        BLE.print("FIX:");
        BLE.println(currentFix ? "1" : "0");
        lastFixState = currentFix;
        fixStateKnown = true;
      }

      Serial.print("GPS fix=");
      Serial.print((int)GPS.fix);
      Serial.print(" sats=");
      Serial.print((int)GPS.satellites);
      Serial.print(" speed=");
      Serial.println(speedKmh, 2);
    }
  }

  if (loggingActive) {
    if (bno08x.wasReset()) {
      enableReports();
    }

    if (bno08x.getSensorEvent(&sensorValue)) {
      if (sensorValue.sensorId == SH2_LINEAR_ACCELERATION) {
        axRaw = sensorValue.un.linearAcceleration.x;
        ayRaw = sensorValue.un.linearAcceleration.y;

        ax = alpha * axRaw + (1.0f - alpha) * ax;
        ay = alpha * ayRaw + (1.0f - alpha) * ay;

        accelValid = true;
      }

      if (sensorValue.sensorId == SH2_ROTATION_VECTOR) {
        float qr = sensorValue.un.rotationVector.real;
        float qi = sensorValue.un.rotationVector.i;
        float qj = sensorValue.un.rotationVector.j;
        float qk = sensorValue.un.rotationVector.k;
        quaternionToRollPitch(qr, qi, qj, qk, rollDeg, pitchDeg);
        rotValid = true;
      }
    }

    if (accelValid && rotValid && gpsValid) {
      uint32_t now = millis();

      if (now - lastLogMs >= LOG_INTERVAL_MS) {
        lastLogMs = now;
        float tSec = (now - startMs) / 1000.0f;

        logFile.print(tSec, 3);
        logFile.print(',');
        logFile.print(ax, 4);
        logFile.print(',');
        logFile.print(ay, 4);
        logFile.print(',');
        logFile.print(rollDeg, 2);
        logFile.print(',');
        logFile.print(pitchDeg, 2);
        logFile.print(',');
        logFile.println(speedKmh, 2);

        updateStats();

        static uint32_t flushCount = 0;
        flushCount++;
        if (flushCount >= 10) {
          logFile.flush();
          flushCount = 0;
        }
      }
    }
  }
}