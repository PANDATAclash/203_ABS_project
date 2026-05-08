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

const uint32_t LOG_INTERVAL_MS = 17;     // ~58.8 Hz logger
const uint32_t GPS_STALE_MS    = 2000;   // declare GPS lost after 2 s without a parse
const uint32_t ERROR_REPEAT_MS = 5000;   // repeat active error messages every 5 s

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
uint32_t startMs        = 0;
uint32_t lastLogMs      = 0;
uint32_t lastGpsParseMs = 0;
uint32_t lastErrorMs    = 0;

bool loggingActive = false;
bool statsReady    = false;
bool statsSent     = false;

// Active error flags — each true condition repeats its message every ERROR_REPEAT_MS.
bool errorGPS = false;
bool errorIMU = false;

char currentFilename[13] = "";
// ---------------------------------------------

// ---------------- IMU values -----------------
float rawAx = 0.0f;
float rawAy = 0.0f;
float rawAz = 0.0f;

float rawAxFilt = 0.0f;
bool filterInitialized = false;

// Gyroscope (calibrated angular velocity, rad/s)
float gyrX = 0.0f;
float gyrY = 0.0f;
float gyrZ = 0.0f;

uint32_t lastGyroUs = 0;

const float RAD_TO_DEG_F = 57.2957795f;

// ---- Two-state pitch estimator ----
// STATIONARY (both halls < MOVING_HALL_HZ): LP-filtered accel tilt.
// MOVING     (any hall  >= MOVING_HALL_HZ): pure gyro integration.
// No gyro-rate threshold needed — state is purely wheel-speed driven.
// Sign: positive = nose UP.  Change -= to += in gyro handler if reversed.
const float MOVING_HALL_HZ        = 0.5f;   // any wheel above this = MOVING state
const float PITCH_ACCEL_AVG_ALPHA = 0.02f;  // LP filter for stationary accel reference (~50 samples = 0.8 s)

float pitchGyroDeg      = 0.0f;
float pitchAccelZeroDeg = 0.0f;   // atan2 pitch at calibration (flat, stationary)
float pitchAccelAvg     = 0.0f;   // running LP-filtered accel tilt used in STATIONARY state

// Raw (uncorrected) accelerometer pitch — logged for forensic comparison.
// Shows spurious nose-UP during braking, proving why accel-only pitch fails.
float pitchAccelDeg = 0.0f;

// Low-pass filter strength
const float LINACC_FILTER_ALPHA = 0.08f;
const float GRAVITY_MPS2 = 9.80665f;
const uint16_t CALIBRATION_SAMPLES_REQUIRED = 120;

// Pitch/roll from SH2_ROTATION_VECTOR (accel+gyro+mag — can be biased by linear accel)
float rollDeg  = 0.0f;
float pitchDeg = 0.0f;

// Pitch/roll from SH2_GAME_ROTATION_VECTOR (accel+gyro, no mag — better during braking)
float rollGameDeg  = 0.0f;
float pitchGameDeg = 0.0f;

bool rawAccValid  = false;
bool rotValid     = false;
bool gameRotValid = false;
bool gyroValid    = false;

bool calibrationActive = false;
bool calibrationDone = false;
uint16_t calibrationSampleCount = 0;
float calibrationRawAxSum      = 0.0f;
float calibrationPitchSum      = 0.0f;
float calibrationPitchGameSum  = 0.0f;
float calibrationAccelPitchSum = 0.0f;  // accumulates atan2(-rawAx,rawAz) for ZUPT zero reference
float rawAxBias        = 0.0f;
float pitchZeroDeg     = 0.0f;
float pitchGameZeroDeg = 0.0f;
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
  if (!bno08x.enableReport(SH2_ACCELEROMETER, 16667)) {
    BLE.println("ERR rawacc");
    return false;
  }

  if (!bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, 16667)) {
    BLE.println("ERR gyro");
    return false;
  }

  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, 16667)) {
    BLE.println("ERR rot");
    return false;
  }

  if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 16667)) {
    BLE.println("ERR gamerot");
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
    rawAxFilt = rawAx;
    filterInitialized = true;
  } else {
    rawAxFilt += LINACC_FILTER_ALPHA * (rawAx - rawAxFilt);
  }
}

