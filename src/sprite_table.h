#pragma once
#include <stdint.h>

#include "layout.h"
#include "model.h"
#include "sprite_ref.h"

// theme is a Board.theme index; THEME_* in sprites.h uses the same numbering.
SpriteRef sprite_for(uint8_t theme, SizeClass size, Kind kind);
