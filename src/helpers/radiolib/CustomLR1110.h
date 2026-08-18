#pragma once

#include <RadioLib.h>
#include "MeshCore.h"
#include "RXPowerSaving.h"

class CustomLR1110 : public LR1110 {
  uint32_t _preambleMillis = 66;
  uint32_t _maxPayloadMillis = 3934;
  uint32_t _activityAt = 0;
  bool _headerSeen = false;
  bool _rx_boosted = false;

  public:
    CustomLR1110(Module *mod) : LR1110(mod) { }

    size_t getPacketLength(bool update) override {
      size_t len = LR1110::getPacketLength(update);
      if (len == 0 && getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR) {
        // we've just received a corrupted packet
        // this may have triggered a bug causing subsequent packets to be shifted
        // call standby() to return radio to known-good state
        // recvRaw will call startReceive() to restart rx
        MESH_DEBUG_PRINTLN("LR1110: got header err, calling standby()");
        standby();
      }
      return len;
    }
    
    float getFreqMHz() const { return freqMHz; }

    // Restores the LF clock configuration RadioLib's begin() installs. Arming
    // the duty cycle switches it to RC + BUSY-release (required by Semtech for
    // SetRxDutyCycle); without this the change would outlive RXPS being turned
    // off, and it also alters what the BUSY pin means to isChipBusy().
    int16_t restoreLfClock() {
      return configLfClock(
          RADIOLIB_LR11X0_LF_BUSY_RELEASE_DISABLED | RADIOLIB_LR11X0_LF_CLK_XOSC);
    }

    // effectiveRxPeriod reports the RX window actually programmed, which may be
    // stretched above the requested value to satisfy the extended-period rule.
    int16_t startReceiveDutyCycle(uint32_t rxPeriod, uint32_t sleepPeriod,
                                  RadioLibIrqFlags_t irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
                                  RadioLibIrqFlags_t irqMask = RADIOLIB_IRQ_RX_DEFAULT_MASK,
                                  uint32_t* effectiveRxPeriod = nullptr) {
      // Keep the conversion local until RadioLib uses 64-bit multiplication;
      // its pinned implementation overflows for longer microsecond periods.
      uint32_t symbolPeriod = (uint32_t)(((1000.0f * (float)(1UL << this->spreadingFactor)) /
                                          this->bandwidthKhz) + 0.999f);
      uint32_t transitionTime = this->tcxoDelay + 1000;
      if (sleepPeriod <= transitionTime) return RADIOLIB_ERR_INVALID_SLEEP_PERIOD;
      uint32_t programmedSleepPeriod = sleepPeriod - transitionTime;

      uint64_t requiredExtendedPeriod =
          ((uint64_t)this->preambleLengthLoRa + 11ULL) * symbolPeriod + 1000ULL;
      uint64_t extendedPeriod = 2ULL * rxPeriod + programmedSleepPeriod;
      if (extendedPeriod < requiredExtendedPeriod) {
        rxPeriod = (uint32_t)((requiredExtendedPeriod - programmedSleepPeriod + 1ULL) / 2ULL);
      }
      if (effectiveRxPeriod) *effectiveRxPeriod = rxPeriod;

      uint32_t rxPeriodRaw = (uint32_t)(((uint64_t)rxPeriod * 32768UL) / 1000000UL);
      uint32_t sleepPeriodRaw =
          (uint32_t)(((uint64_t)programmedSleepPeriod * 32768UL) / 1000000UL);
      if ((rxPeriodRaw & 0xFF000000) || rxPeriodRaw == 0) return RADIOLIB_ERR_INVALID_RX_PERIOD;
      if ((sleepPeriodRaw & 0xFF000000) || sleepPeriodRaw == 0) {
        return RADIOLIB_ERR_INVALID_SLEEP_PERIOD;
      }

      int16_t state = standby(RADIOLIB_LR11X0_STANDBY_RC);
      RADIOLIB_ASSERT(state);
      // Semtech requires the RC standby/RTC setup before SetRxDutyCycle.
      state = configLfClock(RADIOLIB_LR11X0_LF_CLK_RC | RADIOLIB_LR11X0_LF_BUSY_RELEASE_ENABLED);
      RADIOLIB_ASSERT(state);
      RadioModeConfig_t cfg = {
        .receive = {
          .timeout = RADIOLIB_LR11X0_RX_TIMEOUT_INF,
          .irqFlags = irqFlags,
          .irqMask = irqMask,
          .len = 0,
        }
      };

      state = stageMode(RADIOLIB_RADIO_MODE_RX, &cfg);
      RADIOLIB_ASSERT(state);
      return setRxDutyCycle(rxPeriodRaw, sleepPeriodRaw, RADIOLIB_LR11X0_RX_DUTY_CYCLE_MODE_RX);
    }

    int16_t setRxBoostedGainMode(bool en) {
      _rx_boosted = en;
      return LR1110::setRxBoostedGainMode(en);
    }

    bool getRxBoostedGainMode() const { return _rx_boosted; }

    int16_t startReceive() override {
      // include the PREAMBLE_DETECTED irq bit in reported flags.
      return LR1110::startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED), RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    bool isChipBusy() {
      uint32_t busy = this->mod->getGpio();
      return busy != RADIOLIB_NC && this->mod->hal->digitalRead(busy);
    }

    bool isReceiving() {
      uint32_t irq = getIrqStatus();
      bool preamble = irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED;      // bit 4
      bool header   = irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID; // bit 5
      bool hdrErr   = irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR;             // bit 6
      uint32_t now  = millis();
      if (hdrErr) {
        clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID | RADIOLIB_LR11X0_IRQ_HEADER_ERR);
        _activityAt = 0;
        _headerSeen = false;
        return false;
      }
      if (!header && _headerSeen) {
        // something cleared the header flag, reset our state.
        _activityAt = 0; _headerSeen = false;
        return false;
      }
      if (header) {
        if (!_headerSeen) { _headerSeen = true; _activityAt = now; };
        if (now - _activityAt > _maxPayloadMillis) {
          MESH_DEBUG_PRINTLN("Clearing header IRQ after %ums", _maxPayloadMillis);
          clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID | RADIOLIB_LR11X0_IRQ_HEADER_ERR);
          _activityAt = 0; _headerSeen = false;
          return false;
        }
        return true;
      }
      if (preamble) {
        if (_activityAt == 0) _activityAt = now;
        if (now - _activityAt > _preambleMillis) {
          clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
          _activityAt = 0;
          MESH_DEBUG_PRINTLN("Clearing preamble IRQ after %ums", _preambleMillis);

          return false;
        }
        return true;
      }
      _activityAt = 0; _headerSeen = false;
      return false;
    }
    
    void setPreambleMillis(uint32_t preambleMillis) {
      _preambleMillis = preambleMillis;
      MESH_DEBUG_PRINTLN("Set _preambleMillis=%u", _preambleMillis);
    }
    void setMaxPayloadMillis(uint32_t payloadMillis) {
      _maxPayloadMillis = payloadMillis;
      MESH_DEBUG_PRINTLN("Set _maxPayloadMillis=%u", _maxPayloadMillis);
    }

    uint8_t getSpreadingFactor() const { return spreadingFactor; }
};
