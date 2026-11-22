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

// Boards using HW USB CDC (Heltec V4, XIAO S3/C3/C6...)
#if defined(ESP32_PLATFORM) && ARDUINO_USB_CDC_ON_BOOT == 1 && ARDUINO_USB_MODE == 1
#define ESP32_HW_USB_CDC 1
#else
#define ESP32_HW_USB_CDC 0
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
  static void updateUsbPowerLocks(); // Call in loop(): blocks auto light sleep while a PC is connected
};