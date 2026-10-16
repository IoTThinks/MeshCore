#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>
#define NodePrefs CompanionNodePrefs
#include "../../examples/companion_radio/NodePrefs.h"
#undef NodePrefs
struct TestFileSystem { void mkdir(const char*) {} };
#define FILESYSTEM TestFileSystem
#include <helpers/CommonCLI.h>
#include "target.h"
class ReplayStream : public Stream {
  const char* _text;
  int _pos = 0;
  int _len;

public:
  explicit ReplayStream(const char* text) : _text(text), _len(strlen(text)) { }

  int available() override { return _len - _pos; }
  int read() override { return _pos < _len ? _text[_pos++] : -1; }
  int peek() override { return _pos < _len ? _text[_pos] : -1; }
};

class CaptureStream : public Stream {
  std::string _text;

  size_t emit(long long value) {
    char text[24];
    int length = snprintf(text, sizeof(text), "%lld", value);
    return write(reinterpret_cast<const uint8_t*>(text), length);
  }

public:
  size_t write(uint8_t value) override {
    _text.push_back(static_cast<char>(value));
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    _text.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

  size_t print(unsigned char value, int = DEC) override { return emit(value); }
  size_t print(int value, int = DEC) override { return emit(value); }
  size_t print(unsigned int value, int = DEC) override { return emit(value); }
  size_t print(long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long value, int = DEC) override { return emit(value); }
  size_t print(long long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long long value, int = DEC) override { return emit(value); }

  size_t print(double value, int precision = 2) override {
    char out[64]; int length=snprintf(out,sizeof(out),"%.*f",precision,value);
    return write(reinterpret_cast<const uint8_t*>(out),length);
  }
  const std::string& text() const { return _text; }
};


template<class T> class RadioPrefsTest : public testing::Test {
protected:
 T prefs;
 void SetUp() override { radio_driver=ConfigTestRadio{};prefs.sf=8;prefs.bw=62.5f; }
};
using PrefsTypes=testing::Types<NodePrefs,CompanionNodePrefs>;
TYPED_TEST_SUITE(RadioPrefsTest,PrefsTypes);
TYPED_TEST(RadioPrefsTest, FreshDefaultsAndSavedOffArePreserved) {
 EXPECT_EQ(1,this->prefs.rxps.enabled); EXPECT_EQ(6,this->prefs.rxps.level); EXPECT_EQ(16,this->prefs.rxps.preamble);
 ReplayStream input("{radio:{rxps_en:0,rxps_level:8,rxps_preamble:32}}");
 ASSERT_TRUE(this->prefs.loadSerial(input)); EXPECT_EQ(0,this->prefs.rxps.enabled); EXPECT_EQ(8,this->prefs.rxps.level);
 ASSERT_TRUE(this->prefs.getRadioPrefs()->applyRxPowerSaving(8,62.5)); EXPECT_FALSE(radio_driver.enabled);
}
TYPED_TEST(RadioPrefsTest, ManualPeriodsRoundTripAndSurviveRetuning) {
 auto* p=this->prefs.getRadioPrefs();char reply[160];
 ASSERT_TRUE(p->applyRxPowerSaving(8,62.5));
 ASSERT_TRUE(p->handleCommand("set radio.rxps 12345 23456",0,reply)); ASSERT_EQ(0,this->prefs.rxps.level);
 CaptureStream output;ASSERT_TRUE(this->prefs.saveSerial(output));
 TypeParam loaded;ReplayStream input(output.text().c_str());ASSERT_TRUE(loaded.loadSerial(input));
 ASSERT_TRUE(loaded.getRadioPrefs()->applyRxPowerSaving(10,125));
 EXPECT_EQ(12345U,radio_driver.rx);EXPECT_EQ(23456U,radio_driver.sleep);
 EXPECT_EQ(1,loaded.rxps.enabled); EXPECT_EQ(0,loaded.rxps.level);
}
TYPED_TEST(RadioPrefsTest, TemporaryRadioFailureRestoresWithoutChangingStoredIntent) {
 auto* p=this->prefs.getRadioPrefs();const auto original=this->prefs.rxps;
 ASSERT_TRUE(p->applyRxPowerSaving(8,62.5)); auto rx=radio_driver.rx;
 EXPECT_FALSE(p->applyRxPowerSaving(6,250)); EXPECT_FALSE(radio_driver.enabled); EXPECT_TRUE(radio_driver.invalid);
 EXPECT_EQ(1,this->prefs.rxps.enabled);EXPECT_EQ(original.rx_us,this->prefs.rxps.rx_us);
 ASSERT_TRUE(p->applyRxPowerSaving(8,62.5));EXPECT_TRUE(radio_driver.enabled);EXPECT_EQ(rx,radio_driver.rx);EXPECT_FALSE(radio_driver.invalid);
}
TYPED_TEST(RadioPrefsTest, SharedCLIValidatesChangesAndTracksDirtyState) {
 auto* p=this->prefs.getRadioPrefs();char reply[160];p->applyRxPowerSaving(8,62.5);this->prefs.clearDirty();
 ASSERT_TRUE(p->handleCommand("set radio.rxps off",0,reply));EXPECT_FALSE(radio_driver.enabled);EXPECT_TRUE(this->prefs.isDirty());
 ASSERT_TRUE(p->handleCommand("set radio.rxps balanced",0,reply));EXPECT_TRUE(radio_driver.enabled);
 ASSERT_TRUE(p->handleCommand("get radio.rxps ",0,reply));EXPECT_NE(nullptr,strstr(reply,"desired=on"));
 ASSERT_TRUE(p->handleCommand("set radio 869.618,250,6,5",0,reply));EXPECT_FALSE(radio_driver.enabled);EXPECT_TRUE(radio_driver.invalid);EXPECT_EQ(1,this->prefs.rxps.enabled);
 EXPECT_EQ(6,this->prefs.sf);EXPECT_NE(nullptr,strstr(reply,"invalid timing"));
 ASSERT_TRUE(p->handleCommand("get radio.rxps",0,reply));
 EXPECT_NE(nullptr,strstr(reply,"err=-10001"));EXPECT_EQ(nullptr,strstr(reply,"catch="));
}
TYPED_TEST(RadioPrefsTest, UnsupportedRXPSDoesNotOverwriteSavedChoice) {
 radio_driver.supported=false;char reply[160];this->prefs.rxps.enabled=0;
 this->prefs.getRadioPrefs()->handleCommand("set radio.rxps balanced",0,reply);
 EXPECT_EQ(0,this->prefs.rxps.enabled);EXPECT_NE(nullptr,strstr(reply,"unsupported"));
}
TEST(CommonPowerPrefs, SavedRebootAndPowerSavingOverrideDefaults) {
 NodePrefs p;EXPECT_EQ(1,p.powersaving_enabled);EXPECT_EQ(0,p.reboot_interval);
 ReplayStream input("{power:{pwr_sav_en:0,pwr_reboot_int:255}}");ASSERT_TRUE(p.loadSerial(input));
 EXPECT_EQ(0,p.powersaving_enabled);EXPECT_EQ(255,p.reboot_interval);
 CaptureStream output;ASSERT_TRUE(p.saveSerial(output));
 NodePrefs restored;ReplayStream saved(output.text().c_str());ASSERT_TRUE(restored.loadSerial(saved));
 EXPECT_EQ(0,restored.powersaving_enabled);EXPECT_EQ(255,restored.reboot_interval);
}
TEST(CompanionPowerPrefs, DefaultOnAndSavedOffRoundTrip) {
 CompanionNodePrefs prefs; EXPECT_EQ(1,prefs.powersaving_enabled);
 ReplayStream input("{power:{pwr_sav_en:0}}"); ASSERT_TRUE(prefs.loadSerial(input));
 EXPECT_EQ(0,prefs.powersaving_enabled);
 CaptureStream output; ASSERT_TRUE(prefs.saveSerial(output));
 CompanionNodePrefs restored; ReplayStream saved(output.text().c_str());
 ASSERT_TRUE(restored.loadSerial(saved)); EXPECT_EQ(0,restored.powersaving_enabled);
}
int main(int argc,char** argv){testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();}

TYPED_TEST(RadioPrefsTest, FEMDefaultsPersistAndDoNotChangeInternalGainOrTX) {
 TypeParam prefs;char reply[160];
 prefs.rx_boosted_gain=1;prefs.radio_fem_txgain=1;
 board=ConfigTestBoard{};board.attachDynamicPrefs(prefs.getCustom());
 EXPECT_FALSE(board.lna);
 auto* radio=prefs.getRadioPrefs();
 ASSERT_TRUE(radio->handleCommand("set radio.fem.rxgain on",0,reply));
 EXPECT_STREQ("OK",reply);EXPECT_TRUE(board.lna);EXPECT_TRUE(prefs.isDirty());
 CaptureStream saved;prefs.saveSerial(saved);
 TypeParam restored;ReplayStream input(saved.text().c_str());restored.loadSerial(input);
 board.attachDynamicPrefs(restored.getCustom());EXPECT_TRUE(board.lna);
 auto* next=restored.getRadioPrefs();
 next->handleCommand("get radio.fem.rxgain",0,reply);EXPECT_STREQ("> on",reply);
 next->handleCommand("set radio.fem.rxgain onion",0,reply);EXPECT_NE(strstr(reply,"Error"),nullptr);EXPECT_TRUE(board.lna);
 next->handleCommand("set radio.fem.rxgain off",0,reply);EXPECT_FALSE(board.lna);
 EXPECT_EQ(1,restored.rx_boosted_gain);EXPECT_EQ(1,restored.radio_fem_txgain);
 CaptureStream saved_off;ASSERT_TRUE(restored.saveSerial(saved_off));
 TypeParam off_restored;ReplayStream off_input(saved_off.text().c_str());
 ASSERT_TRUE(off_restored.loadSerial(off_input));
 board.attachDynamicPrefs(off_restored.getCustom());EXPECT_FALSE(board.lna);
 EXPECT_EQ(1,off_restored.rx_boosted_gain);EXPECT_EQ(1,off_restored.radio_fem_txgain);
 next=off_restored.getRadioPrefs();
 board.supported=false;
 next->handleCommand("set radio.fem.rxgain on",0,reply);EXPECT_STREQ("Error: unsupported",reply);
 next->handleCommand("get radio.fem.rxgain",0,reply);EXPECT_STREQ("Error: unsupported",reply);
 EXPECT_FALSE(board.lna);
}
