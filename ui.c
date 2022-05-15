#include <assert.h>

#include "ui.h"
#include "r.h"
#include "common.h"

#define MAX_REGION_STACK_SIZE (256)

struct uistate {
	int n_regions;
	struct rect regions[MAX_REGION_STACK_SIZE];
	int flags[MAX_REGION_STACK_SIZE];
	struct ui_window* uw;

	// derived
	union v2 current_offset;
	struct rect current_region;
	int current_flags;
} uistate;

void ui_begin(struct ui_window* uw)
{
	struct uistate* u = &uistate;
	assert((u->uw == NULL) && "already inside ui_begin()");
	u->uw = uw;
	uw->codepoint_cursor = 0;
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
	for (int i = 0; i < GK_SPECIAL_END; i++) uw->key[i].pressed = 0;
	uw->n_codepoints = 0;
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

void ui_dim(int* w, int* h)
{
	struct uistate* u = &uistate;
	const int n = u->n_regions;
	assert((n > 0) && "no region");
	if (w) *w = u->regions[n-1].w;
	if (h) *h = u->regions[n-1].h;
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
		struct rect abs_region = u->regions[0];
		struct rect clip_rect = abs_region;
		union v2 abs_offset = v2(abs_region.x, abs_region.y);
		int flags = u->flags[0];
		int sticky_flags = 0;
		for (int i = 1; i < n_regions; i++) {
			struct rect r = u->regions[i];
			abs_offset = v2_add(abs_offset, v2(r.x, r.y));
			struct rect r2 = rect(abs_offset.x, abs_offset.y, r.w, r.h);
			rect_intersection(&abs_region, &abs_region, &r2);
			flags = u->flags[i];
			if (flags & CLIP) rect_intersection(&clip_rect, &clip_rect, &r2);
			if (flags & NO_INPUT) sticky_flags |= NO_INPUT;
		}
		r_offset(abs_offset.x, abs_offset.y);
		r_clip(clip_rect.x, clip_rect.y, clip_rect.w, clip_rect.h);
		u->current_offset = abs_offset;
		u->current_region = abs_region;
		u->current_flags = flags | sticky_flags;
		ui_pan(0,0);
	}
}

void ui_enter(int x, int y, int w, int h, int flags)
{
	struct uistate* u = &uistate;
	const int n = u->n_regions;
	assert(n >= 0);
	assert((n < MAX_REGION_STACK_SIZE) && "too many regions");

	u->flags[u->n_regions] = flags;
	u->regions[u->n_regions] = rect(x,y,w,h);
	u->n_regions++;

	region_refresh();
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
	// TODO proper focus?
	return 1;
}

int ui_keyseq(struct ui_keyseq* keyseq)
{
	if (!has_keyboard_focus()) return 0;
	if (keyseq->n == 0) return 0;
	struct ui_window* uw = get_uw();
	int last_serial = 0;
	for (int i = 0; i < keyseq->n; i++) {
		const int code = keyseq->code[i];
		assert(0 <= code && code < GK_SPECIAL_END);
		struct ui_key* key = &uw->key[code];
		if (key->down_serial <= last_serial) return 0;
		last_serial = key->down_serial;
		if (i == keyseq->n-1 && !key->pressed) return 0;
	}
	return 1;
}

int ui_key(int code)
{
	return ui_keyseq(&(struct ui_keyseq) { .n=1, .code={ code } });
}

int ui_keyseq2(int code0, int code1)
{
	return ui_keyseq(&(struct ui_keyseq) { .n=2, .code={ code0,code1 } });
}

int ui_keyseq3(int code0, int code1, int code2)
{
	return ui_keyseq(&(struct ui_keyseq) { .n=3, .code={ code0,code1,code2 } });
}

int ui_keyseq4(int code0, int code1, int code2, int code3)
{
	return ui_keyseq(&(struct ui_keyseq) { .n=4, .code={ code0,code1,code2,code3 } });
}

int ui_read()
{
	if (!has_keyboard_focus()) return 0;
	struct ui_window* uw = get_uw();
	if (0 <= uw->codepoint_cursor && uw->codepoint_cursor < uw->n_codepoints) {
		return uw->codepoints[uw->codepoint_cursor++];
	}
	return 0;
}

union v2 ui_mpos()
{
	return v2_sub(get_uw()->mpos, get_offset());
}

int ui_clicked(enum gpudl_button button)
{
	struct rect* cr = get_region();
	struct ui_window* uw = get_uw();
	struct ui_mbtn* mb = &uw->mbtn[button];
	return mb->clicked && rect_contains(cr, &mb->mpos);
}

int ui_down(enum gpudl_button button)
{
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
