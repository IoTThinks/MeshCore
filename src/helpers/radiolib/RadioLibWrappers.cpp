
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

#define NF_CALIB_INTERVAL_MS 60000UL
#define NF_CALIB_TIMEOUT_MS 5000UL
#define NF_CALIB_SETTLE_MS 20UL
#define NF_CALIB_SAMPLE_INTERVAL_MS 1UL
#define NF_CALIB_MAX_SAMPLE_ATTEMPTS (NUM_NOISE_FLOOR_SAMPLES * 4U)

static volatile uint8_t state = STATE_IDLE;

// this function is called when a complete packet
// is transmitted by the module
static
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // we sent a packet, set the flag
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
  _preamble_sf = getSpreadingFactor();
  _radio->setPreambleLength(preambleLengthForSF(_preamble_sf)); // longer preamble for lower SF improves reliability
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;
  _cad_enabled = false;

  // start average out some samples
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

uint32_t RadioLibWrapper::getRngSeed() {
  return _radio->random(0x7FFFFFFF);
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
  prepareForRadioConfig();
  _radio->setOutputPower(dbm);
}

void RadioLibWrapper::idle() {
  if (_rx_ps_armed) stopReceiveDutyCycle();
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
}

void RadioLibWrapper::powerOff() {
  if (_rx_ps_armed) stopReceiveDutyCycle();
  _radio->sleep();
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {  // ignore trigger if currently sampling
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::doResetAGC() {
  _radio->sleep();  // warm sleep to reset analog frontend
}

void RadioLibWrapper::resetAGC() {
  // make sure we're not mid-receive or mid-transmit of a packet
  if (isPacketPendingOrReceiving() || state == STATE_TX_WAIT) return;

  if (_rx_ps_armed) stopReceiveDutyCycle();

  doResetAGC();
  state = STATE_IDLE;

  // Recalibrate synchronously so callers never observe the temporary zero
  // used to bypass a stale sampling threshold after an AGC reset.
  const int16_t previous_noise_floor = _noise_floor;
  _noise_floor = 0;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;

  _nf_calib_active = true;  // force startReceiveMode() into continuous RX
  bool packet_in_progress = false;
  int16_t err = startReceiveMode();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
    delay(NF_CALIB_SETTLE_MS);

    uint16_t sample_attempts = 0;
    while (_num_floor_samples < NUM_NOISE_FLOOR_SAMPLES &&
           sample_attempts++ < NF_CALIB_MAX_SAMPLE_ATTEMPTS) {
      if (isPacketPendingOrReceiving()) {
        packet_in_progress = true;
        break;
      }
      sampleNoiseFloorOnce();
      if (_num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
        delay(NF_CALIB_SAMPLE_INTERVAL_MS);
      }
    }
  }

  if (!publishNoiseFloor()) {
    _noise_floor = previous_noise_floor;
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }

  _nf_calib_active = false;
  _nf_last_calib = millis();

  // A frame can start after resetAGC()'s initial idle check. Keep continuous
  // RX alive until recvRaw() consumes it; restarting RX here would abort a
  // frame that is between preamble detection and RX_DONE.
  if (!packet_in_progress) packet_in_progress = isPacketPendingOrReceiving();
  if (!packet_in_progress) requestRestartRecv();
}

void RadioLibWrapper::sampleNoiseFloorOnce() {
  int rssi = getCurrentRSSI();
  if (rssi < _noise_floor + SAMPLING_THRESHOLD) {
    _num_floor_samples++;
    _floor_sample_sum += rssi;
  }
}

bool RadioLibWrapper::publishNoiseFloor() {
  if (_num_floor_samples < NUM_NOISE_FLOOR_SAMPLES || _floor_sample_sum == 0) return false;

  _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
  if (_noise_floor < -120) {
    _noise_floor = -120;    // clamp to lower bound of -120dBi
  }
  _floor_sample_sum = 0;

  #ifdef MESH_DEBUG_NOISE_FLOOR
  MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
  #endif
  return true;
}

// Clear the RX/idle state so the next loop calls startRecv() again, without
// dropping an STATE_INT_READY that the ISR may raise while we are in here.
// A plain `state = STATE_IDLE` loses that flag (and with it, a received
// packet) when setFlag() fires between the test and the store.
void RadioLibWrapper::requestRestartRecv() {
  noInterrupts();
  if ((state & ~STATE_INT_READY) != STATE_TX_WAIT) {
    state &= STATE_INT_READY;   // STATE_IDLE, but keep a pending interrupt
  }
  interrupts();
}

bool RadioLibWrapper::isPacketPendingOrReceiving() {
  return (state & STATE_INT_READY) != 0 || isReceivingPacket();
}

void RadioLibWrapper::noiseFloorCalibCheck() {
  unsigned long now = millis();
  if (_nf_calib_active) {
    if (!_rx_ps_enabled || (long)(now - _nf_calib_deadline) >= 0) {
      endNoiseFloorCalib(now);
    } else if (_rx_ps_armed && !isPacketPendingOrReceiving()) {
      // A packet may have delayed the RXPS-to-continuous-RX transition.
      requestRestartRecv();
    }
  } else if (_rx_ps_enabled && _rx_ps_armed && state == STATE_RX &&
             (_nf_last_calib == 0 || now - _nf_last_calib >= NF_CALIB_INTERVAL_MS) &&
             !isPacketPendingOrReceiving()) {
    _nf_calib_active = true;
    _nf_calib_deadline = now + NF_CALIB_TIMEOUT_MS;
    _nf_sample_from = now + NF_CALIB_SETTLE_MS;
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
    if (!isPacketPendingOrReceiving()) requestRestartRecv();
  }
}

void RadioLibWrapper::endNoiseFloorCalib(unsigned long now) {
  _nf_calib_active = false;
  _nf_last_calib = now;
  if (!isPacketPendingOrReceiving()) requestRestartRecv();
}

void RadioLibWrapper::loop() {
  noiseFloorCalibCheck();

  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!_rx_ps_armed && !(_nf_calib_active && (long)(millis() - _nf_sample_from) < 0) &&
        !isReceivingPacket()) {
      sampleNoiseFloorOnce();
    }
  } else if (publishNoiseFloor()) {
    if (_nf_calib_active) endNoiseFloorCalib(millis());
  }
}

