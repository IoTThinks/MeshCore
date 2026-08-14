# sh ./build-repeaters-iotthinks.sh
export FIRMWARE_VERSION="PowerSaving17.1"

############# Repeaters #############
# Commonly-used boards
## ESP32 - 18 boards
sh build.sh build-firmware \
Heltec_ct62_repeater \
Heltec_E290_repeater \
Heltec_v3_repeater \
heltec_v4_repeater \
heltec_v4_r8_repeater \
heltec_tracker_v2_repeater \
Heltec_Wireless_Paper_repeater \
Heltec_Wireless_Tracker_repeater \
Heltec_WSL3_repeater \
LilyGo_T3S3_sx1262_repeater \
LilyGo_TBeam_1W_repeater \
Station_G2_repeater \
T_Beam_S3_Supreme_SX1262_repeater \
Tbeam_SX1262_repeater \
ThinkNode_M5_Repeater \
Xiao_C3_repeater \
Xiao_C6_repeater_ \
Xiao_S3_repeater \
Xiao_S3_WIO_repeater

## NRF52 - 22 boards
sh build.sh build-firmware \
GAT562_30S_Mesh_Kit_repeater \
GAT562_Mesh_Tracker_Pro_repeater \
Heltec_mesh_solar_repeater \
Heltec_t096_repeater \
Heltec_t114_repeater \
Heltec_t1_repeater \
ikoka_nano_nrf_22dbm_repeater \
ikoka_nano_nrf_30dbm_repeater \
ikoka_nano_nrf_33dbm_repeater \
LilyGo_T-Echo_Card_repeater \
LilyGo_T-Echo_repeater \
ProMicro_repeater \
RAK_3401_repeater \
RAK_4631_repeater \
RAK_WisMesh_Tag_repeater \
SenseCap_Solar_repeater \
t1000e_repeater \
ThinkNode_M1_repeater \
ThinkNode_M3_repeater \
ThinkNode_M6_repeater \
WioTrackerL1_repeater \
Xiao_nrf52_repeater

## ESP32, SX1276 - 3 boards
sh build.sh build-firmware \
Heltec_v2_repeater \
LilyGo_TLora_V2_1_1_6_repeater \
Tbeam_SX1276_repeater

############# Room Server #############
# ESP32 - 8 boards
sh build.sh build-firmware \
Heltec_v3_room_server \
heltec_v4_room_server \
heltec_v4_r8_room_server \
heltec_tracker_v2_room_server \
Heltec_Wireless_Paper_room_server \
Heltec_WSL3_room_server \
LilyGo_TBeam_1W_room_server \
Xiao_S3_room_server

# NRF52 - 7 boards
sh build.sh build-firmware \
Heltec_t096_room_server \
Heltec_t114_room_server \
RAK_3401_room_server \
RAK_4631_room_server \
t1000e_room_server \
WioTrackerL1_room_server \
Xiao_nrf52_room_server

############# Companions BLE #############
# NRF52 - 17 boards
sh build.sh build-firmware \
Heltec_t096_companion_radio_ble_femon \
Heltec_t096_companion_radio_ble_femoff \
Heltec_t1_companion_radio_ble \
Heltec_t114_companion_radio_ble \
LilyGo_T-Echo_Card_companion_radio_ble \
LilyGo_T-Echo_companion_radio_ble \
ProMicro_companion_radio_ble \
RAK_3401_companion_radio_ble \
RAK_4631_companion_radio_ble \
RAK_WisMesh_Tag_companion_radio_ble \
SenseCap_Solar_companion_radio_ble \
t1000e_companion_radio_ble \
ThinkNode_M1_companion_radio_ble \
ThinkNode_M3_companion_radio_ble \
ThinkNode_M6_companion_radio_ble \
WioTrackerL1_companion_radio_ble \
Xiao_nrf52_companion_radio_ble

############# Companions BLE PS #############
# ESP32 - 21 boards
sh build.sh build-firmware \
Heltec_ct62_companion_radio_ble \
heltec_tracker_v2_companion_radio_ble \
Heltec_v2_companion_radio_ble \
Heltec_v3_companion_radio_ble \
heltec_v4_3_companion_radio_ble_femoff \
heltec_v4_companion_radio_ble_femon \
heltec_v4_expansionkit_tft_companion_radio_ble \
heltec_v4_r8_companion_radio_ble \
Heltec_Wireless_Paper_companion_radio_ble \
Heltec_Wireless_Tracker_companion_radio_ble \
Heltec_WSL3_companion_radio_ble \
LilyGo_T3S3_sx1262_companion_radio_ble \
LilyGo_TBeam_1W_companion_radio_ble \
LilyGo_TLora_V2_1_1_6_companion_radio_ble \
T_Beam_S3_Supreme_SX1262_companion_radio_ble \
Tbeam_SX1262_companion_radio_ble \
Tbeam_SX1276_companion_radio_ble \
ThinkNode_M5_companion_radio_ble \
Xiao_C3_companion_radio_ble \
Xiao_S3_companion_radio_ble \
Xiao_S3_WIO_companion_radio_ble

############# Companions USB #############
sh build.sh build-firmware \
Heltec_t096_companion_radio_usb \
heltec_v4_companion_radio_usb \
heltec_v4_companion_radio_usb_femoff \
heltec_v4_companion_radio_usb_femon

############# Sample builds #############
# 24 boards
sh build.sh build-firmware \
Heltec_t096_companion_radio_ble_femon \
Heltec_t096_companion_radio_ble_femoff \
Heltec_t096_companion_radio_usb \
Heltec_t096_repeater \
Heltec_t114_companion_radio_ble \
Heltec_t114_repeater \
Heltec_v3_companion_radio_ble \
Heltec_v3_repeater \
heltec_v4_3_companion_radio_ble_femoff \
heltec_v4_repeater \
RAK_3401_companion_radio_ble \
RAK_3401_repeater \
RAK_4631_companion_radio_ble \
RAK_4631_repeater \
SenseCap_Solar_companion_radio_ble \
SenseCap_Solar_repeater \
WioTrackerL1_companion_radio_ble \
WioTrackerL1_repeater \
Xiao_C3_companion_radio_ble \
Xiao_C3_repeater \
Xiao_C6_companion_radio_ble_ \
Xiao_C6_repeater_ \
Xiao_nrf52_companion_radio_ble \
Xiao_nrf52_repeater

# 4 boards
sh build.sh build-firmware \
heltec_v4_companion_radio_ble_femon \
heltec_v4_companion_radio_usb \
heltec_v4_companion_radio_usb_femoff \
heltec_v4_companion_radio_usb_femon

# Mini sample builds
sh build.sh build-firmware \
Heltec_t096_companion_radio_ble_femon \
Heltec_t096_companion_radio_usb \
Heltec_t096_repeater \
heltec_v4_3_companion_radio_ble_femoff \
heltec_v4_repeater \
RAK_3401_companion_radio_ble \
RAK_3401_repeater \
RAK_4631_companion_radio_ble \
RAK_4631_repeater \
Xiao_C3_companion_radio_ble \
Xiao_C3_repeater \
Xiao_C6_companion_radio_ble_ \
Xiao_C6_repeater_ \
Xiao_nrf52_companion_radio_ble \
Xiao_nrf52_repeater