// THROWAWAY SPIKE - display bring-up, round 2: orientation.
//
// Round 1 drew a landscape image that appeared rotated 90 deg, cropped to
// 240 columns, with red/blue swapped. Both the row/column exchange and the
// colour order live in one register, MADCTL (0x36). This round cycles the
// four rotations, each labelled, framed and with an UP arrow, so we can see
// whether the panel responds to MADCTL at all and which rotation is upright.
#include <TFT_eSPI.h>

static const int PIN_BACKLIGHT = 32;
TFT_eSPI tft;

static void drawRotation(uint8_t r) {
  tft.setRotation(r);
  const int w = tft.width(), h = tft.height();
  tft.fillScreen(TFT_BLACK);

  // Frame on the outermost pixels: every edge should show a white line.
  tft.drawRect(0, 0, w, h, TFT_WHITE);
  tft.drawRect(1, 1, w - 2, h - 2, TFT_WHITE);

  // Up arrow near the top edge.
  tft.fillTriangle(w / 2, 8, w / 2 - 14, 30, w / 2 + 14, 30, TFT_YELLOW);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  char buf[32];
  snprintf(buf, sizeof buf, "ST7789 ROT %d", r);
  tft.drawString(buf, w / 2, h / 2 - 20, 4);
  snprintf(buf, sizeof buf, "%d x %d", w, h);
  tft.drawString(buf, w / 2, h / 2 + 10, 4);

  // Colour-order check: each swatch labelled with what it SHOULD be.
  const uint16_t c[] = {TFT_RED, TFT_GREEN, TFT_BLUE};
  const char* n[] = {"RED", "GREEN", "BLUE"};
  for (int i = 0; i < 3; i++) {
    int x = w / 2 - 75 + i * 50;
    tft.fillRect(x, h - 50, 46, 24, c[i]);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(n[i], x + 23, h - 22, 1);
  }
  Serial.printf("showing rotation %d (%d x %d)\n", r, w, h);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== display bring-up: orientation ===");
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);
  tft.init();
}

void loop() {
  for (uint8_t r = 0; r < 4; r++) {
    drawRotation(r);
    delay(6000);
  }
}