void RadioLibWrapper::startRecv() {
  #if defined(USE_LR2021)
  _radio->standby(); // without this LR2021 can throw -706 when calling startReceive after hardware CAD when side detectors are enabled
  #endif
  int err = startReceiveMode();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

int16_t RadioLibWrapper::startReceiveMode() {
  // Periodic calibration switches RXPS to continuous RX. Re-check at the
  // hardware transition so a preamble that arrived after the scheduler's
  // idle check is not aborted by stopReceiveDutyCycle().
  if (_nf_calib_active && _rx_ps_armed && isPacketPendingOrReceiving()) {
    return RADIOLIB_ERR_NONE;
  }
  if (_rx_ps_armed) stopReceiveDutyCycle();
  if (!_rx_ps_enabled || _nf_calib_active) {
    _rx_ps_armed = false;
    _rx_ps_last_error = RADIOLIB_ERR_NONE;
    return _radio->startReceive();
  }

  if (!_rx_ps_arm_retry.canAttempt()) {
    // arming has failed repeatedly; stop paying for a doomed SPI round-trip on
    // every RX restart. A config change (setRxPowerSaving) re-enables retries.
    _rx_ps_armed = false;
    return _radio->startReceive();
  }

  const RadioLibIrqFlags_t irq_flags =
      RADIOLIB_IRQ_RX_DEFAULT_FLAGS |
      (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
  const RadioLibIrqFlags_t irq_mask =
      (1UL << RADIOLIB_IRQ_RX_DONE) |
      (1UL << RADIOLIB_IRQ_TIMEOUT) |
      (1UL << RADIOLIB_IRQ_CRC_ERR) |
      (1UL << RADIOLIB_IRQ_HEADER_ERR);

  uint32_t eff_rx_us = _rx_ps_rx_us;
  uint32_t eff_sleep_us = _rx_ps_sleep_us;
  int16_t err = armDutyCycle(irq_flags, irq_mask, &eff_rx_us, &eff_sleep_us);
  if (err == RADIOLIB_ERR_NONE) {
    _rx_ps_armed = true;
    _rx_ps_last_error = RADIOLIB_ERR_NONE;
    _rx_ps_arm_retry.recordSuccess();
    _rx_ps_eff_rx_us = eff_rx_us;
    _rx_ps_eff_sleep_us = eff_sleep_us;
    return err;
  }

  _rx_ps_armed = false;
  _rx_ps_last_error = err;
  _rx_ps_eff_rx_us = 0;
  _rx_ps_eff_sleep_us = 0;
  n_rx_ps_arm_failures++;
  _rx_ps_arm_retry.recordFailure();
  MESH_DEBUG_PRINTLN("RadioLibWrapper: startReceiveDutyCycle(%d), continuous RX fallback", err);
  int16_t fallback_err = _radio->startReceive();
  if (fallback_err != RADIOLIB_ERR_NONE) _rx_ps_last_error = fallback_err;
  return fallback_err;
}

void RadioLibWrapper::stopReceiveDutyCycle() {
  int16_t err = stopDutyCycleHardware();
  _rx_ps_armed = false;
  _rx_ps_eff_rx_us = 0;
  _rx_ps_eff_sleep_us = 0;
  if (err != RADIOLIB_ERR_NONE) _rx_ps_last_error = err;
}

bool RadioLibWrapper::isPacketReady() {
  if (!_rx_ps_armed) return true;
  return _radio->checkIrq(RADIOLIB_IRQ_RX_DONE) != 0;
}

void RadioLibWrapper::prepareForRadioConfig() {
  if (!_rx_ps_armed) return;

  stopReceiveDutyCycle();
  requestRestartRecv();
}

bool RadioLibWrapper::setRxPowerSaving(bool enabled, uint32_t rx_us, uint32_t sleep_us) {
  if (!isValidRxPowerSavingPeriod(rx_us) || !isValidRxPowerSavingPeriod(sleep_us)) return false;
  if (enabled && !supportsRxPowerSaving()) return false;

  _rx_ps_enabled = enabled;
  _rx_ps_rx_us = rx_us;
  _rx_ps_sleep_us = sleep_us;
  _rx_ps_last_error = RADIOLIB_ERR_NONE;
  _rx_ps_arm_retry.reset();   // new config, give the hardware a fresh chance
  requestRestartRecv();
  return true;
}

RxPowerSavingStatus RadioLibWrapper::getRxPowerSavingStatus() const {
  RxPowerSavingStatus status;
  status.supported = supportsRxPowerSaving();
  status.armed = _rx_ps_armed;
  status.last_error = _rx_ps_last_error;
  status.arm_failures = n_rx_ps_arm_failures;
  status.effective_rx_us = _rx_ps_eff_rx_us;
  status.effective_sleep_us = _rx_ps_eff_sleep_us;
  return status;
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (state & STATE_INT_READY) {
    if (isPacketReady()) {
      if (_rx_ps_armed) stopReceiveDutyCycle();
      len = _radio->getPacketLength();
      if (len > 0) {
        if (len > sz) { len = sz; }
        _last_snr = _radio->getSNR();
        _last_rssi = _radio->getRSSI();
        _last_metrics_valid = true;
        int err = _radio->readData(bytes, len);
        if (err != RADIOLIB_ERR_NONE) {
          MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
          len = 0;
          n_recv_errors++;
        } else {
        //  Serial.print("  readData() -> "); Serial.println(len);
          n_recv++;
        }
      }
    }
    #if defined(USE_LR2021)
    state = STATE_RX;     // LR2021 stays in Rx after readData, calling startReceive while still in Rx throws -706 errors
    #else
    state = STATE_IDLE;   // need another startReceive()
    #endif
  }

  if (state != STATE_RX) {
    startRecv();
  }
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  if (_rx_ps_armed) stopReceiveDutyCycle();
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_TX_WAIT;
    return true;
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();   // trigger another startRecv()
  _board->onAfterTransmit();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
}

int16_t RadioLibWrapper::performChannelScan() {
  return _radio->scanChannel();
}

bool RadioLibWrapper::isChannelActive() {
  // int.thresh: RSSI-based interference detection (relative to noise floor)
  if (_threshold != 0 && !(_rx_ps_armed && isChipBusy()) &&
      getCurrentRSSI() > _noise_floor + _threshold) return true;

  // cad: hardware channel activity detection
  if (_cad_enabled) {
    if (_rx_ps_armed) stopReceiveDutyCycle();
    int16_t result = performChannelScan();
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) return true;
  }

  return false;
}

float RadioLibWrapper::getLastRSSI() const {
  if (_last_metrics_valid) return _last_rssi;
  return _rx_ps_armed ? 0 : _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  if (_last_metrics_valid) return _last_snr;
  return _rx_ps_armed ? 0 : _radio->getSNR();
}

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};

float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;

  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}

PacketMillis RadioLibWrapper::calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols) {
  // based on RadioLib's calculateTimeOnAir()
  uint32_t tsym_us = ((uint32_t)10000 << sf) / (bw * 10);
  uint32_t sfCoeff1_x4 = (sf == 5 || sf == 6) ? 25 : 17; // 6.25 : 4.25, semtech magic numbers to account for sync word + sfd

  // preamble + syncword + sfd + header
  uint32_t preamble_us = (((preambleSymbols + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;

  // airtime for max packet at current radio settings
  uint32_t total_us   = _radio->getTimeOnAir(MAX_TRANS_UNIT);
  // airtime for payload only (no preamble, header or SOF)
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : 4000 - preamble_us; // fallback to 4 secs at worst case
  // rescale payload_us for max possible CR
  if (cr >= 5 && cr < 8) { payload_us = (payload_us * 8) / cr; }

  return PacketMillis {(preamble_us + 999) / 1000, (payload_us + 999) / 1000};
}
