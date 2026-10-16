#include <gtest/gtest.h>
#define NRF52_PLATFORM
inline void randomSeed(long) {}
inline long random(long low, long) {return low;}
#include <helpers/ArduinoHelpers.h>
extern "C" { volatile uint32_t persistent_magic; volatile uint32_t persistent_time; }
TEST(RetainedClock, ValidTimeSurvivesSoftwareRestart) {
 persistent_magic=0;g_mock_millis=0;
 VolatileRTCClock first;first.setCurrentTime(RTC_TIME_MIN+123);
 g_mock_millis=1500;first.tick();EXPECT_EQ(RTC_TIME_MIN+124,first.getCurrentTime());
 g_mock_millis=0;VolatileRTCClock restarted;
 EXPECT_EQ(RTC_TIME_MIN+124,restarted.getCurrentTime());
}
TEST(RetainedClock, InvalidMagicOrTimeUsesSeed) {
 persistent_magic=0;persistent_time=RTC_TIME_MIN+123;
 VolatileRTCClock bad_magic;EXPECT_EQ(RTC_TIME_MIN,bad_magic.getCurrentTime());
 persistent_magic=CLOCK_MAGIC_NUM;persistent_time=RTC_TIME_MIN-1;
 VolatileRTCClock bad_time;EXPECT_EQ(RTC_TIME_MIN,bad_time.getCurrentTime());
}
TEST(RetainedClock, MillisRolloverDoesNotJumpTime) {
 persistent_magic=0;g_mock_millis=0xFFFFFF00U;
 VolatileRTCClock clock;
 g_mock_millis=744;clock.tick();EXPECT_EQ(RTC_TIME_MIN+1,clock.getCurrentTime());
 EXPECT_EQ(clock.getCurrentTime(),persistent_time);
}
int main(int argc,char**argv) {testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();}
