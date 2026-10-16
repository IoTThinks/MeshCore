#include <gtest/gtest.h>
#include "helpers/Uptime.h"
TEST(Uptime, IncludesTimeBeforeFirstPollAndSurvivesRollover) {
 Uptime t; t.tick(1234); EXPECT_EQ(1234U,t.milliseconds());
 t.tick(0xFFFFFFF0U); t.tick(0x10U); EXPECT_EQ(0x100000010ULL,t.milliseconds());
 t.tick(0x10U); EXPECT_EQ(0x100000010ULL,t.milliseconds());
}
int main(int argc,char**argv) { testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS(); }
