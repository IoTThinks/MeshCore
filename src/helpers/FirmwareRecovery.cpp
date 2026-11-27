#include "FirmwareRecovery.h"

#if defined(ESP_PLATFORM) && defined(ADMIN_PASSWORD)

#include <esp_app_format.h>
#include <esp_attr.h>
// Not pulled in by esp_ota_ops.h on ESP-IDF 5.x (pioarduino / ESP32-C6); needed for esp_image_verify().
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace {

using FirmwareRecovery::RecoveryState;

// A post-OTA image is trusted only after this many mesh loop iterations following markReady().
static const uint32_t OTA_HEALTH_MIN_TICKS = 100;
// ... and after this much wall-clock time since the first completed mesh loop.
static const uint32_t OTA_HEALTH_WINDOW_MS = 90000;

// Loop counter owned by this module (was a repeater-app global with a latent link failure).
static volatile uint32_t s_loop_completed = 0;

static bool s_ab_available = false;
static bool s_stabilizing = false;
static volatile bool s_ready = false;                // set by markReady() after the baseline is published
static volatile uint32_t s_ready_at = 0;             // millis() of the first completed mesh loop
static volatile uint32_t s_ready_tick_baseline = 0;  // s_loop_completed at that moment

void logLine(const char *format, ...) {
  char buf[160];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  Serial.println(buf);
}

uint32_t stateCrc(const RecoveryState &state) {
  return esp_rom_crc32_le(UINT32_MAX, (const uint8_t *)&state, (uint32_t)offsetof(RecoveryState, crc));
}

bool stateIsValid(const RecoveryState &state) {
  return state.magic == FirmwareRecovery::RECOVERY_MAGIC &&
         state.version == FirmwareRecovery::RECOVERY_STATE_VERSION && state.crc == stateCrc(state);
}

void saveState() {
  RecoveryState &state = FirmwareRecovery::State();
  state.crc = stateCrc(state);
}

void resetState(uint32_t reset_reason) {
  RecoveryState &state = FirmwareRecovery::State();
  memset(&state, 0, sizeof(state));
  state.magic = FirmwareRecovery::RECOVERY_MAGIC;
  state.version = FirmwareRecovery::RECOVERY_STATE_VERSION;
  state.slot_index = -1;
  state.last_sync_slot = -1;
  state.last_reset_reason = reset_reason;
  saveState();
}

void setDebugPhase(int phase) {
  RecoveryState &state = FirmwareRecovery::State();
  state.debug_phase = phase;
  saveState();
}

void setDebugError(int err) {
  RecoveryState &state = FirmwareRecovery::State();
  state.debug_error = err;
  saveState();
}

const char *resetReasonName(uint32_t reason) {
  switch (reason) {
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_POWERON: return "POWERON";
    default: return "OTHER";
  }
}

// Slot index comes from the partition subtype, never from the label: PS18 stock tables use
// app0/app1 while other tables use ota_0/ota_1.
int getSlotIndex(const esp_partition_t *partition) {
  if (!partition) return -1;
  switch (partition->subtype) {
    case ESP_PARTITION_SUBTYPE_APP_OTA_0: return 0;
    case ESP_PARTITION_SUBTYPE_APP_OTA_1: return 1;
    default: return -1;
  }
}

const esp_partition_t *findSlot(int slot) {
  esp_partition_subtype_t subtype;
  if (slot == 0) {
    subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
  } else if (slot == 1) {
    subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
  } else {
    return nullptr;
  }

  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, subtype, nullptr);
  if (!it) return nullptr;

  const esp_partition_t *partition = esp_partition_get(it);
  esp_partition_iterator_release(it);
  return partition;
}

const esp_partition_t *findOtherSlot(int current_slot) {
  if (current_slot != 0 && current_slot != 1) return nullptr;
  const esp_partition_t *partition = findSlot(current_slot == 0 ? 1 : 0);
  if (!partition) {
    setDebugError(3);
  }
  return partition;
}

