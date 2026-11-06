#pragma once

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
    // Companion profile. `powersaving` keeps its original meaning (BLE modem
    // sleep + DVFS); `light_sleep` adds automatic light sleep.
    static void applyCompanionPowerSaving(bool powersaving, bool light_sleep);
    static esp_err_t configurePowerManagement(int max_freq_mhz, int min_freq_mhz, bool light_sleep_enable);
#endif
};