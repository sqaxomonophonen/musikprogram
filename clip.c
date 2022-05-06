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

#define VS_CLIP_INNER(ORD) \
	int nout = 0; \
	union v2 out[7]; \
	int clipped = 0; \
	int i0 = *n-1; \
	for (int i1 = 0; i1 < *n; i1++) { \
		union v2 p0 = vs[i0]; \
		union v2 p1 = vs[i1]; \
		const int p0_inside = (ORD-p0.ORD) * n##ORD >= 0.0f; \
		const int p1_inside = (ORD-p1.ORD) * n##ORD >= 0.0f; \
		union v2 cutp = v2_lerp((ORD-p0.ORD) / (p1.ORD-p0.ORD), p0, p1); \
		if (p1_inside) { \
			if (!p0_inside) { \
				out[nout++] = cutp; \
				clipped = 1; \
			} \
			out[nout++] = p1; \
		} else if (p0_inside) { \
			out[nout++] = cutp; \
			clipped = 1; \
		} \
	} \
	*n = nout; \
	memcpy(vs, out, sizeof(*vs)*nout); \
	return clipped;

static int vs_xclip(int* n, union v2* vs, float x, float nx)
{
	VS_CLIP_INNER(x)
}

static int vs_yclip(int* n, union v2* vs, float y, float ny)
{
	VS_CLIP_INNER(y)
}

#undef VS_CLIP_INNER

void clip_triangle(struct clip* clip, struct rect* clip_rect, union v2* p0, union v2* p1, union v2* p2)
{
	memset(clip, 0, sizeof *clip);

	int n = 3;
	union v2 vs[ARRAY_LENGTH(clip->vs)];

	vs[0] = *p0;
	vs[1] = *p1;
	vs[2] = *p2;

	float cx0, cy0, cx1, cy1;
	rect_expand(clip_rect, &cx0, &cy0, &cx1, &cy1);

	int clipped = 0;
	clipped |= vs_xclip(&n, vs, cx0,  1.0f);
	clipped |= vs_xclip(&n, vs, cx1, -1.0f);
	clipped |= vs_yclip(&n, vs, cy0,  1.0f);
	clipped |= vs_yclip(&n, vs, cy1, -1.0f);

	if (n == 0) {
		clip->result = CLIP_ALL;
	} else {
		if (clipped) {
			assert(n >= 3);
			clip->result = CLIP_SOME;
		} else {
			assert(n == 3);
			clip->result = CLIP_NONE;
		}
	}

	// TODO update barycentric UVs

	assert(!"TODO");
	// TODO https://en.wikipedia.org/wiki/Sutherland%E2%80%93Hodgman_algorithm
	// TODO https://codeplea.com/triangular-interpolation
}
