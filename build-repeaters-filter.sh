# sh ./build-repeaters-filter.sh
export FIRMWARE_VERSION="PowerSaving16-Filter"

############# Repeaters #############
# Commonly-used boards
## ESP32 - 17 boards
sh build.sh build-firmware \
heltec_v4_repeater \
Heltec_t096_repeater \
RAK_4631_repeater
