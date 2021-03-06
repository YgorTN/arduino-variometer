#ifndef VARIOSCREEN_H
#define VARIOSCREEN_H

#include <Arduino.h>
#include <SPI.h>
#include <digit.h>

#define VARIOSCREEN_DISPLAY_THRESHOLD 0.65

class VarioScreen {
 public:
  VarioScreen(int8_t DC, int8_t CS, int8_t RST);
  void begin(uint8_t clockDiviser = SPI_CLOCK_DIV2, uint8_t contrast = 40, uint8_t bias = 0x04);
  void beginDisplay(uint8_t x, uint8_t y);
  void display(uint8_t displayByte);
  void endDisplay();
  void flush();
  void clear();

 private:
  void command(uint8_t c);
  int8_t _dc, _rst, _cs;
};


class ScreenDigit {

 public:
  ScreenDigit(VarioScreen& screen, uint8_t anchorX, uint8_t anchorY, uint8_t precision, boolean plusDisplay = false) : screen(screen), digit(precision, plusDisplay), anchorX(anchorX), anchorY(anchorY) { lastDisplayWidth = 0; lastDisplayValue = 10000000.0; }
  
  void display(double value);

 private:
  void displayCharacter(const uint8_t* pointer, uint8_t width, uint8_t line);
  VarioScreen& screen;
  Digit digit;
  uint8_t anchorX, anchorY;
  uint8_t lastDisplayWidth;
  double lastDisplayValue;
};

class MSUnit {

 public :
  MSUnit(VarioScreen& screen) : screen(screen) { }
  void display(uint8_t posX, uint8_t posY);

 private :
  VarioScreen& screen;
};

class MUnit {

 public :
  MUnit(VarioScreen& screen) : screen(screen) { }
  void display(uint8_t posX, uint8_t posY);

 private :
  VarioScreen& screen;
};

class KMHUnit {

 public :
  KMHUnit(VarioScreen& screen) : screen(screen) { }
  void display(uint8_t posX, uint8_t posY);

 private :
  VarioScreen& screen;
};

class GRUnit {

 public :
  GRUnit(VarioScreen& screen) : screen(screen) { }
  void display(uint8_t posX, uint8_t posY);

 private :
  VarioScreen& screen;
};


#endif
