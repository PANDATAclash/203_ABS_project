#include <Wire.h>
#include <SD.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_GPS.h>
#include <math.h>

#define BNO08X_RESET -1
#define BLE Serial8
#define GPSSerial Serial3

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;
Adafruit_GPS GPS(&GPSSerial);

File logFile;

const uint32_t LOG_INTERVAL_MS = 17;   // ~58.8 Hz logger

// ---------------- Hall frequency measurement ----------------
const int HALL_F_PIN = 4;
const int HALL_R_PIN = 5;

volatile uint32_t lastEdgeUsHallF = 0;
volatile uint32_t lastEdgeUsHallR = 0;
volatile uint32_t periodUsHallF = 0;
volatile uint32_t periodUsHallR = 0;
volatile bool newPulseHallF = false;
volatile bool newPulseHallR = false;

float hallF_Hz = 0.0f;
float hallR_Hz = 0.0f;

const uint32_t HALL_TIMEOUT_US = 500000UL;  // 0.5 s timeout -> frequency goes to 0

void isrHallF() {
  uint32_t now = micros();
  if (lastEdgeUsHallF != 0) {
    periodUsHallF = now - lastEdgeUsHallF;
    newPulseHallF = true;
  }
  lastEdgeUsHallF = now;
}

void isrHallR() {
  uint32_t now = micros();
  if (lastEdgeUsHallR != 0) {
    periodUsHallR = now - lastEdgeUsHallR;
    newPulseHallR = true;
  }
  lastEdgeUsHallR = now;
}

void updateHallReadings() {
  uint32_t pF, pR, tF, tR;
  bool gotF, gotR;

  noInterrupts();
  pF = periodUsHallF;
  pR = periodUsHallR;
  tF = lastEdgeUsHallF;
  tR = lastEdgeUsHallR;
  gotF = newPulseHallF;
  gotR = newPulseHallR;
  newPulseHallF = false;
  newPulseHallR = false;
  interrupts();

  if (gotF && pF > 0) {
    hallF_Hz = 1000000.0f / (float)pF;
  }

  if (gotR && pR > 0) {
    hallR_Hz = 1000000.0f / (float)pR;
  }

  uint32_t nowUs = micros();

  if ((nowUs - tF) > HALL_TIMEOUT_US) {
    hallF_Hz = 0.0f;
  }
  if ((nowUs - tR) > HALL_TIMEOUT_US) {
    hallR_Hz = 0.0f;
  }
}
// ------------------------------------------------------------

// ---------------- Time / state ----------------
uint32_t startMs = 0;
uint32_t lastLogMs = 0;

bool loggingActive = false;
bool statsReady = false;
bool statsSent = false;

char currentFilename[13] = "";
// ---------------------------------------------

// ---------------- IMU values -----------------
float linAx = 0.0f;
float linAy = 0.0f;
float linAz = 0.0f;

float linAxFilt = 0.0f;
float linAyFilt = 0.0f;
float linAzFilt = 0.0f;

float rawAx = 0.0f;
float rawAy = 0.0f;
float rawAz = 0.0f;

float rawAxFilt = 0.0f;
bool filterInitialized = false;

// Low-pass filter strength
const float LINACC_FILTER_ALPHA = 0.08f;
const float GRAVITY_MPS2 = 9.80665f;
const uint16_t CALIBRATION_SAMPLES_REQUIRED = 120;

float rollDeg = 0.0f;
float pitchDeg = 0.0f;

bool linAccValid = false;
bool rawAccValid = false;
bool rotValid = false;

bool calibrationActive = false;
bool calibrationDone = false;
uint16_t calibrationSampleCount = 0;
float calibrationRawAxSum = 0.0f;
float calibrationPitchSum = 0.0f;
float rawAxBias = 0.0f;
float pitchZeroDeg = 0.0f;
// ---------------------------------------------

// ---------------- GPS values -----------------
float speedKmh = 0.0f;
bool gpsValid = false;
bool lastFixState = false;
bool fixStateKnown = false;
// ---------------------------------------------