void resetCalibration() {
  calibrationActive = false;
  calibrationDone   = false;
  calibrationSampleCount     = 0;
  calibrationRawAxSum        = 0.0f;
  calibrationPitchSum        = 0.0f;
  calibrationPitchGameSum    = 0.0f;
  calibrationAccelPitchSum   = 0.0f;
  rawAxBias         = 0.0f;
  pitchZeroDeg      = 0.0f;
  pitchGameZeroDeg  = 0.0f;
  pitchAccelZeroDeg = 0.0f;
  pitchGyroDeg      = 0.0f;
  pitchAccelAvg     = 0.0f;
  pitchAccelDeg     = 0.0f;
  lastGyroUs        = 0;
}

void startCalibration() {
  calibrationActive = true;
  calibrationDone   = false;
  calibrationSampleCount     = 0;
  calibrationRawAxSum        = 0.0f;
  calibrationPitchSum        = 0.0f;
  calibrationPitchGameSum    = 0.0f;
  calibrationAccelPitchSum   = 0.0f;
  rawAxBias         = 0.0f;
  pitchZeroDeg      = 0.0f;
  pitchGameZeroDeg  = 0.0f;
  pitchAccelZeroDeg = 0.0f;
  pitchGyroDeg      = 0.0f;
  pitchAccelAvg     = 0.0f;
  pitchAccelDeg     = 0.0f;
  lastGyroUs        = 0;
  BLE.println("CAL");
}

