#include "SSD1306Display.h"

bool SSD1306Display::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

// Color scheme
ColorVal UIColor::window_bkg = SSD1306_BLACK;
ColorVal UIColor::title_bkg = SSD1306_BLACK;
ColorVal UIColor::title_txt = SSD1306_WHITE;
ColorVal UIColor::primary_txt = SSD1306_WHITE;
ColorVal UIColor::secondary_txt = SSD1306_WHITE;
ColorVal UIColor::warning_txt = SSD1306_WHITE;
ColorVal UIColor::popup_bkg = SSD1306_BLACK;
ColorVal UIColor::popup_txt = SSD1306_WHITE;
ColorVal UIColor::corp_blue = SSD1306_WHITE;

bool SSD1306Display::begin() {
  if (!_isOn && _peripher_power) _peripher_power->claim();
  const bool ok = display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, true, false)
                  && i2c_probe(Wire, DISPLAY_ADDRESS);
  if (!ok) {
    if (_peripher_power) _peripher_power->release();
    _isOn = false;
    return false;
  }
#ifdef DISPLAY_ROTATION
  display.setRotation(DISPLAY_ROTATION);
#endif
  _isOn = true;
  return true;
}

void SSD1306Display::turnOn() {
  if (!_isOn) {
    if (_peripher_power) {
      if (!begin()) return;  // power was cut: claim it and re-init; released again if no display answers
    } else {
      // No switched supply, so the panel kept its state. A full begin() would also pulse
      // PIN_OLED_RESET, which defaults to 21 = SX126X_POWER_EN (FEM enable) on RAK3401.
      _isOn = true;
    }
  }
  display.ssd1306_command(SSD1306_DISPLAYON);
}

void SSD1306Display::turnOff() {
  if (!_isOn) return;
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (_peripher_power) {
#if PIN_OLED_RESET >= 0
    digitalWrite(PIN_OLED_RESET, LOW);
#endif
    _peripher_power->release();
  }
  _isOn = false;
}

void SSD1306Display::clear() {
  display.clearDisplay();
  display.display();
}

void SSD1306Display::startFrame(ColorVal bkg) {
  display.clearDisplay();  // TODO: apply 'bkg'
  _color = SSD1306_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void SSD1306Display::setTextSize(int sz) {
  display.setTextSize(sz);
}

void SSD1306Display::setColor(ColorVal c) {
  _color = c;
  display.setTextColor(_color);
}

void SSD1306Display::setCursor(int x, int y) {
  display.setCursor(x, y);
}

void SSD1306Display::print(const char* str) {
  display.print(str);
}

void SSD1306Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, _color);
}

void SSD1306Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x, y, w, h, _color);
}

void SSD1306Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x, y, bits, w, h, _color);
}

uint16_t SSD1306Display::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SSD1306Display::endFrame() {
  display.display();
}
