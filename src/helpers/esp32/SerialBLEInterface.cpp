#include "SerialBLEInterface.h"
#include "esp_mac.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis

// BLE intervals are in 1.25 ms / 0.625 ms units; the CLI works in millis.
#define BLE_INTERVAL_UNIT_MS    1.25f
#define BLE_ADV_UNIT_MS         0.625f

// Advertised TX power steps are 3 dBm apart on every ESP32 variant; only the
// base differs (S3/C3 start at -24 dBm, the original ESP32 at -12 dBm).
#if CONFIG_IDF_TARGET_ESP32
  #define BLE_TX_MIN_DBM    (-12)
  #define BLE_TX_LEVELS     8
#else
  #define BLE_TX_MIN_DBM    (-24)
  #define BLE_TX_LEVELS     16
#endif

static esp_power_level_t bleTxPowerLevel(int8_t dbm) {
  int level = (dbm - BLE_TX_MIN_DBM) / 3;
  if (level < 0) level = 0;
  if (level >= BLE_TX_LEVELS) level = BLE_TX_LEVELS - 1;
  return (esp_power_level_t) level;
}

static uint16_t bleIntervalUnits(uint16_t ms, float unit_ms) {
  uint16_t units = (uint16_t)(ms / unit_ms + 0.5f);
  return units < 6 ? 6 : units;   // BLE minimum is 7.5 ms
}

// Frames closer together than this belong to the same burst; a burst of
// SYNC_BURST_FRAMES promotes the link back to the fast interval. One poll from
// an idle app never gets there, an app sync (contacts, channels, messages) does.
#define SYNC_BURST_WINDOW_MS   2000
#define SYNC_BURST_FRAMES      3

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", prefix, name);

  // Create the BLE Device
  BLEDevice::init(dev_name);
  BLEDevice::setPower(bleTxPowerLevel(_tx_dbm));
  BLEDevice::setSecurityCallbacks(this);
  BLEDevice::setMTU(MAX_FRAME_SIZE);

  BLESecurity  sec;
  sec.setStaticPIN(pin_code);
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(this);

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  pRxCharacteristic->setCallbacks(this);

  pServer->getAdvertising()->addServiceUUID(SERVICE_UUID);
  if (_adv_ms) {
    uint16_t units = bleIntervalUnits(_adv_ms, BLE_ADV_UNIT_MS);
    pServer->getAdvertising()->setMinInterval(units);
    pServer->getAdvertising()->setMaxInterval(units);
  }
}

// Ask the phone for connection timing: the fast profile while the app is
// talking, the idle one once it has been quiet for `quiet` seconds. It is a
// request (the central may decline), sent once the link is encrypted so it
// cannot be dropped mid-pairing.
void SerialBLEInterface::applyConnParams() {
  uint16_t interval_ms = _fast ? _sync_ms : _conn_ms;
  uint8_t latency = _fast ? 0 : _conn_latency;
  if (!interval_ms) return;

  // Ask for a min/max window rather than min == max: Apple's accessory rules
  // require max >= min + 15 ms, and a request that breaks them can be dropped
  // outright (leaving the phone's own timing in place, i.e. no savings on iOS).
  uint16_t min_interval = bleIntervalUnits(interval_ms, BLE_INTERVAL_UNIT_MS);
  uint16_t max_interval = min_interval + (uint16_t)(15.0f / BLE_INTERVAL_UNIT_MS);

  // Spec: supervision timeout > (1 + latency) * interval * 2; Apple adds
  // < interval * (1 + latency) * 3 and <= 6 s. Size it from the max interval with
  // 25% margin, floored at 4 s so a slow link still has slack.
  uint32_t period_ms = (uint32_t) max_interval * 5 / 4;
  uint32_t timeout = period_ms * 2 * (1 + latency) / 10;   // units of 10 ms
  timeout += timeout / 4;
  if (timeout < 400) timeout = 400;
  if (timeout > 600) timeout = 600;

  pServer->updateConnParams(_peer_bda, min_interval, max_interval, latency, timeout);
}

// Flip between the two profiles and tell the phone about it.
void SerialBLEInterface::setFastProfile(bool fast) {
  _fast = fast;
  _burst = 0;
  applyConnParams();
}

