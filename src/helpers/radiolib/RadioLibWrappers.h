#pragma once

#include <Mesh.h>
#include <RadioLib.h>
#include "RXPowerSaving.h"

#ifdef USE_CC310_HW_CRYPTO
#include <Adafruit_nRFCrypto.h>
#endif
struct PacketMillis {
  uint32_t preambleMillis;  // preamble-detect -> header-valid deadline
  uint32_t payloadMillis;   // header-valid   -> rx-done deadline
};

class RadioLibWrapper : public mesh::Radio, public RxPowerSavingControl {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  uint32_t n_recv, n_sent, n_recv_errors;
  int16_t _noise_floor, _threshold;
  float _last_rssi, _last_snr;
  bool _last_metrics_valid;
  bool _cad_enabled;
  uint16_t _num_floor_samples;
  int32_t _floor_sample_sum;
  uint8_t _preamble_sf;

  bool _rx_ps_enabled;
  bool _rx_ps_armed;
  uint32_t _rx_ps_rx_us;
  uint32_t _rx_ps_sleep_us;
  uint32_t _rx_ps_eff_rx_us;      // periods the driver actually armed with
  uint32_t _rx_ps_eff_sleep_us;
  int16_t _rx_ps_last_error;
  uint32_t n_rx_ps_arm_failures;
  RxPowerSavingArmRetryState _rx_ps_arm_retry;

  bool _nf_calib_active;
  unsigned long _nf_last_calib;
  unsigned long _nf_calib_deadline;
  unsigned long _nf_sample_from;

  void idle();
  void startRecv();
  void requestRestartRecv();
  bool isPacketPendingOrReceiving();
  void prepareForRadioConfig();
  void sampleNoiseFloorOnce();
  bool publishNoiseFloor();
  void noiseFloorCalibCheck();
  void endNoiseFloorCalib(unsigned long now);
  int16_t startReceiveMode();
  void stopReceiveDutyCycle();
  // eff_rx_us/eff_sleep_us report the periods the driver really programmed,
  // which may be clamped to satisfy a hardware constraint.
  virtual int16_t armDutyCycle(RadioLibIrqFlags_t irq_flags,
                               RadioLibIrqFlags_t irq_mask,
                               uint32_t* eff_rx_us, uint32_t* eff_sleep_us) {
    (void)irq_flags;
    (void)irq_mask;
    (void)eff_rx_us;
    (void)eff_sleep_us;
    return RADIOLIB_ERR_UNSUPPORTED;
  }
  virtual int16_t stopDutyCycleHardware() { return _radio->standby(); }
  virtual bool isPacketReady();
  virtual bool isChipBusy() { return false; }
  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;
  virtual void doResetAGC();

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board)
      : _radio(&radio), _board(&board), _last_rssi(0), _last_snr(0), _last_metrics_valid(false),
        _preamble_sf(0), _rx_ps_enabled(false), _rx_ps_armed(false),
        _rx_ps_rx_us(RX_POWERSAVING_DEFAULT_RX_US),
        _rx_ps_sleep_us(RX_POWERSAVING_DEFAULT_SLEEP_US), _rx_ps_eff_rx_us(0),
        _rx_ps_eff_sleep_us(0), _rx_ps_last_error(RADIOLIB_ERR_NONE),
        n_rx_ps_arm_failures(0), _nf_calib_active(false),
        _nf_last_calib(0), _nf_calib_deadline(0), _nf_sample_from(0) {
    n_recv = n_sent = n_recv_errors = 0;
  }

  void begin() override;
  virtual void powerOff();
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
  bool isChannelActive();
  virtual bool supportsRxPowerSaving() const { return false; }
  bool setRxPowerSaving(bool enabled, uint32_t rx_us, uint32_t sleep_us) override;
  RxPowerSavingStatus getRxPowerSavingStatus() const override;
  bool isRxPowerSavingCalibrationActive() const override { return _nf_calib_active; }

  bool isReceiving() override {
    if (isReceivingPacket()) return true;

    return isChannelActive();
  }

  virtual void setParams(float freq, float bw, uint8_t sf, uint8_t cr) = 0;
  uint32_t getRngSeed();
  void setTxPower(int8_t dbm);

  virtual float getCurrentRSSI() =0;
  virtual uint8_t getSpreadingFactor() const { return LORA_SF; }
  static uint16_t preambleLengthForSF(uint8_t sf) { return sf <= 8 ? 32 : 16; }
  void updatePreamble(uint8_t sf) { _preamble_sf = sf; _radio->setPreambleLength(preambleLengthForSF(sf)); }
  PacketMillis calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols);
  virtual int16_t performChannelScan();

  int getNoiseFloor() const override { return _noise_floor; }
  void triggerNoiseFloorCalibrate(int threshold) override;
  void setCADEnabled(bool enable) override { _cad_enabled = enable; }
  void resetAGC() override;

  void loop() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() {
    n_recv = n_sent = n_recv_errors = n_rx_ps_arm_failures = 0;
    // Clearing diagnostics also grants a fresh set of arm attempts. If they
    // fail again, the normal three-failure continuous-RX fallback applies.
    _rx_ps_arm_retry.reset();
  }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

  float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10

  virtual bool setRxBoostedGainMode(bool) { return false; }
  virtual bool getRxBoostedGainMode() const { return false; }
  
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) { return false; }
};

/**
 * \brief  an RNG impl using the noise from the LoRa radio as entropy.
 *         NOTE: this is VERY SLOW!  Use only for things like creating new LocalIdentity
*/
class RadioNoiseListener : public mesh::RNG {
  PhysicalLayer* _radio;
public:
  RadioNoiseListener(PhysicalLayer& radio): _radio(&radio) { }

  void random(uint8_t* dest, size_t sz) override {
#ifdef USE_CC310_HW_CRYPTO
    nRFCrypto.Random.generate(dest, (uint16_t)sz);
    for (int i = 0; i < sz; i++) {
      dest[i] ^= _radio->randomByte() ^ (::random(0, 256) & 0xFF); // combine with Radio's entropy
    }
#else
    for (int i = 0; i < sz; i++) {
      dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
    }
#endif
  }
};
