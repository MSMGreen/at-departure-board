#include "model.h"

#include <string.h>

namespace {

void copy_text(char* dst, size_t size, const char* src) {
  if (src == nullptr) src = "";
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

}  // namespace

bool Watch::add(Departure d) {
  if (n_deps >= MAX_DEPARTURES) return false;
  deps[n_deps++] = d;
  return true;
}

void Watch::sort_departures() {
  // Insertion sort: stable, and n is at most four.
  for (int i = 1; i < n_deps; i++) {
    const Departure d = deps[i];
    int j = i - 1;
    while (j >= 0 && deps[j].eta_s > d.eta_s) {
      deps[j + 1] = deps[j];
      j--;
    }
    deps[j + 1] = d;
  }
}

void watch_init(Watch* w, const char* badge, const char* headsign, Kind kind,
                const char* route_color_hex) {
  memset(w, 0, sizeof *w);
  copy_text(w->badge, sizeof w->badge, badge);
  copy_text(w->headsign, sizeof w->headsign, headsign);
  w->kind = kind;
  w->has_route_color = parse_hex(route_color_hex, &w->route_color);
}

void watch_set_message(Watch* w, const char* message) {
  copy_text(w->message, sizeof w->message, message);
}

void board_set_location(Board* b, const char* location) {
  copy_text(b->location, sizeof b->location, location);
}
