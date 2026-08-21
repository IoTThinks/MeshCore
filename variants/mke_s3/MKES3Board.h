#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class MKES3Board : public ESP32Board {
public:
  MKES3Board() { }

  const char* getManufacturerName() const override {
    return "MKE S3";
  }
};
