# sh ./build-repeaters-iotthinks.sh
export FIRMWARE_VERSION="PowerSaving14.1"

############# Repeaters #############
# Commonly-used boards
## ESP32 - 7 boards
sh build.sh build-firmware \
Heltec_v3_repeater \
Heltec_WSL3_repeater \
heltec_v4_repeater \
Station_G2_repeater \
T_Beam_S3_Supreme_SX1262_repeater \
Tbeam_SX1262_repeater \
LilyGo_T3S3_sx1262_repeater

## NRF52 - 9 boards
sh build.sh build-firmware \
RAK_4631_repeater \
Heltec_t114_repeater \
Xiao_nrf52_repeater \
Heltec_mesh_solar_repeater \
ProMicro_repeater \
SenseCap_Solar_repeater \
t1000e_repeater \
LilyGo_T-Echo_repeater \
WioTrackerL1_repeater

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
Heltec_E290_repeater

############# Room Server #############
sh build.sh build-firmware \
Heltec_v3_room_server \
heltec_v4_room_server \
RAK_4631_room_server \
Heltec_t114_room_server \
Xiao_nrf52_room_server \
t1000e_room_server \
WioTrackerL1_room_server \
LilyGo_T3S3_sx1262_room_server \
RAK_3401_room_server

############# Companions BLE #############
sh build.sh build-firmware \
RAK_4631_companion_radio_ble \
Heltec_t114_companion_radio_ble \
Xiao_nrf52_companion_radio_ble \
t1000e_companion_radio_ble \
LilyGo_T-Echo_companion_radio_ble \
WioTrackerL1_companion_radio_ble \
RAK_3401_companion_radio_ble

sh build.sh build-firmware \
Heltec_v3_companion_radio_ble \
heltec_v4_companion_radio_ble \
Xiao_S3_WIO_companion_radio_ble \
Heltec_Wireless_Paper_companion_radio_ble

############# Companions USB #############
sh build.sh build-firmware \
Heltec_v3_companion_radio_usb
