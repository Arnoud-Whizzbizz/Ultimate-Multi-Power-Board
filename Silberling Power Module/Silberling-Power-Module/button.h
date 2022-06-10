// Super simple button debounce - read http://www.ganssle.com/debouncing-pt2.htm
// Based on https://www.e-tinkers.com/2021/05/the-simplest-button-debounce-solution/

#ifndef button_h
#define button_h

#include "Arduino.h"

class Button {
  private:
    uint8_t btn;
    uint16_t state;
  public:
    void begin(uint8_t button) {
      btn = button;
      state = 0;
      pinMode(btn, INPUT_PULLUP);
    }
    //bool debounce() {
    //  state = (state<<1) | digitalRead(btn) | 0xfe00;
    //  return (state == 0xff00);
    //}
    bool debounce() {
      // Changed 5 bits bubble to 2 bits, this is quick enough... 
      state = (state<<1) | digitalRead(btn) | 0xFFF8;
      return (state == 0xFFFC);
    }

    bool longpress() {
      state = (state<<1) | digitalRead(btn) | 0xf000000000000000;
      return (state == 0xf800000000000000);
    }
};
#endif
