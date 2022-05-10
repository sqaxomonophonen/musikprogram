#ifndef UI_H

#include <stdlib.h>

#define NO_INPUT (1<<0) // suppress input in region
#define CLIP     (1<<1) // clip graphics to region

void ui_region(int* x, int* y, int* w, int* h);
static inline void ui_dimensions(int* w, int* h) { ui_region(NULL, NULL, w, h); }
void ui_enter(int x, int y, int w, int h, int flags);
void ui_leave();

#define UI_H
#endif
