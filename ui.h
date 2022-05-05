#ifndef UI_H

#define NO_INPUT (1<<0) // suppress input in region
#define CLIP     (1<<1) // clip graphics to region

void Enter(int x, int y, int w, int h, int flags);
void Leave();
void Region(int* x, int* y, int* w, int* h);
void Dimensions(int* w, int* h); // same as Region(NULL, NULL, w, h);

#define UI_H
#endif
