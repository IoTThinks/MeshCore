#pragma once

#include "CustomLR1110.h"
#include "RadioLibWrappers.h"
#include "LR11x0Reset.h"

class CustomLR1110Wrapper : public RadioLibWrapper {
public:
  CustomLR1110Wrapper(CustomLR1110& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    prepareForRadioConfig();
    ((CustomLR1110 *)_radio)->setFrequency(freq);
    ((CustomLR1110 *)_radio)->setSpreadingFactor(sf);
    ((CustomLR1110 *)_radio)->setBandwidth(bw);
    ((CustomLR1110 *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, preambleLengthForSF(sf));
    ((CustomLR1110 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomLR1110 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
  }

  bool isReceivingPacket() override {
    // While duty-cycling, BUSY marks the sleep phase: probing IRQ status over
    // SPI there could wake the chip and break the cycle. Only skip the probe
    // in that case - outside RXPS this must stay a real channel check, since
    // Dispatcher::checkSend() relies on it to avoid transmitting over others.
    if (_rx_ps_armed && ((CustomLR1110 *)_radio)->isChipBusy()) return false;
    return ((CustomLR1110 *)_radio)->isReceiving();
  }
  bool isChipBusy() override {
    return ((CustomLR1110 *)_radio)->isChipBusy();
  }
  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1110 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    auto airtime = RadioLibWrapper::getEstAirtimeFor(len_bytes);
    return airtime < 200 ? 200 : airtime;   // at least 200 millis
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(preambleLengthForSF(getSpreadingFactor())); // overcomes weird issues with small and big pkts
  }

  bool supportsRxPowerSaving() const override { return true; }

protected:
  int16_t armDutyCycle(RadioLibIrqFlags_t irq_flags, RadioLibIrqFlags_t irq_mask,
                       uint32_t* eff_rx_us, uint32_t* eff_sleep_us) override {
    *eff_sleep_us = _rx_ps_sleep_us;   // only the RX window can get clamped
    return ((CustomLR1110 *)_radio)->startReceiveDutyCycle(
        _rx_ps_rx_us, _rx_ps_sleep_us, irq_flags, irq_mask, eff_rx_us);
  }

  int16_t stopDutyCycleHardware() override {
    int16_t standby_err = _radio->standby();
    int16_t lf_err = ((CustomLR1110 *)_radio)->restoreLfClock();
    return standby_err != RADIOLIB_ERR_NONE ? standby_err : lf_err;
  }

public:

  uint8_t getSpreadingFactor() const override { return ((CustomLR1110 *)_radio)->getSpreadingFactor(); }
  
  bool setRxBoostedGainMode(bool en) override {
    prepareForRadioConfig();
    return ((CustomLR1110 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLR1110 *)_radio)->getRxBoostedGainMode();
  }

  void doResetAGC() override { lr11x0ResetAGC((LR11x0 *)_radio, ((CustomLR1110 *)_radio)->getFreqMHz(), getRxBoostedGainMode()); }
};
