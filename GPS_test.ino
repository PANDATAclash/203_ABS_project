#include <Adafruit_GPS.h>

#define GPSSerial Serial2

Adafruit_GPS GPS(&GPSSerial);

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("GPS Serial2 monitor");

  GPSSerial.begin(9600);
  GPS.begin(9600);

  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);

  delay(1000);
}

void loop() {
  GPS.read();

  if (GPS.newNMEAreceived()) {
    if (!GPS.parse(GPS.lastNMEA())) {
      return;
    }

    Serial.print("Fix: ");
    Serial.print((int)GPS.fix);

    Serial.print("  Quality: ");
    Serial.print((int)GPS.fixquality);

    Serial.print("  Sats: ");
    Serial.print((int)GPS.satellites);

    Serial.print("  Lat: ");
    Serial.print(GPS.latitude, 4);
    Serial.print(GPS.lat);

    Serial.print("  Lon: ");
    Serial.print(GPS.longitude, 4);
    Serial.print(GPS.lon);

    Serial.print("  Alt(m): ");
    Serial.print(GPS.altitude);

    Serial.print("  Speed(knots): ");
    Serial.print(GPS.speed);

    Serial.print("  Speed(km/h): ");
    Serial.println(GPS.speed * 1.852f, 2);
  }
}