#include "demo.h"

#include <string.h>

#include "scenes_data.h"
#include "theme.h"

int demo_scene_count() { return SCENE_DATA_COUNT; }

const Scene& demo_scene(int i) { return SCENE_DATA[i]; }

Board board_from_scene(const Scene& s, uint8_t theme_index, int32_t elapsed_s) {
  Board b;
  memset(&b, 0, sizeof b);
  b.n_watches = s.n_watches;
  strncpy(b.clock, s.clock, sizeof b.clock - 1);
  b.stale_s = s.stale_s;
  b.dimmed = s.dimmed;
  b.theme = theme_index;
  board_set_location(&b, s.location);
  for (int i = 0; i < s.n_watches; i++) {
    const SceneWatch& sw = s.watches[i];
    Watch& w = b.watches[i];
    watch_init(&w, sw.badge, sw.headsign, sw.kind, sw.route_color);
    watch_set_message(&w, sw.message);
    for (int j = 0; j < sw.n_deps; j++) {
      const int32_t eta = sw.deps[j].eta_s - elapsed_s;
      if (eta < 0) continue;  // it has left; the next one is promoted
      w.add({eta, sw.deps[j].live, sw.deps[j].cancelled});
    }
    w.sort_departures();
  }
  return b;
}

Board demo_board(uint32_t ms) {
  const uint32_t scene = (ms / DEMO_SCENE_MS) % SCENE_DATA_COUNT;
  const uint32_t th = (ms / (DEMO_SCENE_MS * SCENE_DATA_COUNT)) % theme_count();
  const int32_t elapsed_s = static_cast<int32_t>((ms % DEMO_SCENE_MS) / 1000);
  return board_from_scene(SCENE_DATA[scene], static_cast<uint8_t>(th), elapsed_s);
}
