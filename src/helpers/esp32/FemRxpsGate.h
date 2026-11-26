#pragma once

#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32S3

#include <Arduino.h>
#include <MeshCore.h>
#include <soc/rtc_io_reg.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <ulp_common.h>
#include <esp32s3/ulp.h>
#include <helpers/KeyValueStore.h>

extern int _rtc_force_slow_end;

// Mirror SX1262 BUSY onto FEM CTX during light sleep.
class FemRxpsGate {
  static constexpr uint32_t FEM_ULP_PROG_W = 0x780;
  static constexpr uint32_t FEM_ULP_FLAG_W = 0x7FC;
  const gpio_num_t busy_pin;
  const gpio_num_t ctx_pin;
  KeyValueStore* _prefs = nullptr;
  bool fem_rxps = true;
  bool fem_rxps_active = false;
  bool (*rxps_armed)() = nullptr;
  uint32_t fem_rxps_sleeps = 0;

  static volatile uint32_t* femWord(uint32_t word) {
    return RTC_SLOW_MEM + word;
  }

  static bool rtcMemoryAvailable() {
    return reinterpret_cast<uintptr_t>(&_rtc_force_slow_end) <= reinterpret_cast<uintptr_t>(femWord(FEM_ULP_PROG_W));
  }

  void femUlpLoad() {
    const uint32_t busy = rtc_io_number_get(busy_pin);
    const uint32_t ctx = rtc_io_number_get(ctx_pin);
    const ulp_insn_t prog[] = {
      I_MOVI(R2, FEM_ULP_FLAG_W),
      I_LD(R0, R2, 0),
      I_BL(9, 1),
      I_RD_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + busy, RTC_GPIO_IN_NEXT_S + busy),
      I_BL(5, 1),
      I_WR_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + ctx, RTC_GPIO_OUT_DATA_W1TS_S + ctx, 1),
      I_MOVI(R0, 1),
      I_ST(R0, R2, 2),
      I_BXI(FEM_ULP_PROG_W + 1),
      I_WR_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + ctx, RTC_GPIO_OUT_DATA_W1TC_S + ctx, 1),
      I_BXI(FEM_ULP_PROG_W + 1),
      I_MOVI(R0, 0),
      I_ST(R0, R2, 1),
      I_HALT(),
    };
    static_assert(sizeof(prog) <= (FEM_ULP_FLAG_W - FEM_ULP_PROG_W) * sizeof(uint32_t),
                  "ULP program overlaps flags");
    memcpy((void*)femWord(FEM_ULP_PROG_W), prog, sizeof(prog));
  }

  const char* femRxpsInactive(bool can_control, bool lna_enabled) const {
    if (!rtcMemoryAvailable()) return "RTC memory in use";
    if (!can_control) return "no switchable LNA";
    if (!lna_enabled) return "LNA off";
    if (!rxps_armed || !rxps_armed()) return "RXPS not armed";
    return nullptr;
  }

  void femRxpsRelease(bool slept) {
    *femWord(FEM_ULP_FLAG_W) = 0;
    uint32_t t0 = micros();
    while ((*femWord(FEM_ULP_FLAG_W + 1) & 0xFFFF) != 0 && micros() - t0 < 2000) { }
    ulp_timer_stop();
    if (slept && (*femWord(FEM_ULP_FLAG_W + 2) & 0xFFFF)) fem_rxps_sleeps++;
    rtc_gpio_deinit(ctx_pin);
    digitalWrite(ctx_pin, LOW);
    rtc_gpio_deinit(busy_pin);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_AUTO);
    fem_rxps_active = false;
  }

public:
  FemRxpsGate(int busy, int ctx) : busy_pin(static_cast<gpio_num_t>(busy)), ctx_pin(static_cast<gpio_num_t>(ctx)) { }

  void setArmedProbe(bool (*probe)()) { rxps_armed = probe; }

  void begin() {
    if (!rtcMemoryAvailable()) {
      MESH_DEBUG_PRINTLN("FEM RXPS disabled: RTC memory in use");
      return;
    }
    ulp_timer_stop();  // The ULP survives soft reset.
    *femWord(FEM_ULP_FLAG_W) = 0;
    rtc_gpio_deinit(ctx_pin);
    rtc_gpio_deinit(busy_pin);
  }

  void beforeSleep(bool can_control, bool lna_enabled) {
    if (!fem_rxps || fem_rxps_active || femRxpsInactive(can_control, lna_enabled) != nullptr) return;
    femUlpLoad();
    rtc_gpio_init(busy_pin);
    rtc_gpio_set_direction(busy_pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_set_level(ctx_pin, 0);
    rtc_gpio_init(ctx_pin);
    rtc_gpio_set_direction(ctx_pin, RTC_GPIO_MODE_OUTPUT_ONLY);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    *femWord(FEM_ULP_FLAG_W) = 1;
    *femWord(FEM_ULP_FLAG_W + 1) = 1;
    *femWord(FEM_ULP_FLAG_W + 2) = 0;
    // The timer starts the ULP loop once.
    ulp_set_wakeup_period(0, 100);
    fem_rxps_active = true;
    if (ulp_run(FEM_ULP_PROG_W) != ESP_OK) {
      femRxpsRelease(false);
    }
  }

  void afterSleep(bool slept) {
    if (fem_rxps_active) femRxpsRelease(slept);
  }

  void attachPrefs(KeyValueStore* prefs) {
    _prefs = prefs;
    char fem_rxps_pref[8] = { 0 };
    if (_prefs->getByKey("fem_rxps", fem_rxps_pref, 7)) fem_rxps = strcmp(fem_rxps_pref, "0") != 0;
  }

  bool handleCommand(const char* command, char* reply, bool can_control, bool lna_enabled) {
    if (strcmp(command, "get radio.fem.rxps") == 0) {
      if (!can_control || !rxps_armed || !_prefs) {
        strcpy(reply, "Error: unsupported");
      } else if (!fem_rxps) {
        strcpy(reply, "> off");
      } else {
        char powersaving[2] = {};
        const char* why = femRxpsInactive(can_control, lna_enabled);
        if (_prefs->getByKey("pwr_sav_en", powersaving, sizeof(powersaving)) && powersaving[0] == '0') {
          why = "MCU power saving off";
        }
        if (why) sprintf(reply, "> on, inactive (%s), %u gated sleeps", why, (unsigned)fem_rxps_sleeps);
        else if (fem_rxps_sleeps == 0) strcpy(reply, "> on, waiting for first bypass");
        else sprintf(reply, "> on, %u gated sleeps", (unsigned)fem_rxps_sleeps);
      }
      return true;
    }
    if (strncmp(command, "set radio.fem.rxps ", 19) == 0) {
      if (!can_control || !rxps_armed || !_prefs) {
        strcpy(reply, "Error: unsupported");
      } else if (strcmp(&command[19], "on") == 0 || strcmp(&command[19], "off") == 0) {
        const bool enabled = command[20] == 'n';
        if (_prefs->setByKey("fem_rxps", enabled ? "1" : "0")) {
          fem_rxps = enabled;
          strcpy(reply, fem_rxps ? "OK - LoRa FEM LNA gated while RXPS sleeps" : "OK - LoRa FEM LNA left as set during RXPS");
        } else {
          strcpy(reply, "Error: FEM RXPS preference unavailable");
        }
      } else {
        strcpy(reply, "Error: state must be on or off");
      }
      return true;
    }
    return false;
  }
};

#endif
