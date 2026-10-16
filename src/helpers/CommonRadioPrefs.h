#pragma once
#include "ConfigSerializer.h"
#include "KeyValueStore.h"
#include "radiolib/RXPowerSaving.h"

// Default for the external LoRa FEM receive LNA on boards that have one (fresh
// installs only; 'set radio.fem.rxgain on|off' changes it at runtime). Off saves
// power; a board or env can build with -D RADIO_FEM_RXGAIN=1 to default it on.
#ifndef RADIO_FEM_RXGAIN
  #define RADIO_FEM_RXGAIN 0
#endif

class CommonRadioPrefs : public ConfigSerializer, public KeyValueStore {
  bool _is_dirty = false;
  uint8_t _active_sf = 0;
  float _active_bw = 0;
protected:
  CommonRadioPrefs() { }
public:
  void markDirty() { _is_dirty = true; }
  void clearDirty() { _is_dirty = false; }
  bool isDirty() const { return _is_dirty; }

  virtual float getFreq() const = 0;
  virtual void setFreq(float f) = 0;

  virtual float getBandwidth() const = 0;
  virtual void setBandwidth(float bw) = 0;

  virtual uint8_t getSpreadFactor() const = 0;
  virtual void setSpreadFactor(uint8_t sf) = 0;

  virtual uint8_t getCodingRate() const = 0;
  virtual void setCodingRate(uint8_t cr) = 0;

  virtual float getAirtimeFactor() const = 0;
  virtual void setAirtimeFactor(float af) = 0;

  virtual bool isCadEnabled() const = 0;
  virtual void setCadEnabled(bool en) = 0;

  virtual uint8_t getIntThresh() const = 0;
  virtual void setIntThresh(uint8_t t) = 0;

  virtual uint8_t getRxGain() const = 0;
  virtual void setRxGain(uint8_t g) = 0;

  virtual uint8_t getTxPower() const = 0;
  virtual void setTxPower(uint8_t dbm) = 0;

  virtual float getRxDelay() const = 0;
  virtual void setRxDelay(float d) = 0;

  virtual uint16_t getAgcResetInt() const = 0;
  virtual void setAgcResetInt(uint16_t secs) = 0;

  virtual uint8_t getHashMode() const = 0;
  virtual void setHashMode(uint8_t m) = 0;

  virtual uint8_t getMultiAcks() const = 0;
  virtual void setMultiAcks(uint8_t m) = 0;

  virtual float getFloodTxDelay() const = 0;
  virtual void setFloodTxDelay(float d) = 0;

  virtual float getDirectTxDelay() const = 0;
  virtual void setDirectTxDelay(float d) = 0;

  virtual uint8_t getFEMRxGain() const = 0;
  virtual void setFEMRxGain(uint8_t g) = 0;

  virtual uint8_t getFEMTxGain() const = 0;
  virtual void setFEMTxGain(uint8_t g) = 0;

  virtual RxPowerSavingConfig* getRxPowerSavingConfig() { return nullptr; }
  bool applyRxPowerSaving(uint8_t sf, float bw);
  bool isRxPowerSavingCalibrationActive() const;

  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply);

  bool setByKey(const char* key, const char* value) override;   // for dynamic key/value access
  bool getByKey(const char* key, char* value, size_t max_len) override;
};
