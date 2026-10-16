#include <gtest/gtest.h>
#include "helpers/ui/SSD1306Display.cpp"
class OLED : public testing::Test {
protected:
 RefCountedDigitalPin power{7};
 SSD1306Display display{&power};
 void SetUp() override { power.begin(); Wire.error=0; Adafruit_SSD1306::init_ok=true; Adafruit_SSD1306::starts=Adafruit_SSD1306::commands=0; }
};
TEST_F(OLED, FailedAllocationReleasesClaimAndDoesNotSendOn) {
 Adafruit_SSD1306::init_ok=false; display.turnOn();
 EXPECT_FALSE(display.isOn()); EXPECT_EQ(LOW,pin_levels[7]); EXPECT_EQ(0,Adafruit_SSD1306::commands);
}
TEST_F(OLED, FailedProbeCanRetryAndRepeatedBeginDoesNotLeakClaim) {
 Wire.error=4; EXPECT_FALSE(display.begin()); EXPECT_EQ(LOW,pin_levels[7]);
 Wire.error=0; EXPECT_TRUE(display.begin()); EXPECT_TRUE(display.begin());
 display.turnOff(); EXPECT_EQ(LOW,pin_levels[7]); EXPECT_FALSE(display.isOn());
}
TEST_F(OLED, FailureDuringReinitializationReleasesExistingClaim) {
 ASSERT_TRUE(display.begin()); Wire.error=4; EXPECT_FALSE(display.begin());
 EXPECT_FALSE(display.isOn()); EXPECT_EQ(LOW,pin_levels[7]);
}
TEST_F(OLED, SharedSupplySurvivesFailureAndRepeatedOff) {
 power.claim(); Wire.error=4; EXPECT_FALSE(display.begin());
 EXPECT_EQ(HIGH,pin_levels[7]); display.turnOff(); display.turnOff();
 EXPECT_EQ(HIGH,pin_levels[7]); power.release(); EXPECT_EQ(LOW,pin_levels[7]);
}
TEST_F(OLED, RepeatedPowerCyclesReinitializeOnceAndBalanceClaims) {
 power.claim();
 for(int i=0;i<5;++i) {
  display.turnOn(); display.turnOn(); EXPECT_TRUE(display.isOn());
  EXPECT_EQ(i+1,Adafruit_SSD1306::starts);
  display.turnOff(); display.turnOff(); EXPECT_EQ(HIGH,pin_levels[7]);
 }
 power.release(); EXPECT_EQ(LOW,pin_levels[7]);
}
TEST(OLEDWithoutSwitchedPower, WakeDoesNotReinitializeOrPulseReset) {
 Wire.error=0; Adafruit_SSD1306::init_ok=true; Adafruit_SSD1306::starts=Adafruit_SSD1306::commands=0;
 SSD1306Display display;
 ASSERT_TRUE(display.begin()); EXPECT_EQ(1,Adafruit_SSD1306::starts);
 for(int i=0;i<3;++i) {
  display.turnOff(); EXPECT_FALSE(display.isOn());
  display.turnOn(); EXPECT_TRUE(display.isOn());
 }
 EXPECT_EQ(1,Adafruit_SSD1306::starts);  // begin() pulses PIN_OLED_RESET; only the boot init may do that
}
TEST(OLEDWithoutSwitchedPower, MissingPanelIsNotRetriedOnWake) {
 Wire.error=4; Adafruit_SSD1306::init_ok=true; Adafruit_SSD1306::starts=Adafruit_SSD1306::commands=0;
 SSD1306Display display;
 EXPECT_FALSE(display.begin()); display.turnOn(); display.turnOff(); display.turnOn();
 EXPECT_EQ(1,Adafruit_SSD1306::starts);
 Wire.error=0;
}
int main(int argc,char**argv) { testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS(); }
