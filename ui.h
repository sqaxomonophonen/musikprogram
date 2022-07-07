#ifndef UI_H

#include <stdlib.h>

#include "common.h"
#include "gpudl.h"
#include "r.h"

#define UI_MODIFIERS \
	MOD(SHIFT) \
	MOD(CTRL) \
	MOD(ALT) \
	MOD(SUPER)

enum ui_modifier {
	#define MOD(M) UI_L##M, UI_R##M,
	UI_MODIFIERS
	#undef MOD
};

enum ui_modifier_mask {
	#define MOD(M) \
		UI_L##M##_MASK = 1 << (UI_L##M), \
		UI_R##M##_MASK = 1 << (UI_R##M), \
		UI_##M##_MASK = (UI_R##M##_MASK + UI_L##M##_MASK),
	UI_MODIFIERS
	#undef MOD
};

#define NO_INPUT (1<<0) // suppress input in region
#define CLIP     (1<<1) // clip graphics to region

struct ui_mbtn {
	int clicked;
	int down;
	union v2 mpos;
};

struct ui_keypress {
	uint32_t modmask:31;
	uint32_t keep:1;
	int keysym;
	int codepoint;
};

#define UI_KEYPRESSES_MAX (32)

struct ui_window {
	union v2 mpos, last_mpos;
	struct ui_mbtn mbtn[GPUDL_BUTTON_END];

	int n_keypresses;
	struct ui_keypress keypresses[UI_KEYPRESSES_MAX];
	int keypress_cursor;
	uint32_t modmask;

	int focused_group;
	int next_focus_group;
};

struct ui_text_input {
	int* codepoints;
	int cursor;
	int select0, select1;
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

int ui_shortcut(struct ui_keypress s);
int ui_key(int codepoint);

void ui_keyclear();
int ui_read_keypress(struct ui_keypress* kp);

union v2 ui_mpos();
int ui_clicked(enum gpudl_button button);
int ui_down(enum gpudl_button button);

void ui_window_key_event(struct ui_window* uw, struct gpudl_event_key* ev);
int ui_text_input_handle(struct ui_text_input* ti, struct ui_style_text_input* style, int flags, int width);
void ui_text_input_debug(struct ui_text_input* ti);
int ui_kpoll(struct ui_keypress** kp);

#define UI_H
#endif
