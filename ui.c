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

void Region(int* x, int* y, int* w, int* h)
{
	assert((uistate.n_regions > 0) && "no region");
	struct rect* cr = &uistate.regions[uistate.n_regions - 1];
	if (x) *x = cr->o.x;
	if (y) *y = cr->o.y;
	if (w) *w = cr->r.x;
	if (h) *h = cr->r.y;
}

void Enter(int x, int y, int w, int h, int flags)
{
	const int n = uistate.n_regions;
	assert(n >= 0);
	assert((n < MAX_REGION_STACK_SIZE) && "too many regions");

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }

	uistate.flags[uistate.n_regions] = flags;
	struct rect* nr = &uistate.regions[uistate.n_regions];
	if (n == 0) {
		nr->o.x = x;
		nr->o.y = y;
		nr->r.x = w;
		nr->r.y = h;
	} else if (n > 0) {
		struct rect* cr = &uistate.regions[uistate.n_regions-1];
		//if (x+w > w0) w = w0-x;
		//if (y+h > h0) h = h0-y;
		nr->o.x = cr->o.x + x;
		nr->o.y = cr->o.y + y;
		nr->r.x = w;
		nr->r.y = h;
	} 
	uistate.n_regions++;

	if (n == 0 || (flags & CLIP)) {
	}

	#if 0
	struct r* r = &rstate;
	assert(r->region_stack_size < MAX_REGION_STACK_SIZE);
	int x0,y0,w0,h0;
	get_region_xywh(&x0,&y0,&w0,&h0);
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x+w > w0) w = w0-x;
	if (y+h > h0) h = h0-y;
	struct region* rg = &r->region_stack[r->region_stack_size++];
	rg->x = x0+x;
	rg->y = y0+y;
	rg->w = w;
	rg->h = h;
	#endif
}

void Leave()
{
	assert((uistate.n_regions > 0) && "no region");
	uistate.n_regions--;
	for (int i = uistate.n_regions-1; i >= 0; i--) {
		if (i == 0 || (uistate.flags[i] & CLIP)) {
			struct rect* rr = &uistate.regions[i];
			r_clip(rr->o.x, rr->o.y, rr->r.x, rr->r.y);
			break;
		}
	}
}

#if 0
struct region {
	int x,y,w,h;
};

static inline int region_intersect(struct region a, struct region b)
{
	return     ( a.x     < b.x+b.w )
		&& ( a.x+a.w > b.x     )
		&& ( a.y     < b.y+b.h )
		&& ( a.y+a.h > b.y     )
		;
}

int region_stack_size;
struct region region_stack[MAX_REGION_STACK_SIZE];

static inline struct region get_region()
{
	struct r* r = &rstate;
	struct region rg;
	if (r->region_stack_size > 0) {
		rg = r->region_stack[r->region_stack_size-1];
	} else {
		rg.x = 0;
		rg.y = 0;
		rg.w = r->width;
		rg.h = r->height;
	}
	return rg;
}

static inline void get_region_xywh(int* x, int* y, int* w, int* h)
{
	struct region rg = get_region();
	if (x) *x = rg.x;
	if (y) *y = rg.y;
	if (w) *w = rg.w;
	if (h) *h = rg.h;
}

static inline void get_origin_xy(int* x, int* y)
{
	get_region_xywh(x,y,NULL,NULL);
}

static inline union v2 get_origin_v2()
{
	int x,y;
	get_origin_xy(&x,&y);
	return v2(x,y);
}

void r_enter(int x, int y, int w, int h)
{
	struct r* r = &rstate;
	assert(r->region_stack_size < MAX_REGION_STACK_SIZE);
	int x0,y0,w0,h0;
	get_region_xywh(&x0,&y0,&w0,&h0);
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x+w > w0) w = w0-x;
	if (y+h > h0) h = h0-y;
	struct region* rg = &r->region_stack[r->region_stack_size++];
	rg->x = x0+x;
	rg->y = y0+y;
	rg->w = w;
	rg->h = h;
}

void r_leave(void)
{
	struct r* r = &rstate;
	assert(r->region_stack_size > 0);
	r->region_stack_size--;
}

void r_scissor(void)
{
	struct r* r = &rstate;
	assert((r->mode == 0) && "scissor must be activated outside of r_begin()/r_end()");
	r->scissor = 1;
	get_region_xywh(&r->scissor_x, &r->scissor_y, &r->scissor_width, &r->scissor_height);
}

void r_no_scissor(void)
{
	struct r* r = &rstate;
	assert((r->mode == 0) && "scissor must be deactivated outside of r_begin()/r_end()");
	r->scissor = 0;
}

void r_enter_scissor(int x, int y, int w, int h)
{
	r_enter(x,y,w,h);
	r_scissor();
}

void r_leave_scissor(void)
{
	r_no_scissor();
	r_leave();
}
#endif

