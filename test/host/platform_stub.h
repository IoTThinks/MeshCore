#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
using std::min; using std::max;
inline uint32_t radio_test_millis = 100;
inline unsigned long millis() { return radio_test_millis; }
inline void delay(unsigned long ms) { radio_test_millis += ms; }
class SPIClass { public: void begin() {} };
struct SerialStub { template<class T> void print(T) {} template<class T> void println(T) {} };
inline SerialStub Serial;
#define LORA_FREQ 869.618f
#define LORA_BW 62.5f
#define LORA_SF 8
#define LORA_TX_POWER 14
inline long random(long a,long) { return a; }
inline void noInterrupts() {}
inline void interrupts() {}
#define LR2021_IRQ_DIO 9