uint32_t sampleCount = 0;

// Stats
float maxSpeed = 0.0f;
float maxAcc = -1000000.0f;
float maxDecc = 1000000.0f;
float maxRollAbs = 0.0f;
float maxPitchAbs = 0.0f;

float maxHallF_Hz = 0.0f;
float maxHallR_Hz = 0.0f;

bool isLeapYear(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2 && isLeapYear(year)) return 29;
  return dim[month - 1];
}

int dayOfWeek(int year, int month, int day) {
  if (month < 3) {
    month += 12;
    year -= 1;
  }
  int K = year % 100;
  int J = year / 100;
  int h = (day + (13 * (month + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
  return (h + 5) % 7;  // Monday=0 ... Sunday=6
}

int lastSunday(int year, int month) {
  int dim = daysInMonth(year, month);
  for (int d = dim; d >= dim - 6; --d) {
    if (dayOfWeek(year, month, d) == 6) return d;
  }
  return dim;
}

bool isEUDST(int year, int month, int day, int hourUtc) {
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;

  int changeDay = lastSunday(year, month);
  if (month == 3) {
    if (day > changeDay) return true;
    if (day < changeDay) return false;
    return hourUtc >= 1;
  }

  if (day < changeDay) return true;
  if (day > changeDay) return false;
  return hourUtc < 1;
}

void getLocalDateTimeFromGPS(int &year, int &month, int &day, int &hour, int &minute) {
  year = 2000 + GPS.year;
  month = GPS.month;
  day = GPS.day;
  hour = GPS.hour;
  minute = GPS.minute;

  int offsetHours = isEUDST(year, month, day, hour) ? 2 : 1;
  hour += offsetHours;

  while (hour >= 24) {
    hour -= 24;
    day++;
    int dim = daysInMonth(year, month);
    if (day > dim) {
      day = 1;
      month++;
      if (month > 12) {
        month = 1;
        year++;
      }
    }
  }
}

bool enableReports() {
  if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION, 16667)) {
    BLE.println("ERR linacc");
    return false;
  }

  if (!bno08x.enableReport(SH2_ACCELEROMETER, 16667)) {
    BLE.println("ERR rawacc");
    return false;
  }

  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, 16667)) {
    BLE.println("ERR rot");
    return false;
  }

  return true;
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

void updateLinAccFilter() {
  if (!filterInitialized) {
    linAxFilt = linAx;
    linAyFilt = linAy;
    linAzFilt = linAz;
    rawAxFilt = rawAx;
    filterInitialized = true;
  } else {
    linAxFilt += LINACC_FILTER_ALPHA * (linAx - linAxFilt);
    linAyFilt += LINACC_FILTER_ALPHA * (linAy - linAyFilt);
    linAzFilt += LINACC_FILTER_ALPHA * (linAz - linAzFilt);
    rawAxFilt += LINACC_FILTER_ALPHA * (rawAx - rawAxFilt);
  }
}

void resetCalibration() {
  calibrationActive = false;
  calibrationDone = false;
  calibrationSampleCount = 0;
  calibrationRawAxSum = 0.0f;
  calibrationPitchSum = 0.0f;
  rawAxBias = 0.0f;
  pitchZeroDeg = 0.0f;
}

void startCalibration() {
  calibrationActive = true;
  calibrationDone = false;
  calibrationSampleCount = 0;
  calibrationRawAxSum = 0.0f;
  calibrationPitchSum = 0.0f;
  rawAxBias = 0.0f;
  pitchZeroDeg = 0.0f;
  BLE.println("CAL");
}

void updateCalibration() {
  if (!calibrationActive || !rawAccValid || !rotValid) {
    return;
  }

  calibrationRawAxSum += rawAx;
  calibrationPitchSum += pitchDeg;
  calibrationSampleCount++;

  if (calibrationSampleCount >= CALIBRATION_SAMPLES_REQUIRED) {
    rawAxBias = calibrationRawAxSum / (float)calibrationSampleCount;
    pitchZeroDeg = calibrationPitchSum / (float)calibrationSampleCount;
    calibrationActive = false;
    calibrationDone = true;
    filterInitialized = false;
    startMs = millis();
    lastLogMs = 0;
    BLE.println("CAL OK");
  }
}