// Compare the application descriptors the linker embeds at the start of each image. The descriptor
// carries the project name, version and ELF SHA-256, so two different builds never compare equal.
// Raw flash windows cannot be used: offset 0xC0 is inside the image, not a version field.
bool versionsMatch(const esp_partition_t *part_a, const esp_partition_t *part_b) {
  esp_app_desc_t desc_a;
  esp_app_desc_t desc_b;

  if (esp_ota_get_partition_description(part_a, &desc_a) != ESP_OK) return false;
  if (esp_ota_get_partition_description(part_b, &desc_b) != ESP_OK) return false;

  bool match = memcmp(desc_a.project_name, desc_b.project_name, sizeof(desc_a.project_name)) == 0 &&
               memcmp(desc_a.version, desc_b.version, sizeof(desc_a.version)) == 0 &&
               memcmp(desc_a.app_elf_sha256, desc_b.app_elf_sha256, sizeof(desc_a.app_elf_sha256)) == 0;
  if (match) {
    logLine("[FW-RECOVERY] Version match");
  } else {
    logLine("[FW-RECOVERY] Version mismatch");
  }
  return match;
}

// A descriptor match alone cannot prove the slot is usable: a copy interrupted after its first
// chunk leaves a valid descriptor over a truncated image. Verify the whole image before skipping.
bool imageIsComplete(const esp_partition_t *partition) {
  esp_partition_pos_t pos = { partition->address, partition->size };
  esp_image_metadata_t metadata = {};
  metadata.start_addr = pos.offset;
  return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &metadata) == ESP_OK;
}

// Select `target` as the next boot partition only when its image verifies. On any failure the
// running slot keeps booting, so the caller must neither clear pending state nor restart.
bool switchBootPartition(const esp_partition_t *target, int from_slot, int to_slot) {
  if (!imageIsComplete(target)) {
    logLine("[FW-RECOVERY] Rollback refused: slot %d image incomplete or invalid", to_slot);
    setDebugError(9);
    return false;
  }

  esp_err_t err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    logLine("[FW-RECOVERY] ERROR: set boot partition slot %d failed: %d (%s)", to_slot, (int)err,
            esp_err_to_name(err));
    setDebugError(10);
    return false;
  }

  logLine("[FW-RECOVERY] Boot partition selected: slot %d -> slot %d", from_slot, to_slot);
  return true;
}

struct SyncTaskArgs {
  const esp_partition_t *src;
  const esp_partition_t *dst;
  int slot_idx;
};

static uint8_t s_sync_buf[16384];
static SyncTaskArgs s_sync_args;
static volatile int s_sync_active = 0;

