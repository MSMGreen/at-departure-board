#pragma once
#include <stdint.h>

#include "model.h"

// DEMO_MODE (spec section 9): the canonical scenes from tools/board/scenes.py,
// played in real time so every state - including 1am-empty and cancelled -
// can be seen on the glass without an API key or waiting for reality.

struct SceneDeparture {
  int32_t eta_s;
  bool live;
  bool cancelled;
};

struct SceneWatch {
  const char* badge;
  const char* headsign;
  Kind kind;
  const char* route_color;  // may be null
  uint8_t n_deps;
  SceneDeparture deps[MAX_DEPARTURES];
};

struct Scene {
  const char* name;
  const char* clock;
  int32_t stale_s;
  bool dimmed;
  uint8_t n_watches;
  SceneWatch watches[MAX_WATCHES];
};

constexpr uint32_t DEMO_SCENE_MS = 20000;

int demo_scene_count();
const Scene& demo_scene(int i);

// A scene `elapsed_s` seconds in: every ETA reduced by that much, departures
// that have left dropped so the next is promoted.
Board board_from_scene(const Scene& s, uint8_t theme, int32_t elapsed_s);

// What DEMO_MODE shows `ms` after boot. Scenes in order, DEMO_SCENE_MS each;
// once all have played, the next theme.
Board demo_board(uint32_t ms);
