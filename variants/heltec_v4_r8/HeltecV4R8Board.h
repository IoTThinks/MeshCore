#pragma once

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <helpers/ESP32Board.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/esp32/FemRxpsGate.h>
#include "LoRaFEMControl.h"

#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER (4.9f * 1.035f)
#endif

class HeltecV4R8Board : public ESP32Board {
  KeyValueStore* _prefs = NULL;
  FemRxpsGate fem_rxps_gate{P_LORA_BUSY, P_LORA_KCT8103L_PA_CTX};

  bool setLoRaFemLnaEnabled(bool enable);
  bool isLoRaFemLnaEnabled() const;
protected:
  float adc_mult = ADC_MULTIPLIER;

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;

  HeltecV4R8Board() : periph_power(PIN_VEXT_EN, PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void attachDynamicPrefs(KeyValueStore* prefs);
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override;

  FemRxpsGate& femRxpsGate() { return fem_rxps_gate; }

  void onBeforeLightSleep() override;
  void onAfterLightSleep(bool slept) override;
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void shutdownPeripherals() override;
  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;
    } else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override { return adc_mult; }
  const char* getManufacturerName() const override;
};
