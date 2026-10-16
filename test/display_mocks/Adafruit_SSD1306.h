#pragma once
#include <Wire.h>
#define SSD1306_BLACK 0
#define SSD1306_WHITE 1
#define SSD1306_SWITCHCAPVCC 2
#define SSD1306_DISPLAYON 0xAF
#define SSD1306_DISPLAYOFF 0xAE
class Adafruit_SSD1306 {
public:
 inline static bool init_ok=true;
 inline static int starts=0, commands=0;
 Adafruit_SSD1306(int,int,TwoWire*,int) {}
 bool begin(int,int,bool,bool) { ++starts;return init_ok; }
 void ssd1306_command(int) { ++commands; }
 void setRotation(int) {} void clearDisplay() {} void display() {}
 void setTextColor(int) {} void setTextSize(int) {} void cp437(bool) {}
 void setCursor(int,int) {} void print(const char*) {}
 void fillRect(int,int,int,int,int) {} void drawRect(int,int,int,int,int) {}
 void drawBitmap(int,int,const uint8_t*,int,int,int) {}
 void getTextBounds(const char*,int,int,int16_t*,int16_t*,uint16_t*,uint16_t*) {}
};
