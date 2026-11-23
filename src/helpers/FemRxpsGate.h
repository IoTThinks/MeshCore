#pragma once

#include <Arduino.h>
#include <soc/rtc_io_reg.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <ulp_common.h>
#include <esp32s3/ulp.h>
#include "KeyValueStore.h"

extern int _rtc_force_slow_end;

// The ULP mirrors SX1262 BUSY onto KCT8103L CTX during CPU light sleep.
class FemRxpsGate {
  static constexpr uint32_t FEM_ULP_PROG_W = 0x780;
  static constexpr uint32_t FEM_ULP_FLAG_W = 0x7FC;
  const gpio_num_t busy_pin;
  const gpio_num_t ctx_pin;
  KeyValueStore* _prefs = nullptr;
  bool fem_rxps = true;
  bool fem_rxps_active = false;
  bool fem_ulp_loaded = false;
  uint32_t fem_rxps_sleeps = 0;

  static volatile uint32_t* femWord(uint32_t word) {
    return reinterpret_cast<volatile uint32_t*>(0x50000000UL + word * 4);
  }

  static bool rtcMemoryAvailable() {
    return reinterpret_cast<uintptr_t>(&_rtc_force_slow_end) <= 0x50000000UL + FEM_ULP_PROG_W * 4;
  }

  void femUlpLoad() {
    const uint32_t busy = rtc_io_number_get(busy_pin);
    const uint32_t ctx = rtc_io_number_get(ctx_pin);
    const ulp_insn_t prog[] = {
      I_MOVI(R2, FEM_ULP_FLAG_W),
      I_LD(R0, R2, 0),
      I_BL(7, 1),                                                                                 // disarmed
      I_RD_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + busy, RTC_GPIO_IN_NEXT_S + busy),
      I_BL(3, 1),
      I_WR_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + ctx, RTC_GPIO_OUT_DATA_W1TS_S + ctx, 1),  // radio asleep: LNA bypass
      I_BXI(FEM_ULP_PROG_W + 1),
      I_WR_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + ctx, RTC_GPIO_OUT_DATA_W1TC_S + ctx, 1),  // radio listening: LNA on
      I_BXI(FEM_ULP_PROG_W + 1),
      I_MOVI(R0, 0),
      I_ST(R0, R2, 1),
      I_HALT(),
    };
    memcpy((void*)femWord(FEM_ULP_PROG_W), prog, sizeof(prog));
    fem_ulp_loaded = true;
  }

  const char* femRxpsInactive(bool can_control, bool lna_enabled) const {
    if (!rtcMemoryAvailable()) return "RTC memory in use";
    if (!can_control) return "no switchable LNA";
    if (!lna_enabled) return "LNA off";
    char rxps[8] = { 0 };
    if (!_prefs || !_prefs->getByKey("rxps_en", rxps, 7) || strcmp(rxps, "1") != 0) return "RXPS off";
    return nullptr;
  }

  void femRxpsRelease() {
    *femWord(FEM_ULP_FLAG_W) = 0;
    uint32_t t0 = micros();
    while ((*femWord(FEM_ULP_FLAG_W + 1) & 0xFFFF) != 0 && micros() - t0 < 2000) { }
    ulp_timer_stop();
    rtc_gpio_deinit(ctx_pin);
    digitalWrite(ctx_pin, LOW);
    rtc_gpio_deinit(busy_pin);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
    fem_rxps_active = false;
  }

public:
  FemRxpsGate(int busy, int ctx) : busy_pin(static_cast<gpio_num_t>(busy)), ctx_pin(static_cast<gpio_num_t>(ctx)) { }

  void begin() {
    if (!rtcMemoryAvailable()) return;
    ulp_timer_stop();   // a soft reset keeps the ULP running
    *femWord(FEM_ULP_FLAG_W) = 0;
    rtc_gpio_deinit(ctx_pin);
    rtc_gpio_deinit(busy_pin);
  }

  void beforeSleep(bool can_control, bool lna_enabled) {
    if (!fem_rxps || fem_rxps_active || femRxpsInactive(can_control, lna_enabled) != nullptr) return;
    if (!fem_ulp_loaded) femUlpLoad();
    rtc_gpio_init(busy_pin);
    rtc_gpio_set_direction(busy_pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_init(ctx_pin);
    rtc_gpio_set_direction(ctx_pin, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(ctx_pin, 0);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    *femWord(FEM_ULP_FLAG_W) = 1;
    *femWord(FEM_ULP_FLAG_W + 1) = 1;
    ulp_set_wakeup_period(0, 100);
    fem_rxps_active = true;
    if (ulp_run(FEM_ULP_PROG_W) != ESP_OK) {
      femRxpsRelease();
      return;
    }
    fem_rxps_sleeps++;
  }

  void afterSleep() {
    if (fem_rxps_active) femRxpsRelease();
  }

  void attachPrefs(KeyValueStore* prefs) {
    _prefs = prefs;
    char fem_rxps_pref[8] = { 0 };
    if (_prefs->getByKey("fem_rxps", fem_rxps_pref, 7)) fem_rxps = strcmp(fem_rxps_pref, "0") != 0;
  }

  bool handleCommand(const char* command, char* reply, bool can_control, bool lna_enabled) {
    if (strcmp(command, "get radio.fem.rxps") == 0) {
      if (!can_control) {
        strcpy(reply, "Error: unsupported");
      } else if (!fem_rxps) {
        strcpy(reply, "> off");
      } else {
        const char* why = femRxpsInactive(can_control, lna_enabled);
        if (why) sprintf(reply, "> on, inactive (%s), %u gated sleeps", why, (unsigned)fem_rxps_sleeps);
        else sprintf(reply, "> on, %u gated sleeps", (unsigned)fem_rxps_sleeps);
      }
      return true;
    }
    if (memcmp(command, "set radio.fem.rxps ", 19) == 0) {
      if (!can_control) {
        strcpy(reply, "Error: unsupported");
      } else if (strcmp(&command[19], "on") == 0 || strcmp(&command[19], "off") == 0) {
        fem_rxps = command[20] == 'n';
        if (_prefs) _prefs->setByKey("fem_rxps", fem_rxps ? "1" : "0");
        strcpy(reply, fem_rxps ? "OK - LoRa FEM LNA gated while RXPS sleeps" : "OK - LoRa FEM LNA left as set during RXPS");
      } else {
        strcpy(reply, "Error: state must be on or off");
      }
      return true;
    }
    return false;
  }
};
