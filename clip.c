#include <string.h>

#include "clip.h"

void clip_rectangle(struct clip* clip, struct rect* clip_rect, struct rect* input_rect)
{
	memset(clip, 0, sizeof *clip);
	float cx0, cy0, cx1, cy1, ix0, iy0, ix1, iy1;
	rect_expand(clip_rect, &cx0, &cy0, &cx1, &cy1);
	rect_expand(input_rect, &ix0, &iy0, &ix1, &iy1);

	if (ix1 <= cx0 || iy1 <= cy0 || cx1 <= ix0 || cy1 <= iy0) {
		clip->result = CLIP_ALL;
		clip->n = 0;
	} else if (ix0 >= cx0 && iy0 >= cy0 && ix1 <= cx1 && iy1 <= cy1) {
		clip->result = CLIP_NONE;
		clip->n = 4;

		clip->vs[0].xy = v2(ix0, iy0);
		clip->vs[0].uv = v2(0,0);

		clip->vs[1].xy = v2(ix1, iy0);
		clip->vs[1].uv = v2(1,0);

		clip->vs[2].xy = v2(ix1, iy1);
		clip->vs[2].uv = v2(1,1);

		clip->vs[3].xy = v2(ix0, iy1);
		clip->vs[3].uv = v2(0,1);
	} else {
		clip->result = CLIP_SOME;
		clip->n = 4;
		assert(!"TODO");
	}
}

void clip_triangle(struct clip* clip, struct rect* clip_rect, union v2* p0, union v2* p1, union v2* p2)
{
	memset(clip, 0, sizeof *clip);
	assert(!"TODO");
}
