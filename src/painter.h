#pragma once
#include <TFT_eSPI.h>

#include "color.h"

// Draw calls take PIL-style boxes - BOTH corners inclusive - so ui.cpp ports
// tools/board/render.py line for line. `oy` is the band's top row on screen:
// everything shifts up by it and the sprite clips whatever falls outside.
// Every colour goes through c(), which is where the dimmed state is applied,
// matching render.py dimming the finished image.

struct Font {
  const GFXfont* gfx;  // a FreeFont, or null to use the numbered font
  uint8_t num;
};

struct Painter {
  TFT_eSprite& s;
  int oy;
  bool dim;

  uint16_t c(Rgb v) const { return to565(dim ? dimmed(v) : v); }

  void rect(int x0, int y0, int x1, int y1, Rgb v) {
    s.fillRect(x0, y0 - oy, x1 - x0 + 1, y1 - y0 + 1, c(v));
  }
  void rrect(int x0, int y0, int x1, int y1, int r, Rgb v) {
    s.fillRoundRect(x0, y0 - oy, x1 - x0 + 1, y1 - y0 + 1, r, c(v));
  }
  void ellipse(int x0, int y0, int x1, int y1, Rgb v) {
    s.fillEllipse((x0 + x1) / 2, (y0 + y1) / 2 - oy, (x1 - x0) / 2, (y1 - y0) / 2, c(v));
  }
  void hline(int x0, int x1, int y, Rgb v) { s.drawFastHLine(x0, y - oy, x1 - x0 + 1, c(v)); }
  void vline(int x, int y0, int y1, Rgb v) { s.drawFastVLine(x, y0 - oy, y1 - y0 + 1, c(v)); }
  void point(int x, int y, Rgb v) { s.drawPixel(x, y - oy, c(v)); }

  void font(Font f) {
    if (f.gfx != nullptr) s.setFreeFont(f.gfx);
    else s.setTextFont(f.num);
  }
  int text_width(const char* str, Font f) {
    font(f);
    return s.textWidth(str);
  }
  void text(const char* str, int x, int y, uint8_t datum, Font f, Rgb fg) {
    font(f);
    s.setTextDatum(datum);
    s.setTextColor(c(fg));  // no background: transparent over the card
    s.drawString(str, x, y - oy);
  }
};
