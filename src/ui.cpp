#include "ui.h"

#include <stdio.h>

#include "layout.h"
#include "painter.h"
#include "theme.h"

namespace {

// Fonts chosen to sit where render.py's TrueType text sits. These and
// MINUTES_DIGIT_H are the only numbers here expected to need tuning by eye.
const Font FONT_SMALL = {nullptr, 1};               // GLCD 6x8
const Font FONT_BADGE = {nullptr, 2};               // 16 px proportional
const Font FONT_MINUTES = {&FreeMonoBold12pt7b, 0};
constexpr int MINUTES_DIGIT_H = 15;                 // for the cancelled strike

// render.py hardcodes the location too; config owns it in the app plan.
const char* const LOCATION = "Kingsland";

void status_bar(Painter& p, const Board& b, const Theme& th) {
  p.rect(0, 0, W, STATUS_H, th.colours[C_PANEL]);
  const int mid = STATUS_H / 2;
  p.text(LOCATION, 6, mid, ML_DATUM, FONT_SMALL, th.colours[C_DIM]);
  p.text(b.clock, W - 6, mid, MR_DATUM, FONT_SMALL, th.colours[C_TEXT]);

  char label[16];
  Rgb dot;
  if (b.is_stale()) {
    snprintf(label, sizeof label, "stale %ldm", static_cast<long>(b.stale_s / 60));
    dot = th.colours[C_WARN];
  } else {
    snprintf(label, sizeof label, "live");
    dot = th.colours[C_LIVE];
  }
  p.text(label, W - 54, mid, MR_DATUM, FONT_SMALL, th.colours[C_DIM]);
  const int dx = W - 46;
  p.ellipse(dx - 3, mid - 3, dx + 3, mid + 3, dot);
}

void times(Painter& p, const Lane& ln, const Watch& w, const Theme& th) {
  const Rgb dim = th.colours[C_DIM];
  const Departure* nxt = w.next();
  if (nxt == nullptr) {
    p.text("--", ln.minutes_x, ln.minutes_y, TR_DATUM, FONT_MINUTES, dim);
    p.text("none tonight", ln.following_x, ln.following_y, TR_DATUM, FONT_SMALL, dim);
    return;
  }

  char buf[16];
  snprintf(buf, sizeof buf, "%d", display_minutes(nxt->eta_s));
  p.text(buf, ln.minutes_x, ln.minutes_y, TR_DATUM, FONT_MINUTES,
         nxt->cancelled ? dim : th.colours[C_TEXT]);

  if (nxt->cancelled) {
    const int tw = p.text_width(buf, FONT_MINUTES);
    const int y = ln.minutes_y + MINUTES_DIGIT_H / 2;
    p.rect(ln.minutes_x - tw - 1, y, ln.minutes_x + 1, y + 1, th.colours[C_WARN]);
    // A 2px strike is invisible from across the room, which is the distance
    // this board is read from. The word is what carries it.
    p.text("cancelled", ln.following_x, ln.following_y, TR_DATUM, FONT_SMALL, th.colours[C_WARN]);
    return;
  }

  const Departure* f = w.following();
  if (f != nullptr) snprintf(buf, sizeof buf, "then %d", display_minutes(f->eta_s));
  else snprintf(buf, sizeof buf, "then --");
  p.text(buf, ln.following_x, ln.following_y, TR_DATUM, FONT_SMALL, dim);
}

void draw_lane(Painter& p, const Lane& ln, const Watch& w, int index, const Theme& th) {
  const Rgb card = th.colours[index % 2 == 0 ? C_PANEL : C_PANEL_HI];
  p.rrect(ln.rect.x0 + MARGIN, ln.rect.y0 + 3, ln.rect.x1 - MARGIN, ln.rect.y1 - 3, 5, card);

  const Rgb colour = badge_colour(th, w);

  // route badge
  const int bw = p.text_width(w.badge, FONT_BADGE) + 14;
  const Rect& b = ln.badge;
  p.rrect(b.x0, b.y0, b.x0 + bw, b.y1, 4, colour);
  p.text(w.badge, (b.x0 + b.x0 + bw) / 2, (b.y0 + b.y1) / 2, MC_DATUM, FONT_BADGE,
         th.colours[C_DARK]);
  p.text(w.headsign, b.x0 + bw + 8, ln.headsign_y, TL_DATUM, FONT_SMALL, th.colours[C_DIM]);

  times(p, ln, w, th);

  // track + stop marker
  p.rect(ln.track.x0, ln.track.y0, ln.track.x1, ln.track.y1,
         th.colours[w.kind == Kind::Bus ? C_ROAD : C_RAIL]);
  p.rect(ln.marker_x, ln.track.y0 - 12, ln.marker_x + 2, ln.track.y1, th.colours[C_DIM]);
  p.ellipse(ln.marker_x - 3, ln.track.y0 - 17, ln.marker_x + 5, ln.track.y0 - 9, colour);
}

}  // namespace

Ui::Ui(TFT_eSPI& tft) : band_(&tft) {}

bool Ui::begin() {
  band_.setColorDepth(16);
  return band_.createSprite(W, BAND_H) != nullptr;
}

void Ui::draw(const Board& b, float t) {
  (void)t;  // animation arrives with the vehicles
  const Theme& th = theme(b.theme);
  const int n = b.n_watches;
  for (int oy = 0; oy < H; oy += BAND_H) {
    Painter p{band_, oy, b.dimmed};
    band_.fillSprite(p.c(th.colours[C_BG]));
    if (oy <= STATUS_H) status_bar(p, b, th);
    for (int i = 0; i < n; i++) {
      const Lane ln = lane(i, n);
      if (ln.rect.y1 < oy || ln.rect.y0 >= oy + BAND_H) continue;  // not in this band
      draw_lane(p, ln, b.watches[i], i, th);
    }
    band_.pushSprite(0, oy);
  }
}