void updateCalibration() {
  if (!calibrationActive || !rawAccValid || !rotValid || !gameRotValid) {
    return;
  }

  calibrationRawAxSum      += rawAx;
  calibrationPitchSum      += pitchDeg;
  calibrationPitchGameSum  += pitchGameDeg;
  // Accumulate raw accelerometer tilt angle — used as ZUPT zero reference.
  // Vehicle is stationary during calibration so atan2 is trustworthy here.
  calibrationAccelPitchSum += atan2f(-rawAx, rawAz) * RAD_TO_DEG_F;
  calibrationSampleCount++;

  if (calibrationSampleCount >= CALIBRATION_SAMPLES_REQUIRED) {
    rawAxBias         = calibrationRawAxSum     / (float)calibrationSampleCount;
    pitchZeroDeg      = calibrationPitchSum     / (float)calibrationSampleCount;
    pitchGameZeroDeg  = calibrationPitchGameSum / (float)calibrationSampleCount;
    pitchAccelZeroDeg = calibrationAccelPitchSum / (float)calibrationSampleCount;
    calibrationActive  = false;
    calibrationDone    = true;
    filterInitialized  = false;
    // Gyro integration starts from 0 after calibration.
    // pitchAccelAvg also reset so stationary tracking starts fresh.
    pitchGyroDeg  = 0.0f;
    pitchAccelAvg = 0.0f;
    pitchAccelDeg = 0.0f;
    lastGyroUs    = 0;
    startMs   = millis();
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

  float rollAbs  = fabs(rollDeg);
  float pitchAbs = fabs(pitchGyroDeg);  // track the reliable (ZUPT-corrected) pitch

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

// Format: STA fix:X cal:X log:X
void sendStatusBLE() {
  BLE.print("STA fix:");
  BLE.print(lastFixState ? "1" : "0");
  BLE.print(" cal:");
  BLE.print(calibrationDone ? "1" : "0");
  BLE.print(" log:");
  BLE.println(loggingActive ? "1" : "0");
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

  logFile.println("t_s,roll_deg,pitch_rv_deg,pitch_game_deg,pitch_gyro_deg,pitch_accel_deg,gyro_y_rads,raw_ax_mps2,raw_ax_filt_mps2,raw_ay_mps2,raw_az_mps2,speed_kmh,hall_f_hz,hall_r_hz,gps_fix");
  logFile.flush();

  rawAccValid  = false;
  rotValid     = false;
  gameRotValid = false;
  gyroValid    = false;
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
    while (1) { BLE.println("ERR IMU"); delay(5000); }
  }
  BLE.println("IMU");
  delay(120);

  if (!enableReports()) {
    while (1) { BLE.println("ERR IMU rep"); delay(5000); }
  }
  BLE.println("REP");
  delay(120);

  GPS.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_5HZ);
  GPS.sendCommand(PGCMD_ANTENNA);
  BLE.println("GPS");
  delay(120);

  while (!SD.begin(BUILTIN_SDCARD)) {
    BLE.println("ERR SD");
    delay(5000);
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

}

void loop() {
  // ---- BLE commands: '1'=start  '0'=stop  's'=status ----
  while (BLE.available()) {
    char cmd = BLE.read();

    if (cmd == '1') {
      startLogging();
    } else if (cmd == '0') {
      stopLogging();
    } else if (cmd == 's' || cmd == 'S') {
      sendStatusBLE();
    }
  }

  updateHallReadings();

  // ---- GPS ----
  while (GPSSerial.available()) {
    GPS.read();
  }

  if (GPS.newNMEAreceived()) {
    if (GPS.parse(GPS.lastNMEA())) {
      lastGpsParseMs = millis();
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

      errorGPS = !currentFix;
    }
  }

  // Declare GPS lost if no parse for GPS_STALE_MS.
  if (gpsValid && lastGpsParseMs != 0 &&
      (millis() - lastGpsParseMs) > GPS_STALE_MS) {
    gpsValid = false;
    speedKmh = 0.0f;
    errorGPS = true;
    if (lastFixState) {
      lastFixState  = false;
      fixStateKnown = true;
      BLE.println("FIX:0");
    }
    BLE.println("ERR GPS");
  }

  // ---- IMU ----
  if (bno08x.wasReset()) {
    rawAccValid       = false;
    rotValid          = false;
    gameRotValid      = false;
    gyroValid         = false;
    filterInitialized = false;
    lastGyroUs        = 0;

    if (!enableReports()) {
      errorIMU = true;
      BLE.println("ERR IMU");
    } else {
      errorIMU = false;
      BLE.println("IMU reset");
    }
  }

  while (bno08x.getSensorEvent(&sensorValue)) {
    if (sensorValue.sensorId == SH2_ACCELEROMETER) {
      rawAx = sensorValue.un.accelerometer.x;
      rawAy = sensorValue.un.accelerometer.y;
      rawAz = sensorValue.un.accelerometer.z;
      rawAccValid = true;
    }

    if (sensorValue.sensorId == SH2_GYROSCOPE_CALIBRATED) {
      uint32_t nowUs = micros();

      gyrX = sensorValue.un.gyroscope.x;
      gyrY = sensorValue.un.gyroscope.y;
      gyrZ = sensorValue.un.gyroscope.z;

      if (calibrationDone && rawAccValid) {
        bool moving = (hallF_Hz > MOVING_HALL_HZ) || (hallR_Hz > MOVING_HALL_HZ);

        if (!moving) {
          // ---- STATIONARY state ----
          // LP-filter the accelerometer tilt angle.  alpha=0.02 gives a time
          // constant of ~50 samples (~0.8 s at 60 Hz): smooth enough to reject
          // engine vibration noise, fast enough to track slow lean changes.
          // pitchGyroDeg is held to this filtered value — no gyro drift at all
          // while the vehicle is not moving.
          float rawPitch = atan2f(-rawAx, rawAz) * RAD_TO_DEG_F - pitchAccelZeroDeg;
          pitchAccelAvg += PITCH_ACCEL_AVG_ALPHA * (rawPitch - pitchAccelAvg);
          pitchGyroDeg   = pitchAccelAvg;
        } else if (lastGyroUs != 0) {
          // ---- MOVING state ----
          // Pure gyro integration.  Completely immune to braking/acceleration.
          // The first sample after leaving STATIONARY starts from pitchAccelAvg
          // (the last filtered stationary value), so the baseline is always clean.
          // Sign: positive gyrY = nose DOWN -> subtract to get nose-up = positive.
          // If sign is reversed in your recording, change -= to +=.
          float dt = (nowUs - lastGyroUs) * 1e-6f;
          if (dt > 0.001f && dt < 0.1f) {
            pitchGyroDeg -= gyrY * dt * RAD_TO_DEG_F;
          }
        }

        // Raw (uncorrected) accelerometer pitch — logged every sample for
        // forensic comparison.  During braking it will show a large positive
        // false angle, demonstrating the contamination effect.
        pitchAccelDeg = atan2f(-rawAx, rawAz) * RAD_TO_DEG_F - pitchAccelZeroDeg;
      }

      lastGyroUs = nowUs;
      gyroValid  = true;
    }

    if (sensorValue.sensorId == SH2_ROTATION_VECTOR) {
      float qr = sensorValue.un.rotationVector.real;
      float qi = sensorValue.un.rotationVector.i;
      float qj = sensorValue.un.rotationVector.j;
      float qk = sensorValue.un.rotationVector.k;
      quaternionToRollPitch(qr, qi, qj, qk, rollDeg, pitchDeg);
      rotValid = true;
    }

    if (sensorValue.sensorId == SH2_GAME_ROTATION_VECTOR) {
      float qr = sensorValue.un.gameRotationVector.real;
      float qi = sensorValue.un.gameRotationVector.i;
      float qj = sensorValue.un.gameRotationVector.j;
      float qk = sensorValue.un.gameRotationVector.k;
      quaternionToRollPitch(qr, qi, qj, qk, rollGameDeg, pitchGameDeg);
      gameRotValid = true;
    }

    updateCalibration();
    if (calibrationDone && rawAccValid && rotValid) {
      updateLinAccFilter();
    }
  }

  // ---- SD logging ----
  if (loggingActive) {
    if (calibrationDone && rawAccValid && rotValid && gpsValid) {
      uint32_t now = millis();

      if (now - lastLogMs >= LOG_INTERVAL_MS) {
        if (lastLogMs == 0) {
          // First call after calibration: just set the time baseline.
          // Don't write a row yet — avoids a duplicate on the next iteration.
          lastLogMs = now;
        } else {
          lastLogMs += LOG_INTERVAL_MS;
          if ((now - lastLogMs) > 100) {
            lastLogMs = now;   // re-sync if we fell more than 100 ms behind
          }

          float tSec = (now - startMs) / 1000.0f;

          logFile.print(tSec, 3);          logFile.print(',');
          logFile.print(rollDeg, 2);       logFile.print(',');
          logFile.print(pitchDeg - pitchZeroDeg, 2);         logFile.print(',');  // RV pitch (zeroed)
          logFile.print(pitchGameDeg - pitchGameZeroDeg, 2); logFile.print(',');  // game RV pitch (zeroed)
          logFile.print(pitchGyroDeg, 2);  logFile.print(',');  // PRIMARY pitch
          logFile.print(pitchAccelDeg, 2); logFile.print(',');  // accel pitch (contaminated, for comparison)
          logFile.print(gyrY, 4);          logFile.print(',');  // pitch rate rad/s
          logFile.print(rawAx, 4);         logFile.print(',');
          logFile.print(rawAxFilt, 4);     logFile.print(',');
          logFile.print(rawAy, 4);         logFile.print(',');
          logFile.print(rawAz, 4);         logFile.print(',');
          logFile.print(speedKmh, 2);      logFile.print(',');
          logFile.print(hallF_Hz, 2);      logFile.print(',');
          logFile.print(hallR_Hz, 2);      logFile.print(',');
          logFile.println(lastFixState ? 1 : 0);

          updateStats();

          static uint32_t flushCount = 0;
          if (++flushCount >= 20) {
            logFile.flush();
            flushCount = 0;
          }
        }
      }
    }
  }

  // ---- Active error repeat (no fix or hardware error) ----
  if (errorGPS || errorIMU) {
    uint32_t nowMs = millis();
    if (nowMs - lastErrorMs >= ERROR_REPEAT_MS) {
      lastErrorMs = nowMs;
      if (errorIMU) BLE.println("ERR IMU");
      if (errorGPS) {
        if (!gpsValid) {
          BLE.println("ERR GPS");
        } else {
          BLE.println("FIX:0");
        }
      }
    }
  }

}
