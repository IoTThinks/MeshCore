#pragma once

#include <stddef.h>

#include "RXPowerSaving.h"

class RXPowerSavingCLI {
public:
  static bool set(const char* value, uint8_t sf, float bw, RxPowerSavingConfig* config,
                  RxPowerSavingControl* control, char* reply, size_t reply_size);
  static void get(const RxPowerSavingConfig* config, const RxPowerSavingControl* control,
                  char* reply, size_t reply_size);
  static void setRfRxDisabled(const char* value, RxPowerSavingControl* control,
                              char* reply, size_t reply_size);
  static void getRfRxDisabled(const RxPowerSavingControl* control,
                              char* reply, size_t reply_size);
};
