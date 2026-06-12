#include <Wire.h>
#include <SD.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_GPS.h>

#define BNO08X_RESET -1
#define BLE Serial8        // Teensy 4.1 RX8/TX8
#define GPSSerial Serial7  // Teensy 4.1 RX7/TX7

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;
Adafruit_GPS GPS(&GPSSerial);

File logFile;

const uint32_t LOG_INTERVAL_MS = 10;     // 100 Hz logger
const uint32_t GPS_STALE_MS    = 2000;   // declare GPS lost after 2 s without a parse
const uint32_t ERROR_REPEAT_MS = 5000;   // repeat active error messages every 5 s

// ---------------- Hall frequency measurement ----------------
const int HALL_F_PIN = 36;
const int HALL_R_PIN = 37;

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

// ---------------- Median filter for hall frequencies ----------------
// Window of 5 — rejects single-pulse outliers without meaningful lag.
const uint8_t HALL_MED_SIZE = 5;
float    hallFBuf[HALL_MED_SIZE] = {0, 0, 0, 0, 0};
float    hallRBuf[HALL_MED_SIZE] = {0, 0, 0, 0, 0};
uint8_t  hallFIdx = 0;
uint8_t  hallRIdx = 0;

float medianOf5(float *buf) {
  float tmp[5];
  memcpy(tmp, buf, 5 * sizeof(float));
  // Insertion sort (5 elements — negligible cost)
  for (uint8_t i = 1; i < 5; i++) {
    float v = tmp[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = v;
  }
  return tmp[2];  // middle element
}
// --------------------------------------------------------------------

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

  uint32_t nowUs = micros();

  // Timeout clears the entire median buffer so old readings don't persist
  // after the wheel has stopped.
  if ((nowUs - tF) > HALL_TIMEOUT_US) {
    memset(hallFBuf, 0, sizeof(hallFBuf));
    hallF_Hz = 0.0f;
  } else if (gotF && pF > 0) {
    hallFBuf[hallFIdx] = 1000000.0f / (float)pF;
    hallFIdx = (hallFIdx + 1) % HALL_MED_SIZE;
    hallF_Hz = medianOf5(hallFBuf);
  }

  if ((nowUs - tR) > HALL_TIMEOUT_US) {
    memset(hallRBuf, 0, sizeof(hallRBuf));
    hallR_Hz = 0.0f;
  } else if (gotR && pR > 0) {
    hallRBuf[hallRIdx] = 1000000.0f / (float)pR;
    hallRIdx = (hallRIdx + 1) % HALL_MED_SIZE;
    hallR_Hz = medianOf5(hallRBuf);
  }
}
// ------------------------------------------------------------

// ---------------- Time / state ----------------
uint32_t startMs            = 0;
uint32_t lastLogMs          = 0;
uint32_t lastGpsParseMs     = 0;
uint32_t lastErrorMs        = 0;
uint32_t lastCalibStreamMs  = 0;

bool loggingActive   = false;
bool statsReady      = false;
bool calibStreaming  = false;

const uint32_t CALIB_STREAM_INTERVAL_MS = 200;  // 5 Hz pitch stream

// Active error flags — each true condition repeats its message every ERROR_REPEAT_MS.
bool errorGPS = false;
bool errorIMU = false;

char currentFilename[13] = "";
// ---------------------------------------------

// ---------------- IMU values -----------------
float rawAx = 0.0f;
float rawAy = 0.0f;
float rawAz = 0.0f;

float gyrX = 0.0f;
float gyrY = 0.0f;

bool rawAccValid = false;
bool gyroValid   = false;

// Calibration — 120-sample (~2 s) settling period before logging starts.
// No bias computation needed; post-analysis handles all filtering.
const uint16_t CALIBRATION_SAMPLES_REQUIRED = 120;
bool     calibrationActive      = false;
bool     calibrationDone        = false;
uint16_t calibrationSampleCount = 0;
// ---------------------------------------------

// ---------------- GPS values -----------------
float speedKmh      = 0.0f;
bool  gpsValid      = false;
bool  lastFixState  = false;
bool  fixStateKnown = false;
// ---------------------------------------------

// ---------------- Auto-stop ------------------
// Logging stops automatically when GPS speed stays below AUTO_STOP_KMH
// for AUTO_STOP_HOLD_MS — but only after the bike has first moved
// (prevents immediate stop during the stationary calibration phase).
const float    AUTO_STOP_KMH     = 1.0f;
const uint32_t AUTO_STOP_HOLD_MS = 2500;
bool     autoStopEnabled = false;   // set true once speed first crosses threshold
bool     autoStopArmed   = false;   // set true when speed drops back below threshold
uint32_t autoStopSinceMs = 0;
// ---------------------------------------------

uint32_t sampleCount = 0;

// Stats
float maxSpeed    = 0.0f;
float maxAcc      = -1000000.0f;
float maxDecc     =  1000000.0f;
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
  if (!bno08x.enableReport(SH2_ACCELEROMETER, 10000)) {
    BLE.println("ERR rawacc");
    return false;
  }

  if (!bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000)) {
    BLE.println("ERR gyro");
    return false;
  }

  return true;
}

void resetCalibration() {
  calibrationActive      = false;
  calibrationDone        = false;
  calibrationSampleCount = 0;
}

void startCalibration() {
  calibrationActive      = true;
  calibrationDone        = false;
  calibrationSampleCount = 0;
  BLE.println("CAL");
}

void updateCalibration() {
  if (!calibrationActive || !rawAccValid) {
    return;
  }

  calibrationSampleCount++;

  if (calibrationSampleCount >= CALIBRATION_SAMPLES_REQUIRED) {
    calibrationActive = false;
    calibrationDone   = true;
    startMs   = millis();
    lastLogMs = 0;
    BLE.println("CAL OK");
  }
}

