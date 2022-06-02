#ifndef UI_H

#include <stdlib.h>

#include "common.h"
#include "gpudl.h"
#include "r.h"

enum ui_modifier {
	UI_LSHIFT, UI_RSHIFT,
	UI_LCTRL,  UI_RCTRL,
	UI_LALT,   UI_RALT,
	UI_LSUPER, UI_RSUPER,
};

#if 0
// XXX do we need this? probably not?
enum ui_modifier_any {
	UI_SHIFT,
	UI_CTRL,
	UI_ALT,
	UI_SUPER,
};
#endif

#define NO_INPUT (1<<0) // suppress input in region
#define CLIP     (1<<1) // clip graphics to region

struct ui_mbtn {
	int clicked;
	int down;
	union v2 mpos;
};

struct ui_keypress {
	uint32_t code         :31;
	uint32_t is_codepoint : 1;
	uint32_t modmask;
};

#define UI_KEYPRESSES_MAX (32)

struct ui_window {
	union v2 mpos, last_mpos;
	struct ui_mbtn mbtn[GPUDL_BUTTON_END];

	int keymask[GK_SPECIAL_END/32+1];
	int n_keypresses;
	struct ui_keypress keypresses[UI_KEYPRESSES_MAX];
	int keypress_cursor;

	int focused_group;
	int next_focus_group;
};

struct ui_shortcut {
	uint32_t modmask;
	uint32_t keycode;
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

int ui_shortcut(struct ui_shortcut s);
int ui_key(int code);

void ui_keyclear();
int ui_read_keypress(struct ui_keypress* kp);

union v2 ui_mpos();
int ui_clicked(enum gpudl_button button);
int ui_down(enum gpudl_button button);

void ui_window_key_event(struct ui_window* uw, struct gpudl_event_key* ev);
void ui_handle_text_input(struct ui_text_input* ti, int width, int flags, struct ui_style_text_input* style);

#define UI_H
#endif
