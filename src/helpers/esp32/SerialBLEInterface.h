#pragma once

#include "../BaseSerialInterface.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class SerialBLEInterface : public BaseSerialInterface, BLESecurityCallbacks, BLEServerCallbacks, BLECharacteristicCallbacks {
  BLEServer *pServer;
  BLEService *pService;
  BLECharacteristic * pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  uint16_t last_conn_id;
  uint32_t _pin_code;
  unsigned long _last_write;
  unsigned long adv_restart_time;

  // BLE power tuning (see setPowerPrefs). Defaults mirror NodePrefs', so the
  // interface behaves the same if setPowerPrefs() is never called.
  uint16_t _adv_ms = 0;        // 0 = stack default
  int8_t   _tx_dbm = 3;
  uint16_t _conn_ms = 0;       // 0 = leave the interval to the phone
  uint8_t  _conn_latency = 0;
  uint16_t _sync_ms = 30;      // interval while the app is talking; 0 = never switch
  uint16_t _quiet_s = 10;      // silence before dropping to the idle interval
  bool     _fast;              // currently on the sync profile
  uint8_t  _burst;             // frames counted towards the next fast switch
  unsigned long _last_activity;
  esp_bd_addr_t _peer_bda;

  void applyConnParams();
  void setFastProfile(bool fast);
  void noteActivity();
  void pollConnParams();

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  4
  StaticQueue_t recv_queue_state;
  uint8_t recv_queue_storage[FRAME_QUEUE_SIZE * sizeof(Frame)];
  QueueHandle_t recv_queue;
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers();

protected:
  // BLESecurityCallbacks methods
  uint32_t onPassKeyRequest() override;
  void onPassKeyNotify(uint32_t pass_key) override;
  bool onConfirmPIN(uint32_t pass_key) override;
  bool onSecurityRequest() override;
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;

  // BLEServerCallbacks methods
  void onConnect(BLEServer* pServer) override;
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override;
  void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override;
  void onDisconnect(BLEServer* pServer) override;

  // BLECharacteristicCallbacks methods
  void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) override;

public:
  SerialBLEInterface() {
    pServer = NULL;
    pService = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    adv_restart_time = 0;
    _isEnabled = false;
    _last_write = 0;
    last_conn_id = 0;
    _fast = false;
    _burst = 0;
    _last_activity = 0;
    memset(_peer_bda, 0, sizeof(_peer_bda));
    recv_queue = xQueueCreateStatic(
      FRAME_QUEUE_SIZE, sizeof(Frame), recv_queue_storage, &recv_queue_state
    );
    send_queue_len = 0;
  }

  /**
   * BLE power tuning, applied by begin()/the next connection. Defaults keep the
   * stack's own settings (20-40 ms advertising, +3 dBm, phone-chosen interval).
   * @param adv_ms        advertising interval, 0 = stack default
   * @param tx_dbm        TX power in dBm
   * @param conn_ms       idle connection interval, 0 = leave it to the phone
   * @param conn_latency  connection events the peripheral may skip while idle
   * @param sync_ms       connection interval while the app is talking, 0 = never
   *                      switch (keep the idle interval all the time)
   * @param quiet_s       seconds of silence before dropping back to idle timing
   */
  void setPowerPrefs(uint16_t adv_ms, int8_t tx_dbm, uint16_t conn_ms, uint8_t conn_latency,
                     uint16_t sync_ms, uint16_t quiet_s) {
    _adv_ms = adv_ms;
    _tx_dbm = tx_dbm;
    _conn_ms = conn_ms;
    _conn_latency = conn_latency;
    _sync_ms = sync_ms;
    _quiet_s = quiet_s;
  }

  // True while the app is talking (the fast profile is up), which the main loop
  // uses to stay on the responsive cadence instead of the long idle tick.
  bool isLinkActive() const { return deviceConnected && _fast; }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  // PowerSaving variant
  bool isReadBusy() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
