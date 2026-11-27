#include <AsyncElegantOTA.h>

#if defined(ESP_PLATFORM) && defined(ADMIN_PASSWORD)
    // FirmwareRecovery.h lives in src/helpers, but this library is compiled from a copied
    // .pio/libdeps/<env> directory that has no project src/ include path. Declare the small A/B
    // API here; FirmwareRecovery.cpp provides the definitions.
    namespace FirmwareRecovery {
        bool isABEnabled();
        bool isSyncing();
        bool isStabilizing();
        void abortOTAPending();
    }
    #if OTA_AB_ENABLED
        #include <esp_ota_ops.h>
        #include <esp_partition.h>
    #endif
#endif

// A/B is a compile-time opt-in. Non-A/B builds keep PS18's upstream Update path untouched and
// must not gain esp_ota_* code or its log strings.
#if defined(ESP_PLATFORM) && defined(ADMIN_PASSWORD) && OTA_AB_ENABLED
    #define ELEGANT_OTA_AB 1
#else
    #define ELEGANT_OTA_AB 0
#endif

// One chunked upload at a time. _ota_attempted lets the POST-complete handler tell a failed
// transfer from an empty request, and _ota_result is the only source of the 200/500 decision.
static bool _ota_active = false;
static bool _ota_attempted = false;
static bool _ota_result = false;

// Request-local markers. The library keys attributes by the name pointer, so share one pointer
// per name.
static const char *const _OTA_SESSION_ATTR = "ota_session";
static const char *const _OTA_REJECTED_ATTR = "ota_rejected";
static const char *const _OTA_REFUSED_ATTR = "ota_refused";
static const char *const _OTA_REASON_ATTR = "ota_reason";

// send() during body parsing only stores the response; the POST handler runs afterwards and
// would otherwise overwrite a 409/503 refusal with its generic 500. Record the refusal reason
// per request so the final response matches what actually happened.
static void refuseUpload(AsyncWebServerRequest *request, int status, const char *reason) {
    request->setAttribute(_OTA_REFUSED_ATTR, (long)status);
    request->setAttribute(_OTA_REASON_ATTR, reason);
    request->send(status, "text/plain", reason);
}

#if ELEGANT_OTA_AB
static esp_ota_handle_t _ota_handle = 0;
static const esp_partition_t *_ota_partition = nullptr;

static void logOtaError(const char *step, esp_err_t err) {
    char buf[96];
    snprintf(buf, sizeof(buf), "[OTA] %s failed: %s (%d)", step, esp_err_to_name(err), (int)err);
    Serial.println(buf);
}

// Select the inactive slot by subtype, never by label: PS18's stock tables call the slots
// app0/app1 and other tables call them ota_0/ota_1. With a single app slot the IDF helper
// returns the running partition, so reject it explicitly; esp_ota_begin() on the running
// partition fails with ESP_ERR_OTA_PARTITION_CONFLICT.
static const esp_partition_t *findABUpdateTarget() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);

    if (!running || !target) return nullptr;
    if (target == running) return nullptr;
    if (target->type != ESP_PARTITION_TYPE_APP) return nullptr;
    if (target->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
        target->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        return nullptr;
    }
    return target;
}

static bool recoveryTaskBusy() {
    if (FirmwareRecovery::isSyncing()) {
        Serial.println("[OTA] Refused: slot sync in progress");
        return true;
    }
    if (FirmwareRecovery::isStabilizing()) {
        Serial.println("[OTA] Refused: post-OTA stabilization in progress");
        return true;
    }
    return false;
}

static void abortABUpload() {
    if (_ota_handle != 0) {
        esp_err_t err = esp_ota_abort(_ota_handle);
        if (err != ESP_OK) {
            logOtaError("esp_ota_abort", err);
        }
        _ota_handle = 0;
    }
    FirmwareRecovery::abortOTAPending();
    _ota_result = false;
}