void slotSyncTask(void *args) {
  SyncTaskArgs *sync_args = (SyncTaskArgs *)args;
  const esp_partition_t *src_part = sync_args->src;
  const esp_partition_t *dst_part = sync_args->dst;

  uint32_t offset = 0;
  uint32_t total_chunks = 0;
  bool ok = true;

  logLine("[FW-RECOVERY] Start syncing: %s -> %s", src_part->label, dst_part->label);

  size_t slot_size = dst_part->size;
  logLine("[FW-RECOVERY] Erasing %lu bytes on %s...", (unsigned long)slot_size, dst_part->label);
  esp_err_t err = esp_partition_erase_range(dst_part, 0, slot_size);
  if (err != ESP_OK) {
    logLine("[FW-RECOVERY] ERROR: erase failed on %s: %d", dst_part->label, err);
    ok = false;
  }

  // 16 KB chunks with a 1 s pause every 8 chunks for radio coexistence.
  const size_t buf_size = sizeof(s_sync_buf);
  while (ok && offset < slot_size) {
    uint32_t chunk_size = (offset + buf_size <= slot_size) ? buf_size : (slot_size - offset);

    if (esp_partition_read(src_part, offset, s_sync_buf, chunk_size) != ESP_OK) {
      logLine("[FW-RECOVERY] ERROR: read failed at 0x%08lx", (unsigned long)offset);
      ok = false;
      break;
    }
    if (esp_partition_write(dst_part, offset, s_sync_buf, chunk_size) != ESP_OK) {
      logLine("[FW-RECOVERY] ERROR: write failed at 0x%08lx", (unsigned long)offset);
      ok = false;
      break;
    }

    offset += chunk_size;
    total_chunks++;

    if (total_chunks % 8 == 0) {
      logLine("[FW-RECOVERY] Sync progress: %lu / %lu (%lu%%)", (unsigned long)offset,
              (unsigned long)slot_size, (unsigned long)(offset * 100 / slot_size));
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  // The success marker is written only after every chunk is on flash. A failed or interrupted
  // copy leaves it cleared, so a stale marker can never report a sync that did not complete.
  RecoveryState &state = FirmwareRecovery::State();
  if (ok) {
    state.last_sync_slot = sync_args->slot_idx;
    state.debug_phase = 4;  // sync done
    state.debug_error = 0;
    logLine("[FW-RECOVERY] Sync complete (copied): %lu chunks, %lu bytes", (unsigned long)total_chunks,
            (unsigned long)offset);
  } else {
    state.last_sync_slot = -1;
    state.debug_error = 7;  // sync copy failed
    logLine("[FW-RECOVERY] Sync failed: %lu chunks, %lu bytes", (unsigned long)total_chunks,
            (unsigned long)offset);
  }
  saveState();

  s_sync_active = 0;
  vTaskDelete(nullptr);
}

void stabilizationTask(void *args) {
  (void)args;
  RecoveryState &state = FirmwareRecovery::State();

  // Slot that was running when the task was created.
  const esp_partition_t *running = esp_ota_get_running_partition();
  int start_slot = getSlotIndex(running);
  if (start_slot < 0) {
    setDebugError(1);
    s_stabilizing = false;
    vTaskDelete(nullptr);
    return;
  }

  logLine("[FW-RECOVERY] POST-OTA health gate: %lu ticks, %lu ms after ready",
          (unsigned long)OTA_HEALTH_MIN_TICKS, (unsigned long)OTA_HEALTH_WINDOW_MS);

  // Fail-before-ready is a rollback: an image that never completed a single mesh loop never became
  // usable. Once a loop completed the image is usable, so a short tick count only means "keep
  // waiting" - passing requires both minimums, never either one alone. s_stabilizing stays true
  // until the verdict, so ESP32Board::sleep() keeps light sleep off for the whole window.
  const uint32_t task_started = millis();
  uint32_t last_log = task_started;
  uint32_t ticks = 0;
  uint32_t elapsed = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!s_ready) {
      if (millis() - task_started >= OTA_HEALTH_WINDOW_MS) {
        logLine("[FW-RECOVERY] *** POST-OTA: no mesh loop within %lu ms - rollback ***",
                (unsigned long)OTA_HEALTH_WINDOW_MS);
        setDebugError(8);
        setDebugPhase(FirmwareRecovery::POSTOTA_ROLLBACK_PHASE);

        const esp_partition_t *dst_slot = findOtherSlot(start_slot);
        if (dst_slot && switchBootPartition(dst_slot, start_slot, getSlotIndex(dst_slot))) {
          logLine("[FW-RECOVERY] Rolling back: slot %d -> slot %d", start_slot, getSlotIndex(dst_slot));
          state.ota_monitored = 0;
          state.postota_loop_completed = 0;
          state.last_sync_slot = start_slot;
          saveState();
          esp_restart();
        } else if (!dst_slot) {
          // No inactive slot means no rollback target to protect: clear pending state as before.
          logLine("[FW-RECOVERY] ERROR: no other slot for rollback after health failure");
          state.ota_monitored = 0;
          state.postota_loop_completed = 0;
          saveState();
        } else {
          // The switch was refused: this slot keeps running and is NOT restarted. ota_monitored
          // stays set so boot.sync cannot overwrite the rollback target, and the next boot
          // retries the health gate instead of silently accepting an unverified image.
          logLine("[FW-RECOVERY] Rollback aborted: still running slot %d (ota_monitored kept)",
                  start_slot);
        }
        s_stabilizing = false;
        vTaskDelete(nullptr);
        return;
      }
      continue;
    }

    // The tick baseline is captured by markReady() and the global counter is never reset, so the
    // delta always describes loops completed after the first mesh loop. Both minimums are required.
    ticks = s_loop_completed - s_ready_tick_baseline;
    elapsed = millis() - s_ready_at;
    if (ticks >= OTA_HEALTH_MIN_TICKS && elapsed >= OTA_HEALTH_WINDOW_MS) {
      break;
    }

    if (millis() - last_log >= 15000) {
      last_log = millis();
      logLine("[FW-RECOVERY] POST-OTA health: %lu/%lu ticks, %lu/%lu ms", (unsigned long)ticks,
              (unsigned long)OTA_HEALTH_MIN_TICKS, (unsigned long)elapsed,
              (unsigned long)OTA_HEALTH_WINDOW_MS);
    }
  }

  // Both minimums were met. Confirm the slot did not switch underneath the check before trusting
  // the image; ota_monitored stays set until this verdict so boot.sync cannot erase the target.
  setDebugPhase(2);
  running = esp_ota_get_running_partition();
  bool healthy = true;
  if (!running) {
    setDebugError(1);
    healthy = false;
  } else if (getSlotIndex(running) != start_slot) {
    setDebugError(2);
    healthy = false;
  }

  state.ota_monitored = 0;
  state.postota_loop_completed = healthy ? ticks : 0;
  saveState();

  if (healthy) {
    logLine("[FW-RECOVERY] POST-OTA health check passed: %lu ticks, %lu ms after ready",
            (unsigned long)ticks, (unsigned long)elapsed);
    logLine("[FW-RECOVERY] POST-OTA health check passed - previous slot preserved");
  }

  // Deliberately no automatic slot sync here: overwriting the inactive slot would destroy the
  // rollback target. startSlotSync() is the only path that copies it.
  s_stabilizing = false;
  vTaskDelete(nullptr);
}

