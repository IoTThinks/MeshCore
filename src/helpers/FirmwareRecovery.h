#ifndef OTA_FIRMWARE_RECOVERY_H
#define OTA_FIRMWARE_RECOVERY_H

#include <Arduino.h>

#ifndef BOOT_FAIL_THRESHOLD
  #define BOOT_FAIL_THRESHOLD 3
#endif

// A/B OTA is opt-in per environment: -D OTA_AB_ENABLED=1. 4 MB / C6 / nRF52 builds keep the
// upstream in-place Update path and must not enter the A/B recovery code.
#ifndef OTA_AB_ENABLED
  #define OTA_AB_ENABLED 0
#endif

#if defined(ESP_PLATFORM) && defined(ADMIN_PASSWORD)

#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace FirmwareRecovery {

static const uint32_t RECOVERY_MAGIC = 0x52544F41;  // 'RTOA'
static const uint32_t RECOVERY_STATE_VERSION = 2;

static const int POSTOTA_ROLLBACK_PHASE = 5;

struct RecoveryState {
  uint32_t magic;
  uint32_t version;
  uint32_t boot_count;
  uint32_t last_reset_reason;
  uint32_t ota_monitored;  // 1 while a post-OTA image is under health observation
  uint32_t postota_loop_completed;  // loop ticks after markReady() when the health verdict was reached
  int32_t slot_index;
  int32_t last_sync_slot;  // source slot of the last full copy; -1 until a copy completes
  int32_t debug_phase;  // 0=init, 1=task_created, 2=postpone_done, 3=sync_triggered, 4=sync_done,
                        // 5=postota_rollback
  int32_t debug_error;  // 0=success, 1=running_null, 2=slot_changed, 3=no_other_slot,
                        // 4=size_mismatch, 5=no_ab_partitions, 6=task_create_failed, 7=sync_copy_failed
                        // 8=health_ready_timeout, 9=rollback_image_invalid, 10=set_boot_failed
  uint32_t crc;         // CRC32 over all fields above
};

RecoveryState &State();
const RecoveryState &GetState();

// Compile-time gate: true only when the env builds with -D OTA_AB_ENABLED=1.
bool isABEnabled();
// Runtime capability: true when the flashed partition table has at least two OTA app slots.
bool isABAvailable();

void begin();
void loopTick();
void markOTAPending();
void abortOTAPending();
void markReady();

bool isStabilizing();
bool isSyncing();
// True from startOTAUpdate()/markOTAPending() until the upload aborts or the device reboots.
// A manual slot sync must not race an upload that owns the inactive slot.
bool isOTAUploadInProgress();

// Outcome of an explicit sync request. The copy itself runs in a background task, so SYNC_STARTED
// only means the job was accepted; the console reports completion or failure.
enum SyncResult {
  SYNC_STARTED = 0,
  SYNC_SKIPPED,
  SYNC_REFUSED_DISABLED,
  SYNC_REFUSED_UNAVAILABLE,
  SYNC_REFUSED_OTA,
  SYNC_REFUSED_BUSY,
  SYNC_FAILED,
};

// Copy the running slot to the inactive one. Nothing in this module starts it on its own: the
// previous slot stays untouched until an admin asks for it. Returns how the request ended, so the
// CLI can report skipped vs started vs refused/failed without guessing.
SyncResult startSlotSync();

} // namespace FirmwareRecovery

#endif // ESP_PLATFORM && ADMIN_PASSWORD

#endif // OTA_FIRMWARE_RECOVERY_H