// A slot sync can be started from the CLI while an upload is in flight; stop writing and never
// restart, so the sync task can finish copying the inactive slot.
static bool abortIfSyncStarted(AsyncWebServerRequest *request) {
    if (!FirmwareRecovery::isSyncing()) return false;
    Serial.println("[OTA] Aborted: slot sync started during upload");
    abortABUpload();
    request->setAttribute(_OTA_SESSION_ATTR, false);
    _ota_active = false;
    _ota_attempted = false;
    refuseUpload(request, 409, "OTA aborted: sync started");
    return true;
}
#endif

AsyncElegantOtaClass AsyncElegantOTA;

void AsyncElegantOtaClass::setID(const char* id){
    _id = id;
}

void AsyncElegantOtaClass::begin(AsyncWebServer *server, const char* username, const char* password){
    _server = server;

    if(strlen(username) > 0){
        _authRequired = true;
        _username = username;
        _password = password;
    }else{
        _authRequired = false;
        _username = "";
        _password = "";
    }

    _server->on("/update/identity", HTTP_GET, [&](AsyncWebServerRequest *request){
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }
        #if defined(ESP8266)
            request->send(200, "application/json", "{\"id\": \""+_id+"\", \"hardware\": \"ESP8266\"}");
        #elif defined(ESP32)
            request->send(200, "application/json", "{\"id\": \""+_id+"\", \"hardware\": \"ESP32\"}");
        #endif
    });

    _server->on("/update", HTTP_GET, [&](AsyncWebServerRequest *request){
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", ELEGANT_HTML, ELEGANT_HTML_SIZE);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    _server->on("/update", HTTP_POST, [&](AsyncWebServerRequest *request) {
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }

        // A concurrent request refused at chunk 0 must not report or restart the owner's upload.
        if (request->getAttribute(_OTA_REJECTED_ATTR, false)) {
            AsyncWebServerResponse *response =
                request->beginResponse(409, "text/plain", "OTA already in progress");
            response->addHeader("Connection", "close");
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);
            return;
        }

        // A request refused before it owned the session (slot sync, busy recovery, MD5/begin
        // rejection) reports that reason, not the generic "upload failed" 500.
        long refused = request->getAttribute(_OTA_REFUSED_ATTR, 0L);
        if (refused != 0) {
            String reason = request->getAttribute(_OTA_REASON_ATTR, String("OTA refused"));
            AsyncWebServerResponse *response =
                request->beginResponse((int)refused, "text/plain", reason);
            response->addHeader("Connection", "close");
            response->addHeader("Access-Control-Allow-Origin", "*");
            request->send(response);
            return;
        }

        bool ok = _ota_attempted && _ota_result;
#if ELEGANT_OTA_AB
        if (!ok && _ota_attempted) {
            // A failed transfer must not leave a pending post-OTA check behind: clear it before
            // the approved A/B restart, so the running slot boots normally.
            FirmwareRecovery::abortOTAPending();
        }
#endif

        // This request is finished; a later disconnect is not an interrupted upload.
        request->setAttribute(_OTA_SESSION_ATTR, false);

        // the request handler is triggered after the upload has finished...
        // create the response, add header, and send response
        const char *body = ok ? "OK" : "FAIL";
        AsyncWebServerResponse *response = request->beginResponse(ok ? 200 : 500, "text/plain", body);
        response->addHeader("Connection", "close");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);

#if ELEGANT_OTA_AB
        if (ok || _ota_attempted) {
            restart();  // A/B failure restarts only after abortOTAPending() above
            return;
        }
#else
        if (ok) {
            // Upstream restarts after every POST. A failed in-place upload leaves the running
            // firmware untouched and must NOT reboot.
            restart();
            return;
        }
