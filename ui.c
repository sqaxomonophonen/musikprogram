#include <assert.h>

#include "ui.h"
#include "r.h"
#include "common.h"

#define MAX_REGION_STACK_SIZE (256)

struct uistate {
	int n_regions;
	struct rect regions[MAX_REGION_STACK_SIZE];
	int flags[MAX_REGION_STACK_SIZE];
} uistate;

void ui_region(int* x, int* y, int* w, int* h)
{
	assert((uistate.n_regions > 0) && "no region");
	struct rect* cr = &uistate.regions[uistate.n_regions - 1];
	if (x) *x = cr->x;
	if (y) *y = cr->y;
	if (w) *w = cr->w;
	if (h) *h = cr->h;
}

void ui_enter(int x, int y, int w, int h, int flags)
{
	const int n = uistate.n_regions;
	assert(n >= 0);
	assert((n < MAX_REGION_STACK_SIZE) && "too many regions");

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }

	uistate.flags[uistate.n_regions] = flags;
	struct rect* nr = &uistate.regions[uistate.n_regions];
	if (n == 0) {
		*nr = rect(x,y,w,h);
	} else if (n > 0) {
		struct rect* cr = &uistate.regions[uistate.n_regions-1];
		const float nx = cr->x + x;
		const float ny = cr->y + y;
		const float nw = x+w > cr->w ? cr->w - x : w;
		const float nh = y+h > cr->h ? cr->h - y : h;
		*nr = rect(nx,ny,nw,nh);
	}
	uistate.n_regions++;

	const struct rect* cr = &uistate.regions[uistate.n_regions-1];
	r_offset(cr->x, cr->y);
	if (n == 0 || (flags & CLIP)) {
		r_clip(cr->x, cr->y, cr->w, cr->h);
	}
}

void ui_leave()
{
	assert((uistate.n_regions > 0) && "no region");
	uistate.n_regions--;

	if (uistate.n_regions > 0) {
		const struct rect* cr = &uistate.regions[uistate.n_regions-1];
		r_offset(cr->x, cr->y);
	}

	for (int i = uistate.n_regions-1; i >= 0; i--) {
		if (i == 0 || (uistate.flags[i] & CLIP)) {
			struct rect* rr = &uistate.regions[i];
			r_clip(rr->x, rr->y, rr->w, rr->h);
			break;
		}
	}
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
