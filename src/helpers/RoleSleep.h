#pragma once
// Include after target.h: these are the role's board, radio and sensor objects.
inline void sleepRoleIfIdle(bool enabled, bool pending, bool command_pending, uint32_t budget_ms) {
  if (!enabled || pending || command_pending || !budget_ms ||
      board.isOTAUpdateActive() || Serial.available()) return;
#ifdef ETHERNET_ENABLED
  return; // the Ethernet CLI/task must remain serviced
#endif
#if defined(ESP32_PLATFORM)
  if (millis() < 120000) return; // retain the serial setup window from v17
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  if (Serial) return; // active USB CDC cannot tolerate explicit ESP32 light sleep
#endif
  auto* location = sensors.getLocationProvider();
  if (location && location->isEnabled()) return; // keep GPS UART reception live
#endif
#ifdef WRAPPER_CLASS
  // Observation only: isReceiving() would start CAD and disturb duty-cycle RX.
  if (radio_driver.hasPendingReceive()) return;
#else
  if (radio_driver.isReceiving()) return;
#endif
  Serial.flush();
  board.sleepMillis(budget_ms);
}
