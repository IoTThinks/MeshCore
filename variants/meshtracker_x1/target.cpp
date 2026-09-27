#include <Arduino.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

MeshTrackerX1Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock rtc_clock;
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
MeshTrackerX1SensorManager sensors = MeshTrackerX1SensorManager(nmea);

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
#endif

bool radio_init() {
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void MeshTrackerX1SensorManager::start_gps() {
  if (_nmea->isPowerSavingEnabled()) {
    gps_wake = true;       // gps_active is unchanged (true for GPS sleep, false for GPS off)
    _nmea->syncTime();     // Clear GPS data and force sync time
    _nmea->setNextSleep(); // Next time to off
  } else {
    gps_active = true;
    gps_wake = true;
  }

  // this init sequence comes from seeed examples and deals with all gps pins
  pinMode(GPS_EN, OUTPUT);
  digitalWrite(GPS_EN, HIGH);
  delay(10);
  pinMode(GPS_VRTC_EN, OUTPUT);
  digitalWrite(GPS_VRTC_EN, HIGH);
  delay(10);

  pinMode(GPS_RESET, OUTPUT);
  digitalWrite(GPS_RESET, HIGH);
  delay(10);
  digitalWrite(GPS_RESET, LOW);

  pinMode(GPS_SLEEP_INT, OUTPUT);
  digitalWrite(GPS_SLEEP_INT, HIGH);
  pinMode(GPS_RTC_INT, OUTPUT);
  digitalWrite(GPS_RTC_INT, LOW);
}

void MeshTrackerX1SensorManager::sleep_gps() {
  if (_nmea->isPowerSavingEnabled()) {
    gps_wake = false;      // gps_active is unchanged (true) even the GPS sleep (e.g: off)
    _nmea->stopTimeSync(); // Stop time sync
    _nmea->setNextWake();  // Next time to on
  } else {
    gps_active = false;
    gps_wake = false; // When GPS is off, wake is false to be sure
  }

  digitalWrite(GPS_VRTC_EN, HIGH);   // keep RTC alive for faster fix on wake
  digitalWrite(GPS_EN, LOW);
  digitalWrite(GPS_RESET, LOW);
  digitalWrite(GPS_SLEEP_INT, HIGH);
  digitalWrite(GPS_RTC_INT, LOW);
}

void MeshTrackerX1SensorManager::stop_gps() {
  if (_nmea->isPowerSavingEnabled()) {
    gps_wake = false;      // gps_active is unchanged (true) even the GPS sleep (e.g: off)
    _nmea->stopTimeSync(); // Stop time sync
    _nmea->setNextWake();  // Next time to on
  } else {
    gps_active = false;
    gps_wake = false; // When GPS is off, wake is false to be sure
  }

  digitalWrite(GPS_VRTC_EN, LOW);
  digitalWrite(GPS_EN, LOW);
  digitalWrite(GPS_RESET, LOW);
  digitalWrite(GPS_SLEEP_INT, HIGH);
  digitalWrite(GPS_RTC_INT, LOW);
}

bool MeshTrackerX1SensorManager::begin() {
  // init GPS
  Serial1.begin(GPS_BAUD_RATE);

  // init SPA06-003 barometer
  baro_ok = spa06.begin(SPA06_003_DEFAULT_ADDR, &Wire) || spa06.begin(0x76, &Wire);
  if (baro_ok) {
    spa06.setPressureOversampling(SPA06_003_OVERSAMPLE_8);
    spa06.setTemperatureOversampling(SPA06_003_OVERSAMPLE_8);
    // 1 Hz continuous keeps reads non-blocking at minimal power cost
    spa06.setPressureMeasureRate(SPA06_003_RATE_1);
    spa06.setTemperatureMeasureRate(SPA06_003_RATE_1);
    spa06.setMeasurementMode(SPA06_003_MEAS_CONTINUOUS_BOTH);
  }
  return true;
}

bool MeshTrackerX1SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  if (requester_permissions & TELEM_PERM_ENVIRONMENT && baro_ok) {
    telemetry.addTemperature(TELEM_CHANNEL_SELF, spa06.readTemperature());
    telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, spa06.readPressure());
  }
  return true;
}

void MeshTrackerX1SensorManager::loop() {
  static long next_gps_update = 0;

  // PowerSaving
  if (_nmea->isPowerSavingEnabled() && gps_active) {
    // Handle change in PowerSaving mode
    _nmea->updatePowerSavingSettings(gps_wake);

    if (gps_wake) {
      // GPS is awake: check whether it should sleep
      if ((int32_t)(millis() - _nmea->getNextSleep()) >= 0) {
        POWERSAVING_DEBUG_PRINTLN("GPS wake timeout. Enter sleep");
        sleep_gps();
      } else if (!_nmea->waitingTimeSync()) {
        POWERSAVING_DEBUG_PRINTLN("GPS set. Enter sleep early");
        sleep_gps();
      }
    } else {
      // GPS is asleep: check whether it should wake
      if ((int32_t)(millis() - _nmea->getNextWake()) >= 0) {
        POWERSAVING_DEBUG_PRINTLN("GPS sleep timeout. Wakeup.");
        start_gps();
      } else if (_nmea->waitingTimeSync()) {
        POWERSAVING_DEBUG_PRINTLN("CLI gps sync. Wakeup");
        start_gps();
      }
    }
  }

  if ((!_nmea->isPowerSavingEnabled() && gps_active) || (_nmea->isPowerSavingEnabled() && gps_wake)) {
    _nmea->loop();
  }

  if ((int32_t)(millis() - next_gps_update) >= 0) {
    if ((!_nmea->isPowerSavingEnabled() && gps_active) || (_nmea->isPowerSavingEnabled() && gps_wake)) {
      if (_nmea->isValid()) {
        node_lat = ((double)_nmea->getLatitude()) / 1000000.;
        node_lon = ((double)_nmea->getLongitude()) / 1000000.;
        MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
        node_altitude = ((double)_nmea->getAltitude()) / 1000.0;
        MESH_DEBUG_PRINTLN("lat %f lon %f alt %f", node_lat, node_lon, node_altitude);
      }

      // In powersaving mode, GPS is on and off. Only update data when GPS is on
      if (_nmea->isPowerSavingEnabled()) next_gps_update = millis() + 1000;
    }

    if (!_nmea->isPowerSavingEnabled()) next_gps_update = millis() + 1000;
  }
}

int MeshTrackerX1SensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* MeshTrackerX1SensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}
const char* MeshTrackerX1SensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}
bool MeshTrackerX1SensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      gps_active = false; // Disabled by CLI or App
      sleep_gps(); // sleep for faster fix !
    } else {
      gps_active = true; // Enabled by CLI or App
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
