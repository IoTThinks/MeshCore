// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jarosław Domański, https://github.com/Dom4n
//
// RXPS: parser for the radio.rxps CLI command.
// Written for Kevin Le's (IoTThinks) MeshCore PowerSaving firmware.
// Overview, usage and version history: src/helpers/radiolib/RXPowerSaving.h

#pragma once

#include <stddef.h>

#include "RXPowerSaving.h"

class RXPowerSavingCLI {
public:
  static bool set(const char* value, uint8_t sf, float bw, RxPowerSavingConfig* config,
                  RxPowerSavingControl* control, char* reply, size_t reply_size);
  // sf/bw are needed to turn the stored periods back into symbols, which is
  // what the level scale is expressed in.
  static void get(const RxPowerSavingConfig* config, const RxPowerSavingControl* control,
                  uint8_t sf, float bw, char* reply, size_t reply_size);
};