// "The app is talking": only frames *from* the phone count. Pushes towards it
// (raw-RX log, adverts, queued messages) are driven by mesh traffic, and counting
// those would keep flipping a busy link back to the fast profile forever.
void SerialBLEInterface::noteActivity() {
  _last_activity = millis();
  if (_fast || !_conn_ms || !_sync_ms) return;   // switching disabled, or already fast

  if (++_burst < SYNC_BURST_FRAMES) return;

  BLE_DEBUG_PRINTLN("SerialBLEInterface -> app burst, fast conn params");
  setFastProfile(true);
}

void SerialBLEInterface::pollConnParams() {
  if (!deviceConnected || !_conn_ms || !_sync_ms) return;

  unsigned long quiet_ms = millis() - _last_activity;
  if (_fast) {
    if (quiet_ms <= (unsigned long) _quiet_s * 1000UL) return;

    BLE_DEBUG_PRINTLN("SerialBLEInterface -> idle, slow conn params");
    setFastProfile(false);
  } else if (_burst && quiet_ms > SYNC_BURST_WINDOW_MS) {
    _burst = 0;   // frames too far apart to be a sync burst
  }
}

// -------- BLESecurityCallbacks methods

uint32_t SerialBLEInterface::onPassKeyRequest() {
  BLE_DEBUG_PRINTLN("onPassKeyRequest()");
  return _pin_code;
}

void SerialBLEInterface::onPassKeyNotify(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onPassKeyNotify(%u)", pass_key);
}

bool SerialBLEInterface::onConfirmPIN(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onConfirmPIN(%u)", pass_key);
  return true;
}

bool SerialBLEInterface::onSecurityRequest() {
  BLE_DEBUG_PRINTLN("onSecurityRequest()");
  return true;  // allow
}

void SerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
  if (cmpl.success) {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
    deviceConnected = true;
    // Start on the fast profile so the app's first sync is quick; the link
    // drops to the idle profile once it goes quiet (see pollConnParams).
    _last_activity = millis();
    setFastProfile(_conn_ms && _sync_ms);
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");

    //pServer->removePeerDevice(pServer->getConnId(), true);
    pServer->disconnect(pServer->getConnId());
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- BLEServerCallbacks methods

void SerialBLEInterface::onConnect(BLEServer* pServer) {
}

void SerialBLEInterface::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", param->connect.conn_id, pServer->getPeerMTU(param->connect.conn_id));
  last_conn_id = param->connect.conn_id;
  memcpy(_peer_bda, param->connect.remote_bda, sizeof(_peer_bda));
}

void SerialBLEInterface::onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", pServer->getPeerMTU(param->mtu.conn_id));
}

void SerialBLEInterface::onDisconnect(BLEServer* pServer) {
  BLE_DEBUG_PRINTLN("onDisconnect()");
  deviceConnected = false;
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- BLECharacteristicCallbacks methods

void SerialBLEInterface::onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
  uint8_t* rxValue = pCharacteristic->getData();
  int len = pCharacteristic->getLength();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
  } else {
    Frame frame = {};
    frame.len = len;
    memcpy(frame.buf, rxValue, len);
    noteActivity();

    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {
      BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
    }
  }
}

// ---------- public methods

void SerialBLEInterface::clearBuffers() {
  xQueueReset(recv_queue);
  send_queue_len = 0;
}

void SerialBLEInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();

  // Start the service
  pService->start();

  // Start advertising

  //pServer->getAdvertising()->setMinInterval(500);
  //pServer->getAdvertising()->setMaxInterval(1000);

  pServer->getAdvertising()->start();
  adv_restart_time = 0;
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  pServer->getAdvertising()->stop();
  pServer->disconnect(last_conn_id);
  pService->stop();
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   60

// PowerSaving variant
bool SerialBLEInterface::isReadBusy() const {
  return uxQueueMessagesWaiting(recv_queue) > 0;
}

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  pollConnParams();

  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }

  Frame frame;
  if (xQueueReceive(recv_queue, &frame, 0) == pdTRUE) {
    memcpy(dest, frame.buf, frame.len);
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", (uint32_t) frame.len, (uint32_t) dest[0]);
    return frame.len;
  }

  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();

      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");

      //pServer->getAdvertising()->setMinInterval(500);
      //pServer->getAdvertising()->setMaxInterval(1000);

      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> stopping advertising");
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      // connecting
      // do stuff here on connecting
      pServer->getAdvertising()->stop();
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      pServer->getAdvertising()->start();  // re-Start advertising
    }
    adv_restart_time = 0;
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}
