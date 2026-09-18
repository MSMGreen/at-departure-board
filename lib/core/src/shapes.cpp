#include "shapes.h"

#include <math.h>

// static_cast<int> truncates toward zero, as Python's int() does.
int hill(int dx) { return static_cast<int>(7 + 5 * sin(dx / 26.0) + 3 * sin(dx / 9.0)); }

int shore(int dx) { return static_cast<int>(4 + 3 * sin(dx / 18.0)); }
