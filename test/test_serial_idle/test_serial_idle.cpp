#include <gtest/gtest.h>
#include <helpers/MultiSerialInterface.h>
struct QueuedSerial : BaseSerialInterface {
 bool enabled=false, queued=false;
 void enable() override {enabled=true;}
 void disable() override {enabled=false;}
 bool isEnabled() const override {return enabled;}
 bool isConnected() const override {return enabled;}
 bool isReadBusy() const override {return queued;}
 bool isWriteBusy() const override {return false;}
 size_t writeFrame(const uint8_t*,size_t len) override {return len;}
 size_t checkRecvFrame(uint8_t*) override {bool had=queued;queued=false;return had?1:0;}
};
TEST(CompanionIdle, QueuedFrameHoldsIdleUntilConsumed) {
 MultiSerialInterface interfaces;QueuedSerial ble;uint8_t buffer[8];
 ASSERT_TRUE(interfaces.addInterface(InterfaceType::Bluetooth,&ble));interfaces.enable();
 ble.queued=true;EXPECT_TRUE(interfaces.isReadBusy());
 EXPECT_EQ(1U,interfaces.checkRecvFrame(buffer));EXPECT_FALSE(interfaces.isReadBusy());
}
TEST(CompanionIdle, DisabledInterfaceDoesNotBlockOtherInterfaces) {
 MultiSerialInterface interfaces;QueuedSerial ble,usb;
 interfaces.addInterface(InterfaceType::Bluetooth,&ble);interfaces.addInterface(InterfaceType::USB,&usb);
 interfaces.enable();ble.queued=true;interfaces.disableBluetooth();EXPECT_FALSE(interfaces.isReadBusy());
 usb.queued=true;EXPECT_TRUE(interfaces.isReadBusy());interfaces.disable();EXPECT_FALSE(interfaces.isReadBusy());
}
int main(int argc,char** argv){testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();}
