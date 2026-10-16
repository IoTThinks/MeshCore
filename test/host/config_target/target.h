#pragma once
#include <helpers/radiolib/RXPowerSaving.h>
struct ConfigTestRadio : RxPowerSavingControl {
 bool supported=true, enabled=false, invalid=false;
 uint32_t rx=0, sleep=0;
 bool setRxPowerSaving(bool on,uint32_t r,uint32_t s) override {
  if(on && !supported) return false;
  enabled=on;rx=r;sleep=s;return true;
 }
 void setRxPowerSavingConfigError(bool error) override {invalid=error;}
 RxPowerSavingStatus getRxPowerSavingStatus() const override {
  RxPowerSavingStatus s;s.supported=supported;s.armed=enabled;
  s.last_error=invalid ? RX_POWERSAVING_ERR_TIMING : 0;return s;
 }
 bool setRxBoostedGainMode(bool) {return true;}
 void setTxPower(int8_t) {}
 uint32_t getCADBusyCount() const {return 0;}
};
#define WRAPPER_CLASS ConfigTestRadio
inline ConfigTestRadio radio_driver;

#include <helpers/KeyValueStore.h>
#include <cstring>
struct ConfigTestBoard {
 bool supported = true, lna = false;
 KeyValueStore* prefs = nullptr;
 void attachDynamicPrefs(KeyValueStore* p) {
  prefs=p;char value[8]={};p->getByKey("fem_rxgain",value,sizeof(value));lna=strcmp(value,"1")==0;
 }
 bool handleCommand(const char* cmd,uint32_t,char* reply) {
  if (!supported) return false;
  if (strcmp(cmd,"get radio.fem.rxgain")==0) {strcpy(reply,lna ? "> on" : "> off");return true;}
  if (strncmp(cmd,"set radio.fem.rxgain ",21)==0) {
   lna=strcmp(cmd+21,"on")==0;prefs->setByKey("fem_rxgain",lna ? "1" : "0");strcpy(reply,"OK");return true;
  }
  return false;
 }
};
inline ConfigTestBoard board;