#endif
        _ota_active = false;
        _ota_attempted = false;
    }, [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data,
           size_t len, bool final) {
        //Upload handler chunks in data
        if(_authRequired){
            if(!request->authenticate(_username.c_str(), _password.c_str())){
                return request->requestAuthentication();
            }
        }

        // Only the request that owns this OTA session may touch the shared handle / Update
        // object. A refused request keeps delivering body chunks (send() does not stop body
        // parsing) and must be a no-op for every one of them.
        if (index > 0 && (request->getAttribute(_OTA_REJECTED_ATTR, false) ||
                          !request->getAttribute(_OTA_SESSION_ATTR, false))) {
            return;
        }

        if (!index) {
            if (_ota_active) {
                // Another upload owns the session; mark this request so its POST handler does not
                // report or restart on the owner's state.
                request->setAttribute(_OTA_REJECTED_ATTR, true);
                return request->send(409, "text/plain", "OTA already in progress");
            }

            _ota_active = true;
            _ota_attempted = true;
            _ota_result = false;
            request->setAttribute(_OTA_SESSION_ATTR, true);

            // A client that drops mid-upload must not leave an open esp_ota handle or a pending
            // post-OTA flag behind. The POST handler clears this marker before its response.
            request->onDisconnect([request]() {
                if (!request->getAttribute(_OTA_SESSION_ATTR, false)) return;
                request->setAttribute(_OTA_SESSION_ATTR, false);
#if ELEGANT_OTA_AB
                if (_ota_result) {
                    Serial.println("[OTA] Upload complete before disconnect; image kept");
                } else {
                    abortABUpload();
                    Serial.println("[OTA] Upload interrupted: A/B session aborted");
                }
#else
                if (_ota_result) {
                    Serial.println("[OTA] Upload complete before disconnect; image kept");
                } else {
                    // Error-path hardening over upstream PS18: a dropped in-place upload would
                    // otherwise leave Update mid-session until reboot.
                    Update.abort();
                    Serial.println("[OTA] Upload interrupted: in-place session aborted");
                }
#endif
                _ota_active = false;
                _ota_attempted = false;
            });

#if ELEGANT_OTA_AB
            // A running sync or stabilization task owns the inactive slot: never race it.
            if (!FirmwareRecovery::isABEnabled() || recoveryTaskBusy()) {
                request->setAttribute(_OTA_SESSION_ATTR, false);
                FirmwareRecovery::abortOTAPending();
                _ota_active = false;
                _ota_attempted = false;
                return refuseUpload(request, 503, "OTA refused: recovery busy");
            }

            _ota_partition = findABUpdateTarget();
            if (!_ota_partition) {
                Serial.println("[OTA] Refused: no inactive A/B app slot");
                request->setAttribute(_OTA_SESSION_ATTR, false);
                FirmwareRecovery::abortOTAPending();
                _ota_active = false;
                _ota_attempted = false;
                return request->send(500, "text/plain", "OTA refused: no inactive slot");
            }

            esp_err_t err = esp_ota_begin(_ota_partition, UPDATE_SIZE_UNKNOWN, &_ota_handle);
            if (err != ESP_OK) {
                logOtaError("esp_ota_begin", err);
                _ota_handle = 0;
                FirmwareRecovery::abortOTAPending();
                // Keep the session owned until the POST handler decides on the A/B restart.
                return request->send(500, "text/plain", "OTA could not begin");
            }

            char buf[112];
            snprintf(buf, sizeof(buf), "[OTA] A/B slot selected by subtype: slot %d, %lu bytes",
                     (_ota_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? 0 : 1,
                     (unsigned long)_ota_partition->size);
            Serial.println(buf);
#else
            if(!request->hasParam("MD5", true)) {
                request->setAttribute(_OTA_SESSION_ATTR, false);
                _ota_active = false;
                _ota_attempted = false;
                return refuseUpload(request, 400, "MD5 parameter missing");
            }

            if(!Update.setMD5(request->getParam("MD5", true)->value().c_str())) {
                request->setAttribute(_OTA_SESSION_ATTR, false);
                _ota_active = false;
                _ota_attempted = false;
                return refuseUpload(request, 400, "MD5 parameter invalid");
            }

            #if defined(ESP8266)
                int cmd = (filename == "filesystem") ? U_FS : U_FLASH;
                Update.runAsync(true);
                size_t fsSize = ((size_t) &_FS_end - (size_t) &_FS_start);
                uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin((cmd == U_FS)?fsSize:maxSketchSpace, cmd)){ // Start with max available size
            #elif defined(ESP32)
                int cmd = (filename == "filesystem") ? U_SPIFFS : U_FLASH;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) { // Start with max available size
            #endif
                Update.printError(Serial);
                request->setAttribute(_OTA_SESSION_ATTR, false);
                _ota_active = false;
                _ota_attempted = false;
                return refuseUpload(request, 400, "OTA could not begin");
            }
#endif
        }

        // Write chunked data to the free sketch space
        if(len){
#if ELEGANT_OTA_AB
            if (!_ota_active || _ota_handle == 0) {
                return;
            }
            if (abortIfSyncStarted(request)) {
                return;
            }
            esp_err_t err = esp_ota_write(_ota_handle, data, len);
            if (err != ESP_OK) {
                logOtaError("esp_ota_write", err);
                abortABUpload();
                return request->send(500, "text/plain", "OTA write failed");
            }
            if (index > 0 && (index % 65536) == 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "[OTA] Written %lu bytes", (unsigned long)index);
                Serial.println(buf);
            }
#else
            if (Update.write(data, len) != len) {
                _ota_result = false;
                return request->send(400, "text/plain", "OTA could not begin");
            }
#endif
        }

        if (final) { // if the final flag is set then this is the last frame of data
#if ELEGANT_OTA_AB
            if (!_ota_active || _ota_handle == 0) {
                return;
            }
            if (abortIfSyncStarted(request)) {
                return;
            }

            esp_err_t err = esp_ota_end(_ota_handle);
            _ota_handle = 0;  // esp_ota_end() frees the handle in every path, even on failure
            if (err != ESP_OK) {
                logOtaError("esp_ota_end", err);
                _ota_result = false;
                FirmwareRecovery::abortOTAPending();
                return request->send(500, "text/plain", "OTA end failed");
            }

            // otadata is owned by the IDF OTA layer. esp_ota_set_boot_partition() verifies the
            // image, then esp_rewrite_ota_data() -> rewrite_ota_seq() erase one otadata sector
            // and write the new select entry; its CRC is seeded with UINT32_MAX over ota_seq
            // (bootloader_common_ota_select_crc()). Sources, esp-idf v4.4.7:
            //   components/app_update/esp_ota_ops.c:436-477 (verify, then esp_rewrite_ota_data)
            //   components/bootloader_support/src/bootloader_common_loader.c:40-43 (CRC seed)
            // The old hand-written select-entry struct, local CRC helper and direct flash
            // sector writes were deleted: they duplicated this logic and corrupted the otadata
            // entry. Do not reintroduce them; this call is the whole commit.
            err = esp_ota_set_boot_partition(_ota_partition);
            if (err != ESP_OK) {
                logOtaError("esp_ota_set_boot_partition", err);
                _ota_result = false;
                FirmwareRecovery::abortOTAPending();
                return request->send(500, "text/plain", "OTA boot partition failed");
            }

            // esp_ota_set_boot_partition() validated the image and rewrote otadata itself; no
            // manual select-entry, CRC or spi_flash writes belong here.
            _ota_result = true;
            _ota_partition = nullptr;
            Serial.println("[OTA] A/B image verified and selected for next boot");
#else
            if (!Update.end(true)) { //true to set the size to the current progress
                Update.printError(Serial);
                _ota_result = false;
                return request->send(400, "text/plain", "Could not end OTA");
            }
            _ota_result = true;
#endif
        }else{
            return;
        }
    });
}

// deprecated, keeping for backward compatibility
void AsyncElegantOtaClass::loop() {
}

void AsyncElegantOtaClass::restart() {
    yield();
    delay(1000);
    yield();
    ESP.restart();
}

String AsyncElegantOtaClass::getID(){
    String id = "";
    #if defined(ESP8266)
        id = String(ESP.getChipId());
    #elif defined(ESP32)
        id = String((uint32_t)ESP.getEfuseMac(), HEX);
    #endif
    id.toUpperCase();
    return id;
}
