# sh ./build-repeaters-iotthinks.sh
export FIRMWARE_VERSION="PowerSaving14"

############# Repeaters #############
# Commonly-used boards
## ESP32 - 6 boards
sh build.sh build-firmware \
Heltec_v3_repeater \
Heltec_WSL3_repeater \
heltec_v4_repeater \
Station_G2_repeater \
T_Beam_S3_Supreme_SX1262_repeater \
Tbeam_SX1262_repeater

## NRF52 - 7 boards
sh build.sh build-firmware \
RAK_4631_repeater \
Heltec_t114_repeater \
Xiao_nrf52_repeater \
Heltec_mesh_solar_repeater \
ProMicro_repeater \
SenseCap_Solar_repeater \
t1000e_repeater

## SX1276 - 3 boards
sh build.sh build-firmware \
Heltec_v2_repeater \
LilyGo_TLora_V2_1_1_6_repeater \
Tbeam_SX1276_repeater

## Ikoka - 3 boards
sh build.sh build-firmware \
ikoka_nano_nrf_22dbm_repeater \
ikoka_nano_nrf_30dbm_repeater \
ikoka_nano_nrf_33dbm_repeater

# Newly-supported boards - 5 boards
sh build.sh build-firmware \
Xiao_S3_WIO_repeater \
Xiao_C3_repeater \
Xiao_C6_repeater_ \
RAK_3401_repeater \
Heltec_E290_repeater_

############# Room Server #############
sh build.sh build-firmware \
Heltec_v3_room_server \
heltec_v4_room_server \
RAK_4631_room_server \
Heltec_t114_room_server \
Xiao_nrf52_room_server

############# Companions #############
sh build.sh build-firmware \
RAK_4631_companion_radio_ble \
Heltec_t114_companion_radio_ble \
Xiao_nrf52_companion_radio_ble