void resetStats() {
  sampleCount   = 0;
  maxSpeed      = 0.0f;
  maxAcc        = -1000000.0f;
  maxDecc       =  1000000.0f;
  maxHallF_Hz   = 0.0f;
  maxHallR_Hz   = 0.0f;
  statsReady    = false;
}

void updateStats() {
  sampleCount++;

  if (speedKmh > maxSpeed) {
    maxSpeed = speedKmh;
  }

  if (rawAx > maxAcc) {
    maxAcc = rawAx;
  }

  if (rawAx < maxDecc) {
    maxDecc = rawAx;
  }

  if (hallF_Hz > maxHallF_Hz) {
    maxHallF_Hz = hallF_Hz;
  }

  if (hallR_Hz > maxHallR_Hz) {
    maxHallR_Hz = hallR_Hz;
  }
}

// Sends last-session stats over BLE.
// Called immediately after stopLogging() and again any time the user
// sends 's' while statsReady — so stats survive a BLE dropout.
void sendStats() {
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

  BLE.print("Hall_Fmax:");
  BLE.print(maxHallF_Hz, 2);
  BLE.print(" Hall_Rmax:");
  BLE.println(maxHallR_Hz, 2);
}

// Format: STA fix:X cal:X log:X  [+ last-session stats if available]
void sendStatusBLE() {
  BLE.print("STA fix:");
  BLE.print(lastFixState ? "1" : "0");
  BLE.print(" cal:");
  BLE.print(calibrationDone ? "1" : "0");
  BLE.print(" log:");
  BLE.println(loggingActive ? "1" : "0");

  // Re-send last-session stats so the user gets them even after a BLE dropout.
  if (statsReady) {
    delay(60);
    sendStats();
  }
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

  logFile.println("t_s,gyro_x_rads,gyro_y_rads,raw_ax_mps2,raw_ay_mps2,raw_az_mps2,speed_kmh,hall_f_hz,hall_r_hz,gps_fix");
  logFile.flush();

  calibStreaming = false;   // stop alignment stream before logging

  rawAccValid = false;
  gyroValid   = false;
  resetCalibration();
  startCalibration();

  lastLogMs = 0;
  startMs   = millis();

  resetStats();

  autoStopEnabled = false;
  autoStopArmed   = false;

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
  statsReady    = true;

  BLE.print("SP ");
  BLE.println(currentFilename);

  sendStats();
}

void setup() {
  BLE.begin(9600);
  GPSSerial.begin(9600);
  delay(1500);

  BLE.println("BOOT");
  delay(120);

  Wire2.begin();
  Wire2.setClock(400000);
  BLE.println("I2C");
  delay(120);

  if (!bno08x.begin_I2C(BNO08x_I2CADDR_DEFAULT, &Wire2)) {
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
    } else if (cmd == 'c' || cmd == 'C') {
      calibStreaming = !calibStreaming;
      BLE.println(calibStreaming ? "PITCH_STREAM ON" : "PITCH_STREAM OFF");
      lastCalibStreamMs = 0;
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

  // ---- Auto-stop on low speed ----
  // Only armed once the bike has first reached AUTO_STOP_KMH, so the
  // stationary calibration phase never triggers a premature stop.
  if (loggingActive && calibrationDone) {
    if (speedKmh >= AUTO_STOP_KMH) {
      autoStopEnabled = true;
      autoStopArmed   = false;
    } else if (autoStopEnabled) {
      if (!autoStopArmed) {
        autoStopArmed   = true;
        autoStopSinceMs = millis();
      } else if (millis() - autoStopSinceMs >= AUTO_STOP_HOLD_MS) {
        BLE.println("AUTOSTOP");
        stopLogging();
        autoStopArmed   = false;
        autoStopEnabled = false;
      }
    }
  }

  // ---- IMU ----
  if (bno08x.wasReset()) {
    rawAccValid = false;
    gyroValid   = false;

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
      gyrX = sensorValue.un.gyroscope.x;
      gyrY = sensorValue.un.gyroscope.y;
      gyroValid = true;
    }

    updateCalibration();
  }

  // ---- Pitch alignment stream ('c' command) ----
  // Streams pitch angle derived from static accelerometer gravity vector so
  // the operator can bend/adjust the IMU mount plate to level it.
  // Disabled automatically when logging starts.
  if (calibStreaming && rawAccValid) {
    uint32_t nowMs = millis();
    if (lastCalibStreamMs == 0 || (nowMs - lastCalibStreamMs) >= CALIB_STREAM_INTERVAL_MS) {
      lastCalibStreamMs = nowMs;
      float pitch = atan2f(rawAx, sqrtf(rawAy * rawAy + rawAz * rawAz)) * 57.2958f;
      BLE.print("PITCH:");
      BLE.println(pitch, 2);
    }
  }

  // ---- SD logging ----
  if (loggingActive) {
    if (calibrationDone && rawAccValid && gpsValid) {
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

          logFile.print(tSec, 3);     logFile.print(',');
          logFile.print(gyrX, 4);     logFile.print(',');  // raw roll rate  rad/s
          logFile.print(gyrY, 4);     logFile.print(',');  // raw pitch rate rad/s
          logFile.print(rawAx, 4);    logFile.print(',');
          logFile.print(rawAy, 4);    logFile.print(',');
          logFile.print(rawAz, 4);    logFile.print(',');
          logFile.print(speedKmh, 2); logFile.print(',');
          logFile.print(hallF_Hz, 2); logFile.print(',');
          logFile.print(hallR_Hz, 2); logFile.print(',');
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
