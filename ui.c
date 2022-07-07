#include <stdio.h>
#include <assert.h>

#include "stb_ds.h"

#include "ui.h"
#include "r.h"
#include "common.h"


#define MAX_REGION_STACK_SIZE (256)

struct uistate {
	int n_regions;
	struct rect regions[MAX_REGION_STACK_SIZE];
	int flags[MAX_REGION_STACK_SIZE];
	int groups[MAX_REGION_STACK_SIZE];
	struct ui_window* uw;

	int group_serial;
	int* seen_groups;

	// derived
	union v2 current_offset;
	struct rect current_region;
	int current_flags;
	int current_group;

} uistate;

static int modifier_from_keysym(int keysym)
{
	switch (keysym) {
	#define MOD(M) \
	case GK_L##M: return UI_L##M; \
	case GK_R##M: return UI_R##M;
	UI_MODIFIERS
	#undef MOD
	default: return -1;
	}
}

#if 0
static int keycode_is_modifier(int keycode)
{
	return modifier_from_keysym(keycode) != -1;
}
#endif

void ui_begin(struct ui_window* uw)
{
	struct uistate* u = &uistate;
	assert((u->uw == NULL) && "already inside ui_begin()");
	u->uw = uw;
	uw->keypress_cursor = 0;
	arrsetlen(u->seen_groups, 0);
}

static struct ui_window* get_uw()
{
	struct uistate* u = &uistate;
	struct ui_window* uw = u->uw;
	assert((uw != NULL) && "not inside ui_begin()");
	return uw;
}

void ui_end()
{
	struct uistate* u = &uistate;
	assert((u->n_regions == 0) && "ui_end() inside ui_enter()");
	struct ui_window* uw = get_uw();
	for (int i = 0; i < GPUDL_BUTTON_END; i++) uw->mbtn[i].clicked = 0;

	uw->n_keypresses = 0;
	int lowest_group = -1;
	int next_group = -1;
	int seen = 0;
	for (int i = 0; i < arrlen(u->seen_groups); i++) {
		const int g = u->seen_groups[i];
		if (g == uw->focused_group) seen = 1;
		if (lowest_group == -1 || g < lowest_group) lowest_group = g;
		if (g > uw->focused_group) {
			if (next_group == -1 || g < next_group) next_group = g;
		}
	}
	if (!seen) uw->focused_group = 0;
	if (next_group != -1) {
		uw->next_focus_group = next_group;
	} else if (lowest_group != -1) {
		uw->next_focus_group = lowest_group;
	} else {
		uw->next_focus_group = 0;
	}

	uw->last_mpos = uw->mpos;
	u->uw = NULL;
}

static struct rect* get_region()
{
	struct uistate* u = &uistate;
	assert((u->n_regions > 0) && "no region");
	return &u->current_region;
}

static union v2 get_offset()
{
	struct uistate* u = &uistate;
	assert((u->n_regions > 0) && "no region");
	return u->current_offset;
}

static int get_flags()
{
	struct uistate* u = &uistate;
	assert((u->n_regions > 0) && "no region");
	return u->current_flags;
}

static int get_group()
{
	struct uistate* u = &uistate;
	assert((u->n_regions > 0) && "no region");
	return u->current_group;
}

void ui_dim(int* w, int* h)
{
	struct uistate* u = &uistate;
	const int n = u->n_regions;
	assert((n > 0) && "no region");
	if (w) *w = u->regions[n-1].w;
	if (h) *h = u->regions[n-1].h;
}

int ui_flags()
{
	return get_flags();
}

int ui_focused()
{
	const int group = get_group();
	return group == 0 ? 1 : (group == uistate.uw->focused_group);
}

void ui_goto_next_focus()
{
	struct uistate* u = &uistate;
	u->uw->focused_group = u->uw->next_focus_group;
}

void ui_pan(int dx, int dy)
{
	union v2 off = get_offset();
	r_offset(off.x+dx, off.y+dy);
}

