#include <stdint.h>
#ifdef NRF52_PLATFORM
extern "C" {
__attribute__((section(".persistent_magic"))) volatile uint32_t persistent_magic;
__attribute__((section(".persistent_data"))) volatile uint32_t persistent_time;
}
#endif