void resetStats() {
  sampleCount = 0;
  maxSpeed = 0.0f;
  maxAcc = -1000000.0f;
  maxDecc = 1000000.0f;
  maxRollAbs = 0.0f;
  maxPitchAbs = 0.0f;
  maxHallF_Hz = 0.0f;
  maxHallR_Hz = 0.0f;
  statsReady = false;
  statsSent = false;
}

void updateStats() {
  sampleCount++;

  if (speedKmh > maxSpeed) {
    maxSpeed = speedKmh;
  }

  float longAccFilt = rawAxFilt;

  if (longAccFilt > maxAcc) {
    maxAcc = longAccFilt;
  }

  if (longAccFilt < maxDecc) {
    maxDecc = longAccFilt;
  }

  float rollAbs = fabs(rollDeg);
  float pitchAbs = fabs(pitchDeg);

  if (rollAbs > maxRollAbs) {
    maxRollAbs = rollAbs;
  }

  if (pitchAbs > maxPitchAbs) {
    maxPitchAbs = pitchAbs;
  }

  if (hallF_Hz > maxHallF_Hz) {
    maxHallF_Hz = hallF_Hz;
  }

  if (hallR_Hz > maxHallR_Hz) {
    maxHallR_Hz = hallR_Hz;
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

  BLE.print("AmaxF:");
  BLE.print(maxAcc, 2);
  BLE.print(" DmaxF:");
  BLE.println(maxDecc, 2);
  delay(120);

  BLE.print("Rmax:");
  BLE.print(maxRollAbs, 2);
  BLE.print(" Pmax:");
  BLE.println(maxPitchAbs, 2);
  delay(120);

  BLE.print("Hall_Fmax:");
  BLE.print(maxHallF_Hz, 2);
  BLE.print(" Hall_Rmax:");
  BLE.println(maxHallR_Hz, 2);

  statsSent = true;
}

bool prepareNextFilename() {
  int yearLocal, monthLocal, dayLocal, hourLocal, minuteLocal;
  getLocalDateTimeFromGPS(yearLocal, monthLocal, dayLocal, hourLocal, minuteLocal);

  for (uint16_t suffix = 0; suffix <= 99; suffix++) {
    if (suffix == 0) {
      snprintf(currentFilename, sizeof(currentFilename), "%02d%02d%02d.CSV", dayLocal, hourLocal, minuteLocal);
    } else {
      snprintf(currentFilename, sizeof(currentFilename), "%02d%02d%02d%d.CSV", dayLocal, hourLocal, minuteLocal, suffix % 10);
    }

    if (!SD.exists(currentFilename)) {
      return true;
    }
  }

  BLE.println("ERR files");
  currentFilename[0] = '\0';
  return false;
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

  logFile = SD.open(currentFilename, FILE_WRITE);
  if (!logFile) {
    BLE.println("ERR file");
    currentFilename[0] = '\0';
    return;
  }

  logFile.println("t_s,roll_deg,pitch_deg,raw_ax_mps2,raw_ax_filt_mps2,raw_ay_mps2,raw_az_mps2,speed_kmh,hall_f_hz,hall_r_hz,gps_fix");
  logFile.flush();

  linAccValid = false;
  rawAccValid = false;
  rotValid = false;
  filterInitialized = false;
  resetCalibration();
  startCalibration();

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
  Wire.setClock(400000);
  BLE.println("I2C");
  delay(120);

  if (!bno08x.begin_I2C()) {
    BLE.println("ERR IMU");
    while (1) {}
  }
  BLE.println("IMU");
  delay(120);

  if (!enableReports()) {
    while (1) {}
  }
  BLE.println("REP");
  delay(120);

  GPS.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_5HZ);
  GPS.sendCommand(PGCMD_ANTENNA);
  BLE.println("GPS");
  delay(120);

  if (!SD.begin(BUILTIN_SDCARD)) {
    BLE.println("ERR SD");
    while (1) {}
  }
  BLE.println("SD");
  delay(120);

  pinMode(HALL_F_PIN, INPUT);
  pinMode(HALL_R_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(HALL_F_PIN), isrHallF, RISING);
  attachInterrupt(digitalPinToInterrupt(HALL_R_PIN), isrHallR, RISING);

  BLE.println("RDY 1/0");
  delay(120);
  BLE.println("FIX:0");

  Serial.println("raw_ax_mps2,raw_ax_filt_mps2,speed_kmh,hall_f_hz,hall_r_hz,gps_fix");
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

  updateHallReadings();

  while (GPSSerial.available()) {
    GPS.read();
  }

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
    }
  }

  if (bno08x.wasReset()) {
    enableReports();
  }

  while (bno08x.getSensorEvent(&sensorValue)) {
    if (sensorValue.sensorId == SH2_LINEAR_ACCELERATION) {
      linAx = sensorValue.un.linearAcceleration.x;
      linAy = sensorValue.un.linearAcceleration.y;
      linAz = sensorValue.un.linearAcceleration.z;
      linAccValid = true;
    }

    if (sensorValue.sensorId == SH2_ACCELEROMETER) {
      rawAx = sensorValue.un.accelerometer.x;
      rawAy = sensorValue.un.accelerometer.y;
      rawAz = sensorValue.un.accelerometer.z;
      rawAccValid = true;
    }

    if (sensorValue.sensorId == SH2_ROTATION_VECTOR) {
      float qr = sensorValue.un.rotationVector.real;
      float qi = sensorValue.un.rotationVector.i;
      float qj = sensorValue.un.rotationVector.j;
      float qk = sensorValue.un.rotationVector.k;
      quaternionToRollPitch(qr, qi, qj, qk, rollDeg, pitchDeg);
      rotValid = true;
    }

    updateCalibration();
    if (calibrationDone && linAccValid && rawAccValid && rotValid) {
      updateLinAccFilter();
    }
  }

  if (loggingActive) {
    if (calibrationDone && linAccValid && rawAccValid && rotValid && gpsValid) {
      uint32_t now = millis();

      if (now - lastLogMs >= LOG_INTERVAL_MS) {
        if (lastLogMs == 0) {
          lastLogMs = now;
        } else {
          lastLogMs += LOG_INTERVAL_MS;
          if ((now - lastLogMs) > 100) {
            lastLogMs = now;
          }
        }

        float tSec = (now - startMs) / 1000.0f;

        logFile.print(tSec, 3);
        logFile.print(',');

        logFile.print(rollDeg, 2);
        logFile.print(',');
        logFile.print(pitchDeg, 2);
        logFile.print(',');
        logFile.print(rawAx, 4);
        logFile.print(',');
        logFile.print(rawAxFilt, 4);
        logFile.print(',');
        logFile.print(rawAy, 4);
        logFile.print(',');
        logFile.print(rawAz, 4);
        logFile.print(',');
        logFile.print(speedKmh, 2);
        logFile.print(',');
        logFile.print(hallF_Hz, 2);
        logFile.print(',');
        logFile.print(hallR_Hz, 2);
        logFile.print(',');
        logFile.println(lastFixState ? 1 : 0);

        updateStats();

        static uint32_t flushCount = 0;
        flushCount++;
        if (flushCount >= 20) {
          logFile.flush();
          flushCount = 0;
        }
      }
    }
  }

  static uint32_t lastPlotMs = 0;
  uint32_t nowPlot = millis();
  if (nowPlot - lastPlotMs >= 100) {
    lastPlotMs = nowPlot;

    Serial.print(rawAx, 3);
    Serial.print(',');
    Serial.print(rawAxFilt, 3);
    Serial.print(',');
    Serial.print(speedKmh, 2);
    Serial.print(',');
    Serial.print(hallF_Hz, 2);
    Serial.print(',');
    Serial.print(hallR_Hz, 2);
    Serial.print(',');
    Serial.println(lastFixState ? 1 : 0);
  }
}