static void region_refresh()
{
	struct uistate* u = &uistate;
	const int n_regions = u->n_regions;
	if (n_regions == 0) {
		r_clip(0,0,0,0);
		r_offset(0,0);
	} else {
		assert(n_regions > 0);
		union v2 abs_offset = v2(0,0);
		struct rect abs_region;
		struct rect clip_rect;
		int flags = 0;
		int sticky_flags = 0;
		int group = 0;
		for (int i = 0; i < n_regions; i++) {
			struct rect r = u->regions[i];
			abs_offset = v2_add(abs_offset, v2(r.x, r.y));
			struct rect r2 = rect(abs_offset.x, abs_offset.y, r.w, r.h);
			if (i == 0) {
				abs_region = r2;
				clip_rect = r2;
			} else {
				rect_intersection(&abs_region, &abs_region, &r2);
			}
			flags = u->flags[i];
			if (flags & CLIP) rect_intersection(&clip_rect, &clip_rect, &r2);
			if (flags & NO_INPUT) {
				sticky_flags |= NO_INPUT;
			}
			const int g = u->groups[i];
			if (g > 0) group = g;
		}
		r_offset(abs_offset.x, abs_offset.y);
		r_clip(clip_rect.x, clip_rect.y, clip_rect.w, clip_rect.h);
		u->current_offset = abs_offset;
		u->current_region = abs_region;
		u->current_flags = flags | sticky_flags;
		u->current_group = group;
		ui_pan(0,0);
	}
}

void ui_enter_group(int x, int y, int w, int h, int flags, int* group)
{
	struct uistate* u = &uistate;

	if (group && *group == 0) *group = ++u->group_serial;

	const int n = u->n_regions;
	assert(n >= 0);
	assert((n < MAX_REGION_STACK_SIZE) && "too many regions");

	u->flags[n] = flags;
	u->regions[n] = rect(x,y,w,h);
	u->groups[n] = group ? *group : 0;

	u->n_regions++;

	region_refresh();

	if (group && !(get_flags() & NO_INPUT)) {
		if (u->uw->focused_group == 0) u->uw->focused_group = *group;
		arrput(u->seen_groups, *group);
		struct rect* cr = get_region();
		if (rect_contains(cr, &u->uw->mpos) && !rect_contains(cr, &u->uw->last_mpos)) {
			u->uw->focused_group = *group;
		}
	}
}

void ui_enter(int x, int y, int w, int h, int flags)
{
	ui_enter_group(x,y,w,h,flags,NULL);
}

void ui_leave()
{
	struct uistate* u = &uistate;
	assert((u->n_regions > 0) && "no region");
	u->n_regions--;
	region_refresh();
}

static int has_keyboard_focus()
{
	const int flags = get_flags();
	if (flags & NO_INPUT) return 0;
	const int group = get_group();
	if (group == 0) return 1;
	return group == uistate.uw->focused_group;
}

