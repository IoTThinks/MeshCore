#include "helpers/PowerSaving.h"

#if defined(ESP32_PLATFORM)
#include <esp_wifi.h>
#if defined(ENABLE_WIFI_INTERFACE)
#include <WiFi.h>
#endif

// Let LoRa-only images use the same policy without pulling in the Wi-Fi driver.
extern "C" esp_err_t esp_wifi_get_mode(wifi_mode_t* mode) __attribute__((weak));

namespace {
struct CpuClock {
  int min_mhz;
  int max_mhz;
  bool light_sleep;

  bool operator==(const CpuClock& other) const {
    return min_mhz == other.min_mhz && max_mhz == other.max_mhz
        && light_sleep == other.light_sleep;
  }
};

CpuClock requested_clock{};

#if defined(HELTEC_LORA_V4_OLED)
constexpr int companion_idle_mhz = 4;
#else
constexpr int companion_idle_mhz = 5;
#endif

int normalCpuClock(bool companion) {
#if defined(ESP32_CPU_FREQ)
  return ESP32_CPU_FREQ;
#else
  static const int boot_clock = getCpuFrequencyMhz();
  return companion ? 80 : boot_clock;
#endif
}

wifi_mode_t wifiMode() {
  wifi_mode_t mode;
  return esp_wifi_get_mode && esp_wifi_get_mode(&mode) == ESP_OK ? mode : WIFI_MODE_NULL;
}

bool bleIsOn() {
#if defined(BLE_PIN_CODE) && CONFIG_BT_ENABLED
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
#else
  return false;
#endif
}
} // namespace

void PowerSaving::applyCompanionPowerSaving(bool enabled) {
#if defined(ENABLE_WIFI_INTERFACE)
  WiFi.setSleep(enabled);  // Station modem sleep wakes for each DTIM beacon.
#endif
#if defined(BLE_PIN_CODE) && !CONFIG_IDF_TARGET_ESP32C6
  if (enabled) esp_bt_sleep_enable();
  else esp_bt_sleep_disable();
#endif
  updateCpuClock(enabled, true);
}

void PowerSaving::updateCpuClock(bool enabled, bool companion) {
  const int normal_mhz = normalCpuClock(companion);
  const int idle_mhz = companion ? companion_idle_mhz : 5;
  const CpuClock normal{normal_mhz, normal_mhz, false};
#if ESP32_HW_USB_CDC && (defined(BLE_PIN_CODE) || defined(ENABLE_USB_INTERFACE))
  const bool light_sleep = companion;
#else
  const bool light_sleep = false;
#endif
  CpuClock clock = normal;
  const wifi_mode_t wifi = wifiMode();
  const bool usb_host = hasUsbPendingWork();
  if (usb_host || wifi != WIFI_MODE_NULL) {
    const int peripheral_mhz = normal_mhz < 80 ? 80 : normal_mhz;
    clock = {peripheral_mhz, peripheral_mhz, false};
#if defined(ENABLE_WIFI_INTERFACE)
    if (!usb_host && enabled && companion && wifi == WIFI_MODE_STA) {
      // The station driver holds the 80 MHz bus lock while its modem is awake.
      const int station_idle_mhz = companion_idle_mhz < 5 && bleIsOn() ? 5 : idle_mhz;
      clock = {station_idle_mhz, 80, false};
    }
#endif
  } else if (enabled && bleIsOn()) {
    // The BLE controller requests an 80 MHz bus clock for radio events.
    clock = {idle_mhz, 80, light_sleep};
  } else if (enabled) {
    clock = {5, companion ? 40 : 20, light_sleep};
  }
  if (clock == requested_clock) return;
  requested_clock = clock;

  auto result = configurePowerManagement(clock.max_mhz, clock.min_mhz, clock.light_sleep);
  if (result == ESP_ERR_INVALID_ARG && clock.min_mhz < 5) {
    result = configurePowerManagement(clock.max_mhz, 5, clock.light_sleep);
  }
  // The original ESP32 needs at least 10 MHz for its 1 MHz REF_TICK.
  if (result == ESP_ERR_INVALID_ARG && clock.min_mhz <= 5) {
    result = configurePowerManagement(clock.max_mhz, 10, clock.light_sleep);
  }
  if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED && !(clock == normal)) {
    configurePowerManagement(normal_mhz, normal_mhz, false);
  }
}

bool PowerSaving::prepareWifi() {
  const int normal_mhz = normalCpuClock(false);
  const int wifi_mhz = normal_mhz < 80 ? 80 : normal_mhz;
  requested_clock = {};
  if (configurePowerManagement(wifi_mhz, wifi_mhz, false) == ESP_OK) {
    // The SDK applies new clock settings at the next PM transition.
    for (int i = 0; i < 10 && getCpuFrequencyMhz() < 80; ++i) vTaskDelay(1);
  }
  return getCpuFrequencyMhz() >= 80;
}

esp_err_t PowerSaving::configurePowerManagement(int max_freq_mhz, int min_freq_mhz, bool light_sleep_enable) {
  // Configure Power Management
#if CONFIG_IDF_TARGET_ESP32C3
  esp_pm_config_esp32c3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32S3
  esp_pm_config_esp32s3_t pm_config;
#elif CONFIG_IDF_TARGET_ESP32S2
  esp_pm_config_esp32s2_t pm_config;
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
  static bool host_present = false;
  const uint32_t now = millis();
  if (Serial.isPlugged()) {
    host_present = true;
    lastPluggedMs = now;
  } else if (now - lastPluggedMs >= USB_UNPLUG_DELAY_MS) {
    host_present = false;
  }
  return host_present;
#elif defined(ESP32_PLATFORM) && ARDUINO_USB_CDC_ON_BOOT == 1
  return static_cast<bool>(Serial);
#else
  return false; // UART boards (Heltec V3), nRF52, etc.
#endif
}

// Hold a PM lock while a PC is connected, so auto light sleep never breaks
// USB serial. Released when the PC is gone.
void PowerSaving::updateUsbPowerLocks() {
#if ESP32_HW_USB_CDC && CONFIG_PM_ENABLE
  // Lock handle and state are "static" so they persist between loop() calls
  static esp_pm_lock_handle_t no_sleep_lock = NULL; // Prevents auto light sleep while held
  static bool held = false;                         // True while we currently hold the lock

  // Create the lock once, on the first call.
  // NO_LIGHT_SLEEP: while held, FreeRTOS idle never enters auto light sleep.
  // Light sleep stops the USB Serial/JTAG peripheral and breaks the USB connection.
  if (no_sleep_lock == NULL &&
      esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb_no_ls", &no_sleep_lock) != ESP_OK) return;

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
