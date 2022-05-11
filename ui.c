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
} uistate;

void ui_begin(struct ui_window* uw)
{
	struct uistate* u = &uistate;
	assert((u->uw == NULL) && "already inside ui_begin()");
	u->uw = uw;
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
	for (int i = 0; i < GPUDL_BUTTON_END; i++) {
		uw->mbtn[i].clicked = 0;
	}
	u->uw = NULL;
}

static struct rect* get_region()
{
	struct uistate* u = &uistate;
	assert((u->n_regions > 0) && "no region");
	return &u->regions[u->n_regions - 1];
}

void ui_region(int* x, int* y, int* w, int* h)
{
	struct rect* cr = get_region();
	if (x) *x = cr->x;
	if (y) *y = cr->y;
	if (w) *w = cr->w;
	if (h) *h = cr->h;
}

void ui_enter(int x, int y, int w, int h, int flags)
{
	struct uistate* u = &uistate;
	const int n = u->n_regions;
	assert(n >= 0);
	assert((n < MAX_REGION_STACK_SIZE) && "too many regions");

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }

	u->flags[u->n_regions] = flags;
	struct rect* nr = &u->regions[u->n_regions];
	if (n == 0) {
		*nr = rect(x,y,w,h);
	} else if (n > 0) {
		struct rect* cr = &u->regions[u->n_regions-1];
		const float nx = cr->x + x;
		const float ny = cr->y + y;
		const float nw = x+w > cr->w ? cr->w - x : w;
		const float nh = y+h > cr->h ? cr->h - y : h;
		*nr = rect(nx,ny,nw,nh);
	}
	u->n_regions++;

	const struct rect* cr = &u->regions[u->n_regions-1];
	r_offset(cr->x, cr->y);
	if (n == 0 || (flags & CLIP)) {
		r_clip(cr->x, cr->y, cr->w, cr->h);
	}
}

void ui_leave()
{
	struct uistate* u = &uistate;
	assert((u->n_regions > 0) && "no region");
	u->n_regions--;

	if (u->n_regions > 0) {
		const struct rect* cr = &u->regions[u->n_regions-1];
		r_offset(cr->x, cr->y);
	}

	for (int i = u->n_regions-1; i >= 0; i--) {
		if (i == 0 || (u->flags[i] & CLIP)) {
			struct rect* rr = &u->regions[i];
			r_clip(rr->x, rr->y, rr->w, rr->h);
			break;
		}
	}
}

union v2 ui_mpos()
{
	return v2_sub(get_uw()->mpos, rect_origin(get_region()));
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
