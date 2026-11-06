#include "helpers/PowerSaving.h"

#if defined(ESP32_PLATFORM)
void PowerSaving::applyCompanionPowerSaving(bool powersaving, bool light_sleep) {
#if defined(BLE_PIN_CODE) && !CONFIG_IDF_TARGET_ESP32C6
  // The BT controller must be sleep-capable before the PM block may stop the CPU
  // while a connection is open, so light sleep implies modem sleep.
  if (powersaving || light_sleep) {
    esp_bt_sleep_enable();
  } else {
    esp_bt_sleep_disable();
  }
#endif

  int max_freq_mhz = 80;
#ifdef ESP32_CPU_FREQ
  max_freq_mhz = ESP32_CPU_FREQ;
#endif
  int min_freq_mhz = powersaving ? 10 : max_freq_mhz;

  esp_err_t err = configurePowerManagement(max_freq_mhz, min_freq_mhz, light_sleep);
  if (err != ESP_OK && light_sleep) {
    // Not available here (e.g. a BT controller that is not sleep-capable):
    // fall back to the awake profile instead of leaving it half-applied.
    POWERSAVING_DEBUG_PRINTLN("Light sleep unavailable, staying awake: %d", err);
    configurePowerManagement(max_freq_mhz, min_freq_mhz, false);
  }
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
#endif