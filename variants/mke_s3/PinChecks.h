#pragma once
#if defined(WITH_RS232_BRIDGE) && ((WITH_RS232_BRIDGE_RX == P_LORA_RESET) || (WITH_RS232_BRIDGE_TX == P_LORA_RESET))
#error "MKE-S3 RS232 pins conflict with LoRa reset; confirm separate RX/TX wiring"
#endif
