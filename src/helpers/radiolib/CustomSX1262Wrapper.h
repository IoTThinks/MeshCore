#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

#ifndef USE_SX1262
#define USE_SX1262
#endif

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    prepareForRadioConfig();
    ((CustomSX1262 *)_radio)->setFrequency(freq);
    ((CustomSX1262 *)_radio)->setSpreadingFactor(sf);
    ((CustomSX1262 *)_radio)->setBandwidth(bw);
    ((CustomSX1262 *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, preambleLengthForSF(sf));
    ((CustomSX1262 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomSX1262 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
  }

  bool isReceivingPacket() override {
    // While duty-cycling, BUSY marks the sleep phase: probing IRQ flags over
    // SPI there could wake the chip and break the cycle. Only skip the probe
    // in that case - outside RXPS this must stay a real channel check, since
    // Dispatcher::checkSend() relies on it to avoid transmitting over others.
    if (_rx_ps_armed && ((CustomSX1262 *)_radio)->isChipBusy()) return false;
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  bool isChipBusy() override {
    return ((CustomSX1262 *)_radio)->isChipBusy();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1262 *)_radio)->getRSSI(false);
  }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomSX1262 *)_radio)->spreadingFactor; }
  virtual void powerOff() override {
    if (_rx_ps_armed) stopReceiveDutyCycle();
    ((CustomSX1262 *)_radio)->sleep(false);
  }

  bool supportsRxPowerSaving() const override { return true; }

  bool supportsRxPowerSavingRfRxDisable() const override {
  #if defined(SX126X_RXEN)
    return SX126X_RXEN != RADIOLIB_NC;
  #else
    return false;
  #endif
  }

  bool setRxPowerSavingRfRxDisabled(bool disabled) override {
    if (!supportsRxPowerSavingRfRxDisable()) return false;
    prepareForRadioConfig();
    ((CustomSX1262 *)_radio)->setRxPowerSavingRfRxDisabled(disabled);
    return true;
  }

  bool isRxPowerSavingRfRxDisabled() const override {
    return ((CustomSX1262 *)_radio)->isRxPowerSavingRfRxDisabled();
  }

protected:
  int16_t armDutyCycle(RadioLibIrqFlags_t irq_flags, RadioLibIrqFlags_t irq_mask,
                       uint32_t* eff_rx_us, uint32_t* eff_sleep_us) override {
    // RadioLib programs the SX126x with exactly what we ask for (it only
    // subtracts the wake-up transition from the sleep period internally).
    *eff_rx_us = _rx_ps_rx_us;
    *eff_sleep_us = _rx_ps_sleep_us;
    return ((CustomSX1262 *)_radio)->startReceiveDutyCycle(
        _rx_ps_rx_us, _rx_ps_sleep_us, irq_flags, irq_mask);
  }

  int16_t stopDutyCycleHardware() override {
    int16_t standby_err = _radio->standby();
    int16_t rtc_err = ((CustomSX1262 *)_radio)->stopRTC();
    return standby_err != RADIOLIB_ERR_NONE ? standby_err : rtc_err;
  }

public:
  bool setRxBoostedGainMode(bool en) override {
    prepareForRadioConfig();
    return ((CustomSX1262 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomSX1262 *)_radio)->getRxBoostedGainMode();
  }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
};