void startStabilizationTask() {
  if (s_stabilizing) return;
  s_stabilizing = true;
  BaseType_t ret =
      xTaskCreatePinnedToCore(stabilizationTask, "ota_stabilize", 16384, nullptr, 5, nullptr, 1);
  if (ret != pdPASS) {
    s_stabilizing = false;
    setDebugError(6);
    logLine("[FW-RECOVERY] ERROR: cannot create stabilization task");
  }
}

} // anonymous namespace

namespace FirmwareRecovery {

bool isABEnabled() {
#if defined(OTA_AB_ENABLED) && OTA_AB_ENABLED
  return true;
#else
  return false;
#endif
}

bool isABAvailable() {
  return esp_ota_get_app_partition_count() >= 2;
}

RecoveryState &State() {
  static RTC_NOINIT_ATTR RecoveryState s_state;
  return s_state;
}

const RecoveryState &GetState() {
  return State();
}

void begin() {
  RecoveryState &state = State();
  uint32_t reset_reason = (uint32_t)esp_reset_reason();

  // RTC_NOINIT memory survives resets; magic/version/CRC decide whether it still means anything.
  if (!stateIsValid(state)) {
    resetState(reset_reason);
    logLine("[FW-RECOVERY] State reset: magic/version/CRC mismatch");
  }

  s_ab_available = isABAvailable();

  if (!isABEnabled()) {
    // Clear stale pending/failure counters so a later A/B build cannot resume an old attempt.
    resetState(reset_reason);
    logLine("[FW-RECOVERY] Disabled: OTA_AB_ENABLED not set (reset 0x%02x)", (unsigned)reset_reason);
    return;
  }

  if (!s_ab_available) {
    resetState(reset_reason);
    logLine("[FW-RECOVERY] Disabled: %u OTA app partition(s), need 2",
            (unsigned)esp_ota_get_app_partition_count());
    return;
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  int running_slot = getSlotIndex(running);
  if (running_slot >= 0) {
    state.slot_index = running_slot;
  }
  saveState();
  logLine("[FW-RECOVERY] A/B mode: slot %d (label %s)", running_slot, running ? running->label : "null");

  // A brownout is a power-rail event, not a firmware crash: it must not consume one of the three
  // strikes or roll back a working image. ESP_RST_SW is a deliberate software restart and is not a
  // failure signal either; only panic and watchdog resets count.
  bool was_failure = (reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_TASK_WDT ||
                      reset_reason == ESP_RST_INT_WDT);
  if (reset_reason == ESP_RST_BROWNOUT) {
    logLine("[FW-RECOVERY] Brownout: not a firmware failure (no strike)");
  }

  if (was_failure && state.boot_count < BOOT_FAIL_THRESHOLD) {
    state.boot_count++;
    logLine("[FW-RECOVERY] Boot failure #%lu (reason %s 0x%02x)", (unsigned long)state.boot_count,
            resetReasonName(reset_reason), (unsigned)reset_reason);

    if (state.boot_count >= BOOT_FAIL_THRESHOLD) {
      logLine("[FW-RECOVERY] *** THRESHOLD REACHED (%d), rolling back ***", BOOT_FAIL_THRESHOLD);

      const esp_partition_t *other_slot = findOtherSlot(running_slot);
      if (other_slot) {
        int other_index = getSlotIndex(other_slot);
        logLine("[FW-RECOVERY] Switching from slot %d to slot %d", running_slot, other_index);
        if (switchBootPartition(other_slot, running_slot, other_index)) {
          state.boot_count = 0;
          state.slot_index = other_index;
          state.last_sync_slot = -1;
          state.ota_monitored = 0;
          saveState();
          esp_restart();
        }
        // A refused switch leaves this slot running. boot_count stays at the threshold so an
        // impossible rollback is not retried on every boot, and ota_monitored keeps its value:
        // a pending OTA target must not be handed to boot.sync after the switch failed.
        logLine("[FW-RECOVERY] Rollback refused: still running slot %d, strikes kept (%lu)",
                running_slot, (unsigned long)state.boot_count);
      } else {
        logLine("[FW-RECOVERY] ERROR: no other slot for rollback");
      }
    }
    saveState();
  } else if (!was_failure) {
    if (state.boot_count > 0) {
      logLine("[FW-RECOVERY] Recovery cleared: successful boot (was %lu failures)",
              (unsigned long)state.boot_count);
    }
    state.boot_count = 0;
    if (running_slot >= 0) {
      state.slot_index = running_slot;
    }
    saveState();

    // Only a boot that follows markOTAPending() runs the post-OTA health check. A normal boot
    // never creates the task, never touches the inactive slot and never delays.
    if (state.ota_monitored == 1) {
      startStabilizationTask();
    }
  }

  state.last_reset_reason = reset_reason;
  saveState();
}

void loopTick() {
  s_loop_completed++;
}

void markOTAPending() {
  if (!isABEnabled() || !isABAvailable()) {
    logLine("[FW-RECOVERY] OTA pending ignored: A/B not active");
    return;
  }

  RecoveryState &state = State();
  if (!stateIsValid(state)) {
    resetState((uint32_t)esp_reset_reason());
  }
  state.magic = RECOVERY_MAGIC;
  state.version = RECOVERY_STATE_VERSION;
  state.boot_count = 0;
  state.last_reset_reason = 0;
  state.slot_index = -1;
  state.last_sync_slot = -1;
  state.debug_phase = 0;
  state.debug_error = 0;
  state.ota_monitored = 1;
  state.postota_loop_completed = 0;
  saveState();
  logLine("[FW-RECOVERY] OTA pending marked");
}

void abortOTAPending() {
  RecoveryState &state = State();
  if (!stateIsValid(state)) {
    resetState((uint32_t)esp_reset_reason());
  }
  state.ota_monitored = 0;
  state.boot_count = 0;
  state.postota_loop_completed = 0;
  state.last_sync_slot = -1;
  state.debug_phase = 0;
  state.debug_error = 0;
  saveState();
  logLine("[FW-RECOVERY] OTA pending aborted");
}

void markReady() {
  if (s_ready) return;

  // Publish the baseline before the flag. This deliberately does not clear state.ota_monitored:
  // the stabilization task owns that flag until it reaches a verdict.
  s_ready_at = millis();
  s_ready_tick_baseline = s_loop_completed;
  s_ready = true;
  logLine("[FW-RECOVERY] Mesh loop ready at %lu ms (%lu ticks)", (unsigned long)s_ready_at,
          (unsigned long)s_ready_tick_baseline);
}

bool isStabilizing() {
  // True from task creation until the post-OTA verdict (or an early setup failure).
  // ESP32Board::sleep() reads this, so light sleep stays off for the whole health window.
  return s_stabilizing;
}

bool isSyncing() {
  return s_sync_active != 0;
}

bool isOTAUploadInProgress() {
  const RecoveryState &state = State();
  return stateIsValid(state) && state.ota_monitored == 1;
}

SyncResult startSlotSync() {
  if (!isABEnabled()) {
    logLine("[FW-RECOVERY] Sync refused: OTA_AB_ENABLED not set");
    return SYNC_REFUSED_DISABLED;
  }
  if (!isABAvailable()) {
    logLine("[FW-RECOVERY] Sync refused: need 2 OTA app partitions");
    return SYNC_REFUSED_UNAVAILABLE;
  }
  if (s_sync_active || s_stabilizing) {
    logLine("[FW-RECOVERY] Sync refused: recovery task already running");
    return SYNC_REFUSED_BUSY;
  }
  if (isOTAUploadInProgress()) {
    logLine("[FW-RECOVERY] Sync refused: OTA upload in progress");
    return SYNC_REFUSED_OTA;
  }

  RecoveryState &state = State();
  if (!stateIsValid(state)) {
    resetState((uint32_t)esp_reset_reason());
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  int running_slot = getSlotIndex(running);
  if (!running || running_slot < 0) {
    setDebugError(1);
    logLine("[FW-RECOVERY] Sync refused: cannot resolve the running slot");
    return SYNC_FAILED;
  }

  const esp_partition_t *other_slot = findOtherSlot(running_slot);
  if (!other_slot) {
    logLine("[FW-RECOVERY] Sync refused: inactive slot not found");
    return SYNC_FAILED;
  }
  if (other_slot->size != running->size) {
    setDebugError(4);
    logLine("[FW-RECOVERY] Sync refused: slot size mismatch");
    return SYNC_FAILED;
  }

  if (versionsMatch(running, other_slot)) {
    // Only skip when the whole inactive image verifies; a partial copy can still carry a
    // matching descriptor from its first chunk.
    if (imageIsComplete(other_slot)) {
      logLine("[FW-RECOVERY] Sync skipped: slots already match");
      return SYNC_SKIPPED;
    }
    logLine("[FW-RECOVERY] Inactive image incomplete - copy needed");
  }

  // The copy is about to touch the inactive slot: clear any earlier success marker first, so an
  // interrupted or failed copy can never be mistaken for a completed sync. It is set again only
  // after the task has copied every byte.
  state.last_sync_slot = -1;
  saveState();

  s_sync_active = 1;
  s_sync_args.src = running;
  s_sync_args.dst = other_slot;
  s_sync_args.slot_idx = running_slot;

  BaseType_t ret =
      xTaskCreatePinnedToCore(slotSyncTask, "slot_sync_job", 6144, &s_sync_args, 5, nullptr, 1);
  if (ret != pdPASS) {
    s_sync_active = 0;
    setDebugError(6);
    logLine("[FW-RECOVERY] ERROR: cannot create slot sync task");
    return SYNC_FAILED;
  }

  setDebugPhase(3);
  logLine("[FW-RECOVERY] Sync requested: slot %d -> slot %d", running_slot, getSlotIndex(other_slot));
  return SYNC_STARTED;
}

} // namespace FirmwareRecovery

#endif // ESP_PLATFORM && ADMIN_PASSWORD
