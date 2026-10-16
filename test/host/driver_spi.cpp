#include <cstdio>
#include <vector>
#include <cassert>
#include "CustomSX1262.h"
#include "CustomLR1110.h"
class TraceHal : public RadioLibHal {
public:
 bool lr=false; uint16_t pending=0; uint8_t error=0; unsigned long now=0;
 std::vector<std::vector<uint8_t>> traces;
 uint32_t pins[32]={};
 TraceHal(bool islr): RadioLibHal(0,1,0,1,1,2),lr(islr) {}
 void pinMode(uint32_t,uint32_t) override {}
 void digitalWrite(uint32_t p,uint32_t v) override {if(p<32)pins[p]=v;}
 uint32_t digitalRead(uint32_t) override {return 0;}
 void attachInterrupt(uint32_t,void(*)(void),uint32_t) override {}
 void detachInterrupt(uint32_t) override {}
 void delay(RadioLibTime_t t) override {now+=t*1000;}
 void delayMicroseconds(RadioLibTime_t t) override {now+=t;}
 RadioLibTime_t millis() override {return now++/1000;}
 RadioLibTime_t micros() override {return now++;}
 long pulseIn(uint32_t,uint32_t,RadioLibTime_t) override {return 0;}
 void spiBegin() override {}
 void spiBeginTransaction() override {}
 void spiEndTransaction() override {}
 void spiEnd() override {}
 void spiTransfer(uint8_t* out,size_t len,uint8_t* in) override {
  traces.emplace_back(out,out+len); memset(in,0,len);
  if(lr){
   if(len)in[0]=error?error:0x04;
   if(len>=2 && out[0]){pending=(uint16_t(out[0])<<8)|out[1];}
   else if(pending && len>=2){
    if(pending==RADIOLIB_LR11X0_CMD_GET_PACKET_TYPE)in[1]=RADIOLIB_LR11X0_PACKET_TYPE_LORA;
    pending=0;
   }
  }else{
   if(len>1)in[1]=error?error:0x24;
   if(len>2 && out[0]==RADIOLIB_SX126X_CMD_GET_PACKET_TYPE)in[2]=RADIOLIB_SX126X_PACKET_TYPE_LORA;
  }
 }
 void output(const char* name,int result,uint32_t eff=0){
  printf("%s result=%d eff=%u",name,result,eff);
  for(auto& t:traces){printf(" |");for(auto b:t)printf("%02x",b);}
  printf("\n"); traces.clear();
 }
};
struct LRProbe : CustomLR1110 {
 using CustomLR1110::CustomLR1110;
 using LRxxxx::tcxoDelay;
 using LRxxxx::spreadingFactor;
 using LRxxxx::bandwidthKhz;
 using LRxxxx::preambleLengthLoRa;
 using LRxxxx::headerType;
 using LRxxxx::implicitLen;
 using LRxxxx::crcTypeLoRa;
 using LRxxxx::invertIQEnabled;
};
int main(){
 TraceHal hs(false),hl(true); Module ms(&hs,1,2,3,4),ml(&hl,1,2,3,4);
 CustomSX1262 sx(&ms); LRProbe lr(&ml);
 ms.spiConfig.stream=true;ms.spiConfig.statusPos=1;
 ms.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR]=Module::BITS_16;
 ms.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD]=Module::BITS_8;
 ms.spiConfig.parseStatusCb=SX126x::SPIparseStatus;
 ms.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ]=RADIOLIB_SX126X_CMD_READ_REGISTER;
 ms.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE]=RADIOLIB_SX126X_CMD_WRITE_REGISTER;
 ms.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_NOP]=0;
 ms.spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS]=RADIOLIB_SX126X_CMD_GET_STATUS;
 ml.spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR]=Module::BITS_32;
 sx.tcxoDelay=lr.tcxoDelay=1600;
 sx.spreadingFactor=lr.spreadingFactor=8;sx.bandwidthKhz=lr.bandwidthKhz=62.5;
 sx.preambleLengthLoRa=lr.preambleLengthLoRa=16;
 sx.headerType=lr.headerType=0;sx.implicitLen=lr.implicitLen=255;
 sx.crcTypeLoRa=lr.crcTypeLoRa=1;sx.invertIQEnabled=lr.invertIQEnabled=false;
 sx.setRfSwitchPins(10,11);
 for(auto periods:std::vector<std::pair<uint32_t,uint32_t>>{{1000,1000},{1000,2600},{1000,2850},{30000,20000},{200000,200000},{30000000,30000000}}){
  char name[90];snprintf(name,sizeof(name),"SX rx=%u sleep=%u",periods.first,periods.second);
  int r=sx.startReceiveDutyCycle(periods.first,periods.second);hs.output(name,r);
  if(periods.second>2600)assert(r==0 && hs.pins[10]==1 && hs.pins[11]==0);
  else assert(r==RADIOLIB_ERR_INVALID_SLEEP_PERIOD);
  uint32_t eff=0;snprintf(name,sizeof(name),"LR rx=%u sleep=%u",periods.first,periods.second);
  r=lr.startReceiveDutyCycle(periods.first,periods.second,RADIOLIB_IRQ_RX_DEFAULT_FLAGS,RADIOLIB_IRQ_RX_DEFAULT_MASK,&eff);
  if(periods.first==200000 && r==0) {
    bool found=false;
    for(auto& cmd:hl.traces) if(cmd.size()>=8 && cmd[0]==0x02 && cmd[1]==0x14) {
      unsigned ticks=(unsigned(cmd[2])<<16)|(unsigned(cmd[3])<<8)|cmd[4];
      assert(ticks==6553);found=true;
    }
    assert(found);
  }
  hl.output(name,r,eff);
  if(periods.second>2600)assert(r==0);
  else assert(r==RADIOLIB_ERR_INVALID_SLEEP_PERIOD);
 }
 int st=sx.getStatus(); assert(st==0x24); hs.output("SX getStatus",st);
 hs.error=0x28; st=sx.startReceiveDutyCycle(30000,20000);
 assert(st==RADIOLIB_ERR_SPI_CMD_INVALID);hs.output("SX injected command error",st);hs.error=0;
 hl.error=0x02; st=lr.startReceiveDutyCycle(30000,20000);
 assert(st==RADIOLIB_ERR_SPI_CMD_INVALID);hl.output("LR injected command error",st);hl.error=0;
 st=sx.stopRTC();assert(st==0);hs.output("SX stopRTC",st);
 st=lr.restoreLfClock();assert(st==0);
 assert(!hl.traces.empty() && hl.traces.front()==std::vector<uint8_t>({0x01,0x16,0x01}));
 hl.output("LR restoreLF",st);
 unsigned valid=0,invalid=0;
 for(float bw:{7.8f,10.4f,15.6f,20.8f,31.25f,41.7f,62.5f,125.0f,250.0f,500.0f})
 for(uint8_t sf=5;sf<=12;sf++)for(uint8_t p:{16,32})for(uint8_t level=1;level<=10;level++)
 for(float cost:{6.f,8.f})for(uint32_t transition:{1000u,2600u,6000u}){
  uint32_t rx=0,sl=0;bool ok=recalcRxPowerSavingFromLevel(level,sf,bw,p,&rx,&sl,cost,transition);
  if(ok) {
    assert(isValidRxPowerSavingPeriod(rx) && isValidRxPowerSavingPeriod(sl));
    assert(sl>transition);
    valid++;
  } else invalid++;
 }
 printf("PROFILE MATRIX valid=%u rejected=%u\n",valid,invalid);
}
