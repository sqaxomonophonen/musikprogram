#ifndef UI_H

#include <stdlib.h>

#define NO_INPUT (1<<0) // suppress input in region
#define CLIP     (1<<1) // clip graphics to region

void Region(int* x, int* y, int* w, int* h);
static inline void Dimensions(int* w, int* h) { Region(NULL, NULL, w, h); }
void Enter(int x, int y, int w, int h, int flags);
void Leave();

#define UI_H
#endif
