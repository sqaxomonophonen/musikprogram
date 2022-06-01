#ifndef UI_H

#include <stdlib.h>

#include "common.h"
#include "gpudl.h"
#include "r.h"

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

#define UI_CODEPOINTS_MAX (32)

struct ui_window {
	union v2 mpos, last_mpos;
	struct ui_mbtn mbtn[GPUDL_BUTTON_END];
	struct ui_key key[GK_SPECIAL_END];
	int serial;
	int n_codepoints;
	int codepoints[UI_CODEPOINTS_MAX];
	int codepoint_cursor;
	int focused_group;
	int next_focus_group;
};

#define UI_KEYSEQ_MAX (4)

struct ui_keyseq {
	int n;
	int code[UI_KEYSEQ_MAX];
};

struct ui_text_input {
	int* codepoints;
	int cursor;
	int select0, select1;;
};

struct ui_style_text_input {
	enum r_font font;
	int font_px;
	// TODO
};

void ui_begin(struct ui_window* uw);
void ui_end();
void ui_pan(int dx, int dy);
void ui_dim(int* w, int* h);
int ui_flags();
int ui_focused();
void ui_goto_next_focus();
void ui_enter(int x, int y, int w, int h, int flags);
void ui_enter_group(int x, int y, int w, int h, int flags, int* group);
void ui_leave();

int ui_keyseq(struct ui_keyseq* keyseq);
int ui_key(int code);
int ui_keyseq2(int code0, int code1);
int ui_keyseq3(int code0, int code1, int code2);
int ui_keyseq4(int code0, int code1, int code2, int code3);
void ui_keyclear();
int ui_read(); // next text-input codepoint or 0 when buffer is empty

union v2 ui_mpos();
int ui_clicked(enum gpudl_button button);
int ui_down(enum gpudl_button button);

void ui_window_key_event(struct ui_window* uw, struct gpudl_event_key* ev);
void ui_handle_text_input(struct ui_text_input* ti, int width, int flags, struct ui_style_text_input* style);

#define UI_H
#endif
