#include <Arduino.h>
#include "t1000e_sensors.h"
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include <helpers/sensors/AirohaSleep.h>

T1000eBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock rtc_clock;
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
T1000SensorManager sensors = T1000SensorManager(nmea);

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
#endif

#ifndef LORA_CR
  #define LORA_CR      5
#endif

#ifdef RF_SWITCH_TABLE
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5,
  RADIOLIB_LR11X0_DIO6,
  RADIOLIB_LR11X0_DIO7,
  RADIOLIB_LR11X0_DIO8,
  RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                 DIO5  DIO6  DIO7  DIO8
  { LR11x0::MODE_STBY,   {LOW,  LOW,  LOW,  LOW  }},
  { LR11x0::MODE_RX,     {HIGH, LOW,  LOW,  HIGH }},
  { LR11x0::MODE_TX,     {HIGH, HIGH, LOW,  HIGH }},
  { LR11x0::MODE_TX_HP,  {LOW,  HIGH, LOW,  HIGH }},
  { LR11x0::MODE_TX_HF,  {LOW,  LOW,  LOW,  LOW  }},
  { LR11x0::MODE_GNSS,   {LOW,  LOW,  HIGH, LOW  }},
  { LR11x0::MODE_WIFI,   {LOW,  LOW,  LOW,  LOW  }},
  END_OF_MODE_TABLE,
};
#endif

// Charge status LED logic (active low charger signals):
//
// EXT_PWR_DETECT | EXT_CHRG_DONE | EXT_CHRG_DETECT | State          | LED_RED
// ---------------+---------------+-----------------+----------------+-----------
// 0              | -             | -               | No power       | Off
// 1              | 0             | -               | Charge done    | Solid
// 1              | 1             | 0               | Charging       | Breathing
// 1              | 1             | 1               | Fault/unknown  | Fast blink
static void chargeLedTick() {
  static uint32_t lastPoll = 0;
  static uint32_t lastBlink = 0;
  static bool blinkOn = false;
  static uint8_t ledState = 0; // 0=off, 1=solid, 2=breathing, 3=fault

  // Charger status doesn't change faster than this
  if (millis() - lastPoll >= 500) {
    lastPoll = millis();

    if (!digitalRead(EXT_PWR_DETECT)) {
      ledState = 0;
      analogWrite(LED_RED, 0);
    } else if (digitalRead(EXT_CHRG_DONE) == LOW) {
      ledState = 1;
      analogWrite(LED_RED, 255);
    } else if (digitalRead(EXT_CHRG_DETECT) == LOW) {
      ledState = 2;
    } else {
      ledState = 3;
    }
  }

  // Breathing: triangle wave, 2 s period, updated every tick for smoothness
  if (ledState == 2) {
    const uint32_t PERIOD_MS = 2000;
    const uint8_t BRIGHT_MIN = 4;
    const uint8_t BRIGHT_MAX = 255;
    uint32_t phase = millis() % PERIOD_MS;
    uint32_t half = PERIOD_MS / 2;
    uint8_t level = (phase < half)
                        ? (uint8_t)((phase * (BRIGHT_MAX - BRIGHT_MIN)) / half + BRIGHT_MIN)
                        : (uint8_t)(((PERIOD_MS - phase) * (BRIGHT_MAX - BRIGHT_MIN)) / half + BRIGHT_MIN);
    analogWrite(LED_RED, level);
  }

  // Fault: fast blink, 200 ms on/off
  if (ledState == 3 && millis() - lastBlink >= 200) {
    lastBlink = millis();
    blinkOn = !blinkOn;
    analogWrite(LED_RED, blinkOn ? 255 : 0);
  }
}

bool radio_init() {
  //rtc_clock.begin(Wire);

#ifdef LR11X0_DIO3_TCXO_VOLTAGE
  float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.6f;
#endif

  SPI.setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI);
  SPI.begin();
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;  // fail
  }

  radio.setCRC(2);
  radio.explicitHeader();

#ifdef RF_SWITCH_TABLE
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#endif
#ifdef RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif

  return true;  // success
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void T1000SensorManager::start_gps() {
  if (_nmea->isPowerSavingEnabled()) {
    gps_wake = true;       // gps_active is unchanged (true for GPS sleep, false for GPS off)
    _nmea->syncTime();     // Clear GPS data and force sync time
    _nmea->setNextSleep(); // Next time to off
  } else {
    gps_active = true;
    gps_wake = true;
  }

  digitalWrite(GPS_EN, HIGH);
  delay(10);
  digitalWrite(GPS_RTC_INT, HIGH);
  delay(5);
  digitalWrite(GPS_RTC_INT, LOW);
}

void T1000SensorManager::stop_gps() {
  if (_nmea->isPowerSavingEnabled()) {
    gps_wake = false;      // gps_active is unchanged (true) even the GPS sleep (e.g: off)
    _nmea->stopTimeSync(); // Stop time sync
    _nmea->setNextWake();  // Next time to on
  } else {
    gps_active = false;
    gps_wake = false; // When GPS is off, wake is false to be sure
  }

  digitalWrite(GPS_VRTC_EN, HIGH);   // keep GPS RTC alive for faster fix on wake
  digitalWrite(GPS_RTC_INT, LOW);    // make sure this is LOW so we can pulse it to wake
  airohaEnterSleep(_nmea);           // send command to put the GPS into RTC backup sleep
  digitalWrite(GPS_EN, LOW);
}


bool T1000SensorManager::begin() {
  // init GPS
  Serial1.begin(115200);
  digitalWrite(GPS_RESET, HIGH);
  delay(10);
  digitalWrite(GPS_RESET, LOW);
  return true;
}

bool T1000SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  if (requester_permissions & TELEM_PERM_ENVIRONMENT) {
    // Firmware reports light as a 0-100 % scale, but expose it via Luminosity so app labels it "Luminosity".
    telemetry.addLuminosity(TELEM_CHANNEL_SELF, t1000e_get_light());
    telemetry.addTemperature(TELEM_CHANNEL_SELF, t1000e_get_temperature());
  }
  return true;
}

void T1000SensorManager::loop() {
  static long next_gps_update = 0;

  // PowerSaving
  if (_nmea->isPowerSavingEnabled() && gps_active) {
    // Handle change in PowerSaving mode
    _nmea->updatePowerSavingSettings(gps_wake);

    if (gps_wake) {
      // GPS is awake: check whether it should sleep
      if ((int32_t)(millis() - _nmea->getNextSleep()) >= 0) {
        POWERSAVING_DEBUG_PRINTLN("GPS wake timeout. Enter sleep");
        stop_gps();
      } else if (!_nmea->waitingTimeSync()) {
        POWERSAVING_DEBUG_PRINTLN("GPS set. Enter sleep early");
        stop_gps();
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

  chargeLedTick(); // every loop() iteration for smooth breathing animation

  if ((int32_t)(millis() - next_gps_update) >= 0) {
    if ((!_nmea->isPowerSavingEnabled() && gps_active) || (_nmea->isPowerSavingEnabled() && gps_wake)) {
      if (gps_active && _nmea->isValid()) {
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

int T1000SensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* T1000SensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}
const char* T1000SensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}
bool T1000SensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      gps_active = false; // Disabled by CLI or App
      stop_gps();
    } else {
      gps_active = true; // Enabled by CLI or App
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
