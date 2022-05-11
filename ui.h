#ifndef UI_H

#include <stdlib.h>

#include "common.h"
#include "gpudl.h"

#define NO_INPUT (1<<0) // suppress input in region
#define CLIP     (1<<1) // clip graphics to region

struct ui_mbtn {
	int clicked;
	int down;
	union v2 mpos;
};

struct ui_window {
	union v2 mpos;
	struct ui_mbtn mbtn[GPUDL_BUTTON_END];
};

void ui_begin(struct ui_window* uw);
void ui_end();
void ui_region(int* x, int* y, int* w, int* h);
static inline void ui_dimensions(int* w, int* h) { ui_region(NULL, NULL, w, h); }
void ui_enter(int x, int y, int w, int h, int flags);
void ui_leave();

union v2 ui_mpos();
int ui_clicked(enum gpudl_button button);
int ui_down(enum gpudl_button button);

#define UI_H
#endif