static int modmaskmatch(int pressed_modifiers_mask, int match_mask)
{
	// no match if modifiers are pressed which are NOT in match mask
	if (pressed_modifiers_mask & ~match_mask) return 0;

	#define MOD(M) \
	{ \
		const int mm = match_mask & UI_##M##_MASK; \
		const int pm = pressed_modifiers_mask & UI_##M##_MASK; \
		if (mm == UI_##M##_MASK && pm == 0) return 0; \
		if (mm == UI_L##M##_MASK && pm != UI_L##M##_MASK) return 0; \
		if (mm == UI_R##M##_MASK && pm != UI_R##M##_MASK) return 0; \
	}
	UI_MODIFIERS
	#undef MOD

	return 1;
}

static int shortcut_match(struct ui_keypress k, struct ui_keypress s)
{
	if (!modmaskmatch(k.modmask, s.modmask)) return 0;
	return k.keysym == s.keysym;
}

int ui_shortcut(struct ui_keypress s)
{
	if (s.keysym == 0) return 0;
	if (!has_keyboard_focus()) return 0;
	struct ui_window* uw = get_uw();

	for (int i = uw->keypress_cursor; i < uw->n_keypresses; i++) {
		struct ui_keypress kp = uw->keypresses[i];
		if (shortcut_match(kp, s)) {
			int to_move = uw->n_keypresses - i - 1;
			if (to_move > 0) memmove(&uw->keypresses[i], &uw->keypresses[i+1], to_move * sizeof(kp));
			uw->n_keypresses--;
			return 1;
		}
	}

	return 0;
}

int ui_key(int codepoint)
{
	return ui_shortcut((struct ui_keypress) { .keysym = codepoint });
}

int ui_read_keypress(struct ui_keypress* kp)
{
	if (!has_keyboard_focus()) return 0;
	struct ui_window* uw = get_uw();
	if (uw->keypress_cursor >= uw->n_keypresses) return 0;
	memcpy(kp, &uw->keypresses[uw->keypress_cursor++], sizeof *kp);
	return 1;
}

union v2 ui_mpos()
{
	return v2_sub(get_uw()->mpos, get_offset());
}

int ui_clicked(enum gpudl_button button)
{
	if (get_flags() & NO_INPUT) return 0;
	struct rect* cr = get_region();
	struct ui_window* uw = get_uw();
	struct ui_mbtn* mb = &uw->mbtn[button];
	return mb->clicked && rect_contains(cr, &mb->mpos);
}

int ui_down(enum gpudl_button button)
{
	if (get_flags() & NO_INPUT) return 0;
	struct rect* cr = get_region();
	struct ui_window* uw = get_uw();
	struct ui_mbtn* mb = &uw->mbtn[button];
	return mb->down && rect_contains(cr, &mb->mpos);
}

#if 0
static inline int region_intersect(struct region a, struct region b)
{
	return     ( a.x     < b.x+b.w )
		&& ( a.x+a.w > b.x     )
		&& ( a.y     < b.y+b.h )
		&& ( a.y+a.h > b.y     )
		;
}
#endif

static void write_keypress(struct ui_window* uw, struct ui_keypress kp)
{
	if (uw->n_keypresses >= UI_KEYPRESSES_MAX) return;
	uw->keypresses[uw->n_keypresses++] = kp;
}

void ui_window_key_event(struct ui_window* uw, struct gpudl_event_key* ev)
{
	if (!uw) return;

	const int keysym = ev->keysym;
	const int modifier_index = modifier_from_keysym(keysym); // -1 if not a modifier
	if (0 <= modifier_index && modifier_index < 32) {
		const int mask = 1 << modifier_index;
		if (ev->pressed) {
			uw->modmask |= mask;
		} else {
			uw->modmask &= ~mask;
		}
	} else if (modifier_index == -1 && ev->pressed) {
		write_keypress(uw, (struct ui_keypress){
			.modmask = uw->modmask,
			.keysym = ev->keysym,
			.codepoint = ev->codepoint,
		});
	}
}

static int ti_get_selection(struct ui_text_input* ti, int* i0, int* i1)
{
	int d = ti->select1 - ti->select0;
	if (d > 0) {
		if (i0) *i0 = ti->select0;
		if (i1) *i1 = ti->select1;
		return d;
	} else if (d < 0) {
		if (i0) *i0 = ti->select1;
		if (i1) *i1 = ti->select0;
		return -d;
	} else {
		return 0;
	}
}

static void ti_trim(struct ui_text_input* ti)
{
	const int n0 = arrlen(ti->codepoints);

	if (ti->cursor < 0) ti->cursor = 0;
	if (ti->cursor > n0) ti->cursor = n0;

	if (ti->select0 < 0) ti->select0 = 0;
	if (ti->select0 > n0) ti->select0 = n0;
	if (ti->select1 < 0) ti->select1 = 0;
	if (ti->select1 > n0) ti->select1 = n0;
}

static void ti_move(struct ui_text_input* ti, int delta, int is_selecting)
{
	ti->cursor += delta;
	if (is_selecting) {
		ti->select1 = ti->cursor;
	} else {
		ti->select0 = ti->select1 = ti->cursor;
	}
	ti_trim(ti);
}

static int ti_delete_selection(struct ui_text_input* ti)
{
	int s0;
	int nsel = ti_get_selection(ti, &s0, NULL);
	if (nsel) {
		arrdeln(ti->codepoints, s0, nsel);
		ti->cursor = ti->select1 = ti->select0 = s0;
	}
	return nsel;
}

static void ti_delete(struct ui_text_input* ti, int is_backspace)
{
	const int nc = arrlen(ti->codepoints);

	if (ti_delete_selection(ti)) {
		// [backspace] and [delete] work the same
		return;
	}

	const int is_delete = !is_backspace;
	if (is_backspace && ti->cursor <= 0) return;
	if (is_delete && ti->cursor >= nc) return;

	if (is_backspace) ti->cursor--;
	assert(0 <= ti->cursor && ti->cursor < nc);
	arrdel(ti->codepoints, ti->cursor);
	ti_trim(ti);
}

int ui_text_input_handle(struct ui_text_input* ti, struct ui_style_text_input* style, int flags, int width)
{
	// TODO handle mouse stuff here?
	if (!has_keyboard_focus()) return 0;
	struct ui_keypress* kp;
	int n_handled = 0;
	while (ui_kpoll(&kp)) {
		n_handled++;
		if (shortcut_match(*kp, (struct ui_keypress) { .keysym = '\b' })) { // backspace
			ti_delete(ti, 1);
		} else if (shortcut_match(*kp, (struct ui_keypress) { .keysym = GK_DELETE })) {
			ti_delete(ti, 0);
		} else if (shortcut_match(*kp, (struct ui_keypress) { .keysym = GK_LEFT })) {
			ti_move(ti, -1, 0);
		} else if (shortcut_match(*kp, (struct ui_keypress) { .keysym = GK_RIGHT })) {
			ti_move(ti, 1, 0);
		} else if (shortcut_match(*kp, (struct ui_keypress) { .modmask = UI_SHIFT_MASK, .keysym = GK_LEFT })) {
			ti_move(ti, -1, 1);
		} else if (shortcut_match(*kp, (struct ui_keypress) { .modmask = UI_SHIFT_MASK, .keysym = GK_RIGHT })) {
			ti_move(ti, 1, 1);
		} else if (shortcut_match(*kp, (struct ui_keypress) { .modmask = UI_CTRL_MASK, .keysym = 'a' })) {
			const int n = arrlen(ti->codepoints);
			ti->cursor = n;
			ti->select0 = 0;
			ti->select1 = n;
		} else if (kp->codepoint >= ' ') {
			ti_delete_selection(ti);
			arrins(ti->codepoints, ti->cursor, kp->codepoint);
			ti_move(ti, 1, 0);
		} else {
			kp->keep = 1;
			n_handled--;
		}
		// other keys?
		//   tab? shift+tab? or is that a "widget manager problem"?
		//   enter? escape? do what, send a signal, hmm?
	}
	return n_handled;
}

void ui_text_input_debug(struct ui_text_input* ti)
{
	const char* NON = "\033[0m";
	const char* CUR = "\033[42m";
	const char* SEL = "\033[44m";
	printf("[");
	const int n = arrlen(ti->codepoints);
	const char* prev_style = NULL;
	int s0, s1;
	int nsel = ti_get_selection(ti, &s0, &s1);
	for (int i = 0; i <= n; i++) {
		int cp = i < n ? ti->codepoints[i] : ']';
		const char* style;
		if (i == ti->cursor) {
			style = CUR;
		} else if (nsel && s0 <= i && i < s1) {
			style = SEL;
		} else {
			style = NON;
		}
		if (style != prev_style) {
			printf("%s", style);
			prev_style = style;
		}
		printf("%c", cp); // FIXME utf8 encode
	}
	printf("%s\n", NON);
}

int ui_kpoll(struct ui_keypress** kp)
{
	if (!has_keyboard_focus()) return 0;

	struct uistate* u = &uistate;
	struct ui_window* uw = u->uw;
	if (uw == NULL) return 0;
	const int n = uw->n_keypresses;
	if (n == 0) return 0;
	const int c = uw->keypress_cursor;
	assert(c >= 0);
	if (c < n) {
		*kp = &uw->keypresses[c];
		uw->keypress_cursor++;
		return 1;
	} else {
		{ // remove keypresses not requested to be kept
			struct ui_keypress* src = uw->keypresses;
			struct ui_keypress* dst = src;
			const int n = uw->n_keypresses;
			for (int i = 0; i < n; i++) {
				if (src->keep) {
					if (src > dst) *dst = *src;
					dst->keep = 0; // clear for another round
					dst++;
				} else {
					uw->n_keypresses--;
				}
				src++;
			}
		}

		uw->keypress_cursor = 0;
		return 0;
	}
}
