#include <gtest/gtest.h>
#include <helpers/IdleSleep.h>
TEST(IdleSleep, ShortensToEarliestDeadline) {
 IdleSleepBudget budget(1000);
 budget.until(5000);budget.until(1006);budget.until(1009);
 EXPECT_EQ(6U,budget.millis());
}
TEST(IdleSleep, ReadyWorkPreventsSleepingAndDisabledTimerDoesNot) {
 IdleSleepBudget budget(1000);budget.until(0);EXPECT_EQ((uint32_t)IDLE_SLEEP_MAX_MILLIS,budget.millis());
 budget.until(1000);EXPECT_EQ(0U,budget.millis());
 IdleSleepBudget overdue(1000);overdue.until(999);EXPECT_EQ(0U,overdue.millis());
}
TEST(IdleSleep, TimerWrapKeepsFutureAndPastDeadlinesSeparate) {
 IdleSleepBudget before(0xfffffffeU);before.until(3);EXPECT_EQ(5U,before.millis());
 IdleSleepBudget after(2);after.until(0xfffffffeU);EXPECT_EQ(0U,after.millis());
}
TEST(IdleSleep, FarDeadlineIsCappedAtTheRoleMaximum) {
 IdleSleepBudget budget(1000);budget.until(1000+10*IDLE_SLEEP_MAX_MILLIS);
 EXPECT_EQ((uint32_t)IDLE_SLEEP_MAX_MILLIS,budget.millis());
 IdleSleepBudget sixty(5000,60000);sixty.until(5000+45000);EXPECT_EQ(45000U,sixty.millis());
}
int main(int argc,char**argv) {testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();}
