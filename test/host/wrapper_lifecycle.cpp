#include <cassert>
#include <cstdio>
#include "helpers/radiolib/RadioLibWrappers.h"
struct TestBoard : mesh::MainBoard {
 uint16_t getBattMilliVolts() override {return 3700;}
 const char* getManufacturerName() const override {return "test";}
 void reboot() override {}
 uint8_t getStartupReason() const override {return BD_STARTUP_NORMAL;}
};
struct TestPhysical : PhysicalLayer {
 void (*irq)() = nullptr;
 int continuous=0, standbys=0, resets=0, power_calls=0;
 int16_t continuous_result=0;
 int16_t setOutputPower(int8_t) override {++power_calls;return 0;}
 int16_t scan_result=RADIOLIB_CHANNEL_FREE;
 Module* getMod() override {return nullptr;}
 void setPacketReceivedAction(void (*func)()) override {irq=func;}
 int16_t setPreambleLength(size_t) override {return 0;}
 int16_t startReceive() override {++continuous;return continuous_result;}
 int16_t standby() override {++standbys;return 0;}
 int16_t sleep() override {++resets;return 0;}
 int16_t startTransmit(const uint8_t*,size_t,uint8_t) override {return 0;}
 int16_t finishTransmit() override {return 0;}
 int16_t scanChannel() override {if(irq)irq();return scan_result;}
 size_t getPacketLength(bool) override {return 3;}
 int16_t readData(uint8_t* data,size_t len) override {memset(data,0x42,len);return 0;}
 float getRSSI() override {return -110;}
 float getSNR() override {return 4;}
};
struct TestWrapper : RadioLibWrapper {
 bool receiving=false;
 bool inject_frame=false;
 int arms=0, stops=0;
 int16_t arm_result=0;
 TestWrapper(TestPhysical& p,TestBoard& b):RadioLibWrapper(p,b) {}
 bool supportsRxPowerSaving() const override {return true;}
 bool isReceivingPacket() override {return receiving;}
 bool isPacketReady() override {return true;}
 float getCurrentRSSI() override {if(inject_frame){receiving=true;inject_frame=false;}return -110;}
 void setParams(float,float,uint8_t,uint8_t) override {prepareForRadioConfig();}
 int16_t armDutyCycle(RadioLibIrqFlags_t,RadioLibIrqFlags_t,uint32_t* rx,uint32_t* sl) override {
  ++arms;*rx=_rx_ps_rx_us;*sl=_rx_ps_sleep_us;return arm_result;
 }
 int16_t stopDutyCycleHardware() override {++stops;return 0;}
 void restart() {requestRestartRecv();}
 void pump() {uint8_t b[256];recvRaw(b,sizeof(b));}
};
int main() {
#ifdef USE_LR2021
 TestBoard board;TestPhysical phy;TestWrapper w(phy,board);w.begin();w.pump();
 int before=phy.continuous;int standby_before=phy.standbys;
 w.setTxPower(14);assert(phy.power_calls==1);assert(phy.standbys==standby_before+1);
 w.pump();assert(phy.continuous>before);
 puts("PASS LR2021 setTxPower enters standby and re-arms continuous RX");
 phy.continuous_result=-707;w.restart();w.pump();assert(w.getRxPowerSavingStatus().last_error==-707);
 phy.continuous_result=0;w.restart();w.pump();assert(w.getRxPowerSavingStatus().last_error==0);
 puts("PASS LR2021 continuous RX error and recovery");
 return 0;
#else
 TestBoard board;TestPhysical phy;TestWrapper w(phy,board);w.begin();
 assert(w.setRxPowerSaving(true,30000,20000));w.pump();assert(w.getRxPowerSavingStatus().armed);
 uint8_t pkt[3]={};assert(w.startSendRaw(pkt,3));assert(!w.getRxPowerSavingStatus().armed);
 phy.irq();assert(w.isSendComplete());w.onSendFinished();w.pump();assert(w.getRxPowerSavingStatus().armed);
 puts("PASS TX -> RXPS");
 w.setCADEnabled(true);phy.scan_result=RADIOLIB_LORA_DETECTED;assert(w.isChannelActive());assert(w.getRxPowerSavingStatus().armed);
 assert(w.getCADBusyCount()==1);
 phy.scan_result=RADIOLIB_CHANNEL_FREE;assert(!w.isChannelActive());assert(w.getCADBusyCount()==1);
 phy.scan_result=-706;assert(w.isChannelActive());assert(w.getCADBusyCount()==1);
 puts("PASS CAD -> RXPS and busy counter excludes errors/free channel");
 w.arm_result=-708;w.restart();int before=phy.continuous;w.pump();
 assert(!w.getRxPowerSavingStatus().armed);assert(phy.continuous>before);assert(w.getRxPowerSavingStatus().last_error==-708);
 puts("PASS arm failure -> continuous RX with error");
 assert(w.setRxPowerSaving(false,0,0));w.pump();assert(!w.getRxPowerSavingStatus().armed);
 w.setRxPowerSavingConfigError(true);assert(w.getRxPowerSavingStatus().last_error==RX_POWERSAVING_ERR_TIMING);
 w.setRxPowerSavingConfigError(false);w.arm_result=0;
 assert(w.setRxPowerSaving(true,30000,20000));w.pump();assert(w.getRxPowerSavingStatus().armed);
 puts("PASS off/on and configuration error separation");
 w.arm_result=-708;assert(w.setRxPowerSaving(true,30000,20000));int arms_before=w.arms;
 for(int i=0;i<8;++i){w.restart();w.pump();}
 assert(w.arms-arms_before==3);assert(!w.getRxPowerSavingStatus().armed);
 puts("PASS retry limit: three failures then continuous RX");
 w.arm_result=0;assert(w.setRxPowerSaving(true,30000,20000));w.pump();
 assert(w.getRxPowerSavingStatus().armed);
 w.receiving=true;int stops_before=w.stops;
 assert(w.setRxPowerSaving(false,30000,20000));w.loop();w.pump();
 assert(w.stops==stops_before);assert(w.getRxPowerSavingStatus().armed);
 w.receiving=false;phy.irq();w.pump();assert(!w.getRxPowerSavingStatus().armed);
 puts("PASS mode change preserves in-flight RX");
 assert(w.setRxPowerSaving(true,30000,20000));w.loop();w.pump();
 w.inject_frame=true;w.resetAGC();
 assert(w.receiving);assert(!w.getRxPowerSavingStatus().armed);
 int continuous_after=phy.continuous;w.pump();assert(phy.continuous==continuous_after);
 w.receiving=false;phy.irq();w.pump();assert(w.getRxPowerSavingStatus().armed);
 puts("PASS frame starting during AGC calibration is retained");
 w.resetAGC();assert(w.getNoiseFloor()==-110);w.pump();assert(w.getRxPowerSavingStatus().armed);
 puts("PASS successful AGC calibration restores RXPS and publishes floor");
 assert(w.setRxPowerSaving(false,30000,20000));w.loop();w.pump();
 int standby_before=phy.standbys;
 w.setTxPower(14);assert(phy.power_calls==1);assert(phy.standbys==standby_before);
 puts("PASS setTxPower leaves continuous RX alone on SX126x/LR11x0");
 phy.continuous_result=-707;w.restart();w.pump();
 assert(w.getRxPowerSavingStatus().last_error==-707);
 phy.continuous_result=0;w.restart();w.pump();assert(w.getRxPowerSavingStatus().last_error==0);
 puts("PASS continuous RX failure and recovery are reported");
#endif
}
