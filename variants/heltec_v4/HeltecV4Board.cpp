#include "HeltecV4Board.h"

// PowerSaving variant: ULP gates the FEM LNA with BUSY during light sleep
#include <soc/rtc_io_reg.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <ulp_common.h>
#include <esp32s3/ulp.h>

#define FEM_ULP_PROG_W 0x780   // 0x50001E00
#define FEM_ULP_FLAG_W 0x7FC   // armed, running
extern int _rtc_force_slow_end;
static bool fem_ulp_loaded = false;
static inline volatile uint32_t* femWord(uint32_t w) { return (volatile uint32_t*)(0x50000000UL + w * 4); }

static void femUlpLoad() {
  const uint32_t busy = rtc_io_number_get((gpio_num_t)P_LORA_BUSY);
  const uint32_t ctx = rtc_io_number_get((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
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

void HeltecV4Board::begin() {
    ESP32Board::begin();


    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    loRaFEMControl.init();
    femRxpsBoot();

    periph_power.begin();
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
    }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void HeltecV4Board::onBeforeTransmit(void) {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    loRaFEMControl.setTxModeEnable();
  }

  void HeltecV4Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    loRaFEMControl.setRxModeEnable();
  }

  void HeltecV4Board::shutdownPeripherals() {
    ESP32Board::shutdownPeripherals();

    // Turn off PA
    digitalWrite(P_LORA_PA_POWER, LOW);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
  }

  uint16_t HeltecV4Board::getBattMilliVolts()  {
    analogReadResolution(10);
    digitalWrite(PIN_ADC_CTRL, HIGH);
    delay(10);
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    digitalWrite(PIN_ADC_CTRL, LOW);

    return (adc_mult * (3.3 / 1024.0) * raw) * 1000;
  }

  const char* HeltecV4Board::getManufacturerName() const {
#ifdef HELTEC_LORA_V4_TFT
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4.3 TFT" : "Heltec V4 TFT";
#else
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4.3 OLED" : "Heltec V4 OLED";
#endif
  }

  bool HeltecV4Board::setLoRaFemLnaEnabled(bool enable) {
    if (!loRaFEMControl.isLnaCanControl()) {
      return false;
    }

    loRaFEMControl.setLNAEnable(enable);
    loRaFEMControl.setRxModeEnable();
    return true;
  }

  bool HeltecV4Board::isLoRaFemLnaEnabled() const {
    return loRaFEMControl.isLNAEnabled();
  }

void HeltecV4Board::attachDynamicPrefs(KeyValueStore* prefs) {
  _prefs = prefs;

  char radio_fem_rxgain[8] = { 0 };
  _prefs->getByKey("fem_rxgain", radio_fem_rxgain, 7);  // get initial values

  setLoRaFemLnaEnabled(strcmp(radio_fem_rxgain, "1") == 0);

  char fem_rxps_pref[8] = { 0 };
  if (_prefs->getByKey("fem_rxps", fem_rxps_pref, 7)) fem_rxps = strcmp(fem_rxps_pref, "0") != 0;
}

const char* HeltecV4Board::femRxpsInactive() {
  if ((uintptr_t)&_rtc_force_slow_end > 0x50000000UL + FEM_ULP_PROG_W * 4) return "RTC memory in use";
  if (!loRaFEMControl.isLnaCanControl()) return "no switchable LNA";
  if (!isLoRaFemLnaEnabled()) return "LNA off";
  char rxps[8] = { 0 };
  if (!_prefs || !_prefs->getByKey("rxps_en", rxps, 7) || strcmp(rxps, "1") != 0) return "RXPS off";
  return nullptr;
}

void HeltecV4Board::femRxpsBoot() {
  ulp_timer_stop();   // a soft reset keeps the ULP running
  *femWord(FEM_ULP_FLAG_W) = 0;
  rtc_gpio_deinit((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
  rtc_gpio_deinit((gpio_num_t)P_LORA_BUSY);
}

void HeltecV4Board::femRxpsRelease() {
  *femWord(FEM_ULP_FLAG_W) = 0;
  uint32_t t0 = micros();
  while ((*femWord(FEM_ULP_FLAG_W + 1) & 0xFFFF) != 0 && micros() - t0 < 2000) { }
  ulp_timer_stop();
  rtc_gpio_deinit((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
  digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
  rtc_gpio_deinit((gpio_num_t)P_LORA_BUSY);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
  fem_rxps_active = false;
}

void HeltecV4Board::onBeforeLightSleep() {
  if (!fem_rxps || fem_rxps_active || femRxpsInactive() != nullptr) return;
  if (!fem_ulp_loaded) femUlpLoad();
  rtc_gpio_init((gpio_num_t)P_LORA_BUSY);
  rtc_gpio_set_direction((gpio_num_t)P_LORA_BUSY, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_init((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
  rtc_gpio_set_direction((gpio_num_t)P_LORA_KCT8103L_PA_CTX, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level((gpio_num_t)P_LORA_KCT8103L_PA_CTX, 0);
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

void HeltecV4Board::onAfterLightSleep() {
  if (fem_rxps_active) femRxpsRelease();
}

bool HeltecV4Board::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  // PowerSaving variant
  if (strcmp(command, "get radio.fem.rxps") == 0) {
    if (!loRaFEMControl.isLnaCanControl()) {
      strcpy(reply, "Error: unsupported");
    } else if (!fem_rxps) {
      strcpy(reply, "> off");
    } else {
      const char* why = femRxpsInactive();
      if (why) sprintf(reply, "> on, inactive (%s), %u gated sleeps", why, (unsigned)fem_rxps_sleeps);
      else sprintf(reply, "> on, %u gated sleeps", (unsigned)fem_rxps_sleeps);
    }
    return true;
  }
  if (memcmp(command, "set radio.fem.rxps ", 19) == 0) {
    if (!loRaFEMControl.isLnaCanControl()) {
      strcpy(reply, "Error: unsupported");
    } else if (strcmp(&command[19], "on") == 0 || strcmp(&command[19], "off") == 0) {
      fem_rxps = command[20] == 'n';
      _prefs->setByKey("fem_rxps", fem_rxps ? "1" : "0");
      strcpy(reply, fem_rxps ? "OK - LoRa FEM LNA gated while RXPS sleeps" : "OK - LoRa FEM LNA left as set during RXPS");
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
    return true;
  }
  if (strcmp(command, "get radio.fem.rxgain") == 0) {
    if (!loRaFEMControl.isLnaCanControl()) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %s", isLoRaFemLnaEnabled() ? "on" : "off");
    }
    return true;
  }
  if (memcmp(command, "set radio.fem.rxgain ", 21) == 0) {
    if (!loRaFEMControl.isLnaCanControl()) {
      strcpy(reply, "Error: unsupported");
    } else if (memcmp(&command[21], "on", 2) == 0) {
      if (setLoRaFemLnaEnabled(true)) {
        _prefs->setByKey("fem_rxgain", "1");
        strcpy(reply, "OK - LoRa FEM RX gain on");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else if (memcmp(&command[21], "off", 3) == 0) {
      if (setLoRaFemLnaEnabled(false)) {
        _prefs->setByKey("fem_rxgain", "0");
        strcpy(reply, "OK - LoRa FEM RX gain off");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
    return true;
  }

  return false; // not handled
}
