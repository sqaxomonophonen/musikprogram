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

struct ui_key {
	int pressed;
	int down_serial;
};

struct ui_window {
	union v2 mpos;
	struct ui_mbtn mbtn[GPUDL_BUTTON_END];
	struct ui_key key[GK_SPECIAL_END];
	int serial;
};

#define UI_KEYSEQ_MAX (4)

struct ui_keyseq {
	int n;
	int code[UI_KEYSEQ_MAX];
};

static inline void ui_window_key_event(struct ui_window* uw, enum gpudl_keycode code, int is_press)
{
	if (!uw || code < 0 || code >= GK_SPECIAL_END) return;
	struct ui_key* key = &uw->key[code];
	if (is_press) {
		key->pressed++;
		key->down_serial = (++uw->serial);
	} else {
		key->down_serial = 0;
	}
}

void ui_begin(struct ui_window* uw);
void ui_end();
void ui_region(int* x, int* y, int* w, int* h);
void ui_pan(int dx, int dy);
static inline void ui_dimensions(int* w, int* h) { ui_region(NULL, NULL, w, h); }
void ui_enter(int x, int y, int w, int h, int flags);
void ui_leave();
int ui_keyseq(struct ui_keyseq* keyseq);
int ui_key(int code);
int ui_keyseq2(int code0, int code1);
int ui_keyseq3(int code0, int code1, int code2);
int ui_keyseq4(int code0, int code1, int code2, int code3);

union v2 ui_mpos();
int ui_clicked(enum gpudl_button button);
int ui_down(enum gpudl_button button);

#define UI_H
#endif
