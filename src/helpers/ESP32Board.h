#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_system.h"

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  bool inhibit_sleep = false;
  volatile bool _pending_radio_irq = false;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;

  #ifdef ESP32_CPU_FREQ
    setCpuFrequencyMhz(ESP32_CPU_FREQ);
  #endif

  #ifdef PIN_VBAT_READ
    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
   #if PIN_BOARD_SDA >= 0 && PIN_BOARD_SCL >= 0
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
   #endif
  #else
    Wire.begin();
  #endif
  }

  // Temperature from ESP32 MCU
  float getMCUTemperature() override {
    uint32_t raw = 0;

    // To get and average the temperature so it is more accurate, especially in low temperature
    for (int i = 0; i < 4; i++) {
      raw += temperatureRead();
    }

    return raw / 4;
  }

  uint32_t getIRQGpio() {
    return P_LORA_DIO_1; // default for SX1262
  }

  bool hasPendingRadioIRQ() override {
    bool pending = _pending_radio_irq;
    _pending_radio_irq = false;
    return pending;
  }

  void sleep(uint32_t secs) override {
    // Skip if not allow to sleep
    if (inhibit_sleep) {
      delay(1); // Give MCU to OTA to run
      return;
    }

    uint32_t irq = getIRQGpio();
    if (irq == (uint32_t)-1) return;  // no IRQ pin configured, can't sleep safely
    gpio_num_t wakeupPin = (gpio_num_t)irq;

    // Configure timer wakeup
    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
    }

#if SOC_PM_SUPPORT_EXT1_WAKEUP
    if (rtc_gpio_is_valid_gpio(wakeupPin)) {
      // ext1 wakeup — does not interfere with GPIO edge interrupts,
      // so RadioLib's ISR fires normally on wake.
      esp_sleep_enable_ext1_wakeup(1ULL << wakeupPin, ESP_EXT1_WAKEUP_ANY_HIGH);

      noInterrupts();

      // Skip sleep if there is a LoRa packet already pending
      if (digitalRead(wakeupPin) == HIGH) {
        interrupts();
        return;
      }

      esp_light_sleep_start();
      interrupts();
      return;
    }
#endif

    // Fallback: gpio_wakeup for SoCs without ext1 (ESP32-C3/C6) or non-RTC pins
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable(wakeupPin, GPIO_INTR_HIGH_LEVEL);

    noInterrupts();

    if (digitalRead(wakeupPin) == HIGH) {
      gpio_wakeup_disable(wakeupPin);
      interrupts();
      return;
    }

    esp_light_sleep_start();

    // Restore edge interrupt for RadioLib ISR
    gpio_wakeup_disable(wakeupPin);
    gpio_set_intr_type(wakeupPin, GPIO_INTR_POSEDGE);

    // Signal radio layer — the ISR may not fire for a packet received during sleep
    if (digitalRead(wakeupPin) == HIGH) {
      _pending_radio_irq = true;
    }

    interrupts();
  }

  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#elif defined(P_LORA_TX_NEOPIXEL_LED)
  #define NEOPIXEL_BRIGHTNESS    64  // white brightness (max 255)

  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS);   // turn TX neopixel on (White)
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);   // turn TX neopixel off
  }
#endif

  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Generic ESP32";
  }

  void reboot() override {
    esp_restart();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;

  void setInhibitSleep(bool inhibit) {
    inhibit_sleep = inhibit;
  }
};

static RTC_NOINIT_ATTR uint32_t _rtc_backup_time;
static RTC_NOINIT_ATTR uint32_t _rtc_backup_magic;
#define RTC_BACKUP_MAGIC  0xAA55CC33
#define RTC_TIME_MIN      1772323200  // 1 Mar 2026

class ESP32RTCClock : public mesh::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      return;  // ESP-IDF preserves system time across deep sleep
    }
    // All other resets (power-on, crash, WDT, brownout) lose system time.
    // Restore from RTC backup if valid, otherwise use hardcoded seed.
    struct timeval tv;
    if (_rtc_backup_magic == RTC_BACKUP_MAGIC && _rtc_backup_time > RTC_TIME_MIN) {
      tv.tv_sec = _rtc_backup_time;
    } else {
      tv.tv_sec = 1772323200;  // 1 Mar 2026
    }
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    return _now;
  }
  void setCurrentTime(uint32_t time) override {
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    _rtc_backup_time = time;
    _rtc_backup_magic = RTC_BACKUP_MAGIC;
  }
  void tick() override {
    time_t now;
    time(&now);
    if (now > RTC_TIME_MIN && (uint32_t)now != _rtc_backup_time) {
      _rtc_backup_time = (uint32_t)now;
      _rtc_backup_magic = RTC_BACKUP_MAGIC;
    }
  }
};

#endif