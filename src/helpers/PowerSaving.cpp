#include "helpers/PowerSaving.h"

#if defined(ESP32_PLATFORM)
// PowerSaving for companions
void PowerSaving::applyCompanionPowerSaving(bool powersaving_enabled) {
  if (powersaving_enabled) {
    enableCompanionPowerSaving();
  } else {
    disableCompanionPowerSaving();
  }
}

void PowerSaving::enableCompanionPowerSaving() {
#if defined(BLE_PIN_CODE) && !CONFIG_IDF_TARGET_ESP32C6
  // Enable BLE sleep
  esp_bt_sleep_enable();
#endif

  // Configure Power Management
#if ESP32_HW_USB_CDC && (defined(BLE_PIN_CODE) || defined(ENABLE_USB_INTERFACE))
  // Frequency scaling and auto light sleep
  // No lightsleep while connected to USB host by updateUsbPowerLocks() 
  // Heltec v4, Xiao S3/C3/C6 (BLE/USB companions)
  configurePowerManagement(80, 10, true);
#elif defined(BLE_PIN_CODE) || defined(ENABLE_USB_INTERFACE)
  // Frequency scaling and no light sleep
  // Heltec v3... (BLE/USB companions)
  configurePowerManagement(80, 10, false);
#endif
}

void PowerSaving::disableCompanionPowerSaving() {
#if defined(BLE_PIN_CODE) && !CONFIG_IDF_TARGET_ESP32C6
  // Disable BLE sleep
  esp_bt_sleep_disable();
#endif

  // Configure Power Management
#if defined(ESP32_CPU_FREQ)
  configurePowerManagement(ESP32_CPU_FREQ, ESP32_CPU_FREQ, false);
#else
  configurePowerManagement(80, 80, false);
#endif
}

esp_err_t PowerSaving::configurePowerManagement(int max_freq_mhz, int min_freq_mhz, bool light_sleep_enable) {
  // Configure Power Management
#if CONFIG_IDF_TARGET_ESP32C3
  esp_pm_config_esp32c3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32S3
  esp_pm_config_esp32s3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32
  esp_pm_config_esp32_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32C6
  esp_pm_config_t pm_config;
#endif

  // Set PM Config
  pm_config = { .max_freq_mhz = max_freq_mhz, .min_freq_mhz = min_freq_mhz, .light_sleep_enable = light_sleep_enable };
  esp_err_t errPM = esp_pm_configure(&pm_config);
  if (errPM == ESP_OK) {
    POWERSAVING_DEBUG_PRINTLN("Power Management configured successfully");
  } else {
    POWERSAVING_DEBUG_PRINTLN("Power Management failed to configure: %d\r\n", errPM);
  }

  return errPM;
}
#endif // ESP32_PLATFORM

// Only for boards using HW USB CDC (Heltec V4, XIAO S3/C3/C6...)
#if ESP32_HW_USB_CDC
#define USB_UNPLUG_DELAY_MS 5000
#endif

// Return true if MCU needs to stay awake for USB
bool PowerSaving::hasUsbPendingWork() {
#if ESP32_HW_USB_CDC
  static uint32_t lastPluggedMs = 0;
  if (Serial.isPlugged()) lastPluggedMs = millis(); // PC detected: Save the time
  if (lastPluggedMs == 0) return false; // Has not plugged to PC

  return millis() - lastPluggedMs < USB_UNPLUG_DELAY_MS;
#else
  return false; // UART boards (Heltec V3), nRF52, etc.
#endif
}

// Hold a PM lock while a PC is connected, so auto light sleep never breaks
// USB serial. Released when the PC is gone. Frequency scaling stays allowed.
void PowerSaving::updateUsbPowerLocks() {
#if ESP32_HW_USB_CDC
  // Lock handle and state are "static" so they persist between loop() calls
  static esp_pm_lock_handle_t no_sleep_lock = NULL; // Prevents auto light sleep while held
  static bool held = false;                         // True while we currently hold the lock

  // Create the lock once, on the first call.
  // NO_LIGHT_SLEEP: while held, FreeRTOS idle never enters auto light sleep.
  // Light sleep stops the USB Serial/JTAG peripheral and breaks the USB connection.
  if (no_sleep_lock == NULL) {
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb_no_ls", &no_sleep_lock);
  }

  // Debounced check: true while a PC (USB host) is connected,
  // and for USB_UNPLUG_DELAY_MS after it disappears
  bool connected = hasUsbPendingWork();

  // PC connected (and lock not yet held): block auto light sleep.
  // Locks are reference-counted, so acquire only once per connection (tracked by "held").
  if (connected && !held) {
    esp_pm_lock_acquire(no_sleep_lock);
    held = true;
  }

  // PC gone (and lock held): release it so auto light sleep is allowed again
  if (!connected && held) {
    esp_pm_lock_release(no_sleep_lock);
    held = false;
  }
#endif
}
