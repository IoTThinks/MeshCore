#include <gtest/gtest.h>
#include "helpers/PeriodicReboot.h"
TEST(PeriodicReboot, DeadlineDefersDuringTransmitAndOTA) {
 PeriodicReboot timer;
 EXPECT_FALSE(timer.due(1,3599999,false,false));
 EXPECT_FALSE(timer.due(1,3600000,true,false));
 EXPECT_FALSE(timer.due(1,3600001,false,true));
 EXPECT_TRUE(timer.due(1,3600002,false,false));
 EXPECT_FALSE(timer.due(0,3600003,false,false));
}
TEST(PeriodicReboot, LongUptimeAndRolloverKeepTheDeadline) {
 PeriodicReboot timer;
 EXPECT_FALSE(timer.due(255,900000000,false,false));
 EXPECT_TRUE(timer.due(255,918000000,false,false));
 EXPECT_FALSE(timer.due(0,0xFFFFFFF0U,false,false));
 EXPECT_TRUE(timer.due(255,0x10U,false,false));
}
TEST(PeriodicReboot, StrictRangeAndNoPartialMutation) {
 uint8_t hours=7;
 for(const char* bad : {"", "-1", "abc", "256", "9999999999999", "1x"}) {
  EXPECT_FALSE(PeriodicReboot::parseHours(bad,hours)); EXPECT_EQ(7,hours);
 }
 EXPECT_TRUE(PeriodicReboot::parseHours("0",hours)); EXPECT_EQ(0,hours);
 EXPECT_TRUE(PeriodicReboot::parseHours("255",hours)); EXPECT_EQ(255,hours);
}
int main(int argc,char**argv) { testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS(); }
