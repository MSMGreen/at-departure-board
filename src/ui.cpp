#include "ui.h"

#include <math.h>
#include <stdio.h>

#include "layout.h"
#include "painter.h"
#include "rng.h"
#include "shapes.h"
#include "sprite_table.h"
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

// Port of sprites.blit. Origin is the BOTTOM-left: vehicles sit on the track
// line, and that is the edge that must stay put. Runs of one role become one
// horizontal line; role 0 is transparent.
void blit(Painter& p, const SpriteRef& sp, int x, int y, const Rgb roles[ROLE_SLOTS]) {
  const int top = y - sp.h;
  for (int ry = 0; ry < sp.h; ry++) {
    const int py = top + ry;
    if (py < p.oy || py >= p.oy + BAND_H) continue;
    int run_start = 0;
    uint8_t run_role = sprite_role(sp, 0, ry);
    for (int rx = 1; rx <= sp.w; rx++) {
      const uint8_t role = rx < sp.w ? sprite_role(sp, rx, ry) : 0xFF;
      if (role == run_role) continue;
      if (run_role != 0) p.hline(x + run_start, x + rx - 1, py, roles[run_role]);
      run_start = rx;
      run_role = role;
    }
  }
}

// Port of scenery.transit: low skyline for the road, overhead wires for rail.
// Ink derives from the card colour because lanes alternate panel shades.
void scenery_transit(Painter& p, int x0, int y0, int x1, int y1, Kind kind, uint32_t seed,
                     Rgb card) {
  Rng rnd(seed);
  const Rgb ink = bright(card, 1.0, 22);
  const Rgb lit = bright(card, 1.3, 52);
  const int base = y1 - 14;
  if (kind == Kind::Bus) {
    int x = x0 + 6;
    while (x < x1 - 6) {
      const int bw = rnd.between(9, 20);
      const int bh = rnd.between(6, 18);
      p.rect(x, base - bh, x + bw, base, ink);
      for (int wy = base - bh + 3; wy < base - 2; wy += 5)
        for (int wx = x + 2; wx < x + bw - 2; wx += 5)
          if (rnd.chance(35)) p.point(wx, wy, lit);
      x += bw + rnd.between(2, 6);
    }
  } else {
    p.hline(x0 + 6, x1 - 6, y0 + 30, ink);
    for (int mx = x0 + 20; mx < x1 - 10; mx += 46) p.vline(mx, y0 + 30, base, ink);
  }
}

// Port of scenery.ghibli: stars over a hill for the road, over water for rail.
// Draw order matters for the RNG: x, then y, then the star colour.
void scenery_ghibli(Painter& p, int x0, int y0, int x1, int y1, Kind kind, uint32_t seed,
                    Rgb card) {
  Rng rnd(seed);
  const Rgb stars[3] = {bright(card, 1.2, 58), bright(card, 1.4, 92), bright(card, 1.6, 140)};
  for (int i = 0; i < 26; i++) {
    const int sx = rnd.between(x0 + 6, x1 - 6);
    const int sy = rnd.between(y0 + 26, y1 - 34);
    p.point(sx, sy, stars[rnd.below(3)]);
  }
  const Rgb land = shade(card, 0.72);  // hills sit BELOW the ground
  const int base = y1 - 14;
  if (kind == Kind::Bus) {
    for (int x = x0 + 6; x < x1 - 6; x++) p.vline(x, base - hill_at(x - x0), base, land);
  } else {
    for (int x = x0 + 6; x < x1 - 6; x++) p.vline(x, base - shore_at(x - x0), base, land);
    for (int k = 0; k < 6; k++)  // moon path on the water
      p.hline(x0 + 40 + k * 30, x0 + 52 + k * 30, base - 2, bright(card, 1.0, 26));
  }
}

void scenery(Painter& p, SceneryKind kind, int x0, int y0, int x1, int y1, Kind vehicle,
             uint32_t seed, Rgb card) {
  if (kind == SCENERY_TRANSIT) scenery_transit(p, x0, y0, x1, y1, vehicle, seed, card);
  else if (kind == SCENERY_GHIBLI) scenery_ghibli(p, x0, y0, x1, y1, vehicle, seed, card);
}

void vehicle(Painter& p, const Lane& ln, const Watch& w, float t, int index, const Theme& th,
             const SpriteRef& sp, Rgb colour) {
  Rgb roles[ROLE_SLOTS];
  const Departure* nxt = w.next();
  if (nxt == nullptr) {
    // Parked at the left, lights off.
    role_colours(th, shade(colour, 0.4), roles);
    blit(p, sp, ln.track.x0, ln.sprite_baseline, roles);
    return;
  }
  const int x = vehicle_x(nxt->eta_s, ln, sp.w);
  const float bob = nxt->eta_s > 30 ? sinf(t * 5 + index * 1.7f) : 0.0f;
  role_colours(th, nxt->cancelled ? shade(colour, 0.4) : colour, roles);
  // The cast truncates toward zero, as sprites.blit's int(y) does.
  blit(p, sp, x, static_cast<int>(ln.sprite_baseline + bob), roles);
}

void draw_lane(Painter& p, const Lane& ln, const Watch& w, float t, int index, const Theme& th,
               uint8_t theme_index, SizeClass size) {
  const Rgb card = th.colours[index % 2 == 0 ? C_PANEL : C_PANEL_HI];
  p.rrect(ln.rect.x0 + MARGIN, ln.rect.y0 + 3, ln.rect.x1 - MARGIN, ln.rect.y1 - 3, 5, card);

  if (size == SizeClass::Large && th.scenery != SCENERY_NONE)
    scenery(p, th.scenery, ln.rect.x0 + 8, ln.rect.y0, ln.rect.x1 - 8, ln.rect.y1, w.kind,
            7 + index, card);

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

  vehicle(p, ln, w, t, index, th, sprite_for(theme_index, size, w.kind), colour);
}

}  // namespace

Ui::Ui(TFT_eSPI& tft) : band_(&tft) {}

bool Ui::begin() {
  band_.setColorDepth(16);
  return band_.createSprite(W, BAND_H) != nullptr;
}

void Ui::draw(const Board& b, float t) {
  const Theme& th = theme(b.theme);
  const int n = b.n_watches;
  const SizeClass size = size_class(n);
  for (int oy = 0; oy < H; oy += BAND_H) {
    Painter p{band_, oy, b.dimmed};
    band_.fillSprite(p.c(th.colours[C_BG]));
    if (oy <= STATUS_H) status_bar(p, b, th);
    for (int i = 0; i < n; i++) {
      const Lane ln = lane(i, n);
      if (ln.rect.y1 < oy || ln.rect.y0 >= oy + BAND_H) continue;  // not in this band
      draw_lane(p, ln, b.watches[i], t, i, th, b.theme, size);
    }
    band_.pushSprite(0, oy);
  }
}
