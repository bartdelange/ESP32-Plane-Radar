/** Device display bring-up: GC9A01 over SPI. */

#include "ui/display.h"

#include "config.h"
#include "ui/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}

uint16_t displayColor565(uint8_t r, uint8_t g, uint8_t b) {
  return config::kDisplayRgbOrder ? tft.color565(b, g, r)
                                  : tft.color565(r, g, b);
}
