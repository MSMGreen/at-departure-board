#pragma once
#include <TFT_eSPI.h>

#include "model.h"

// Draws a Board. A 320x240 16-bit frame needs 153,600 contiguous bytes and the
// largest free block on this chip is ~114 KB (docs/hardware-notes.md), so the
// frame is built one band at a time in a single reused sprite and pushed band
// by band. Everything is redrawn every frame; the scenery RNG makes that safe.
constexpr int BAND_H = 48;  // 320 x 48 x 2 = 30,720 bytes; 5 bands = 240 rows

class Ui {
 public:
  explicit Ui(TFT_eSPI& tft);
  bool begin();  // false if the band sprite could not be allocated
  void draw(const Board& board, uint32_t ms);

 private:
  TFT_eSprite band_;
};
