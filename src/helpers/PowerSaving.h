#pragma once

#include <Arduino.h>
#include "MeshCore.h"

#if defined(ESP32_PLATFORM)
#include "esp_pm.h"
#include "esp_bt.h"
#include <esp_err.h>
#include "esp_sleep.h"
#include "driver/uart.h"
#endif

class PowerSaving {
public:
#if defined(ESP32_PLATFORM)
  static void applyCompanionPowerSaving(bool enabled);
  static void enableCompanionPowerSaving();
  static void disableCompanionPowerSaving();
  static esp_err_t configurePowerManagement(int max_freq_mhz, int min_freq_mhz, bool light_sleep_enable);
#endif

  static bool hasUsbPendingWork();
};