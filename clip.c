#include <string.h>

#include "clip.h"

void clip_rectangle(struct clip* clip, const struct rect* clip_rect, const struct rect* input_rect)
{
	memset(clip, 0, sizeof *clip);
	float cx0, cy0, cx1, cy1, ix0, iy0, ix1, iy1;
	rect_expand(clip_rect, &cx0, &cy0, &cx1, &cy1);
	rect_expand(input_rect, &ix0, &iy0, &ix1, &iy1);

	if (ix1 <= cx0 || iy1 <= cy0 || cx1 <= ix0 || cy1 <= iy0) {
		clip->result = CLIP_ALL;
		clip->n = 0;
		return;
	}

	clip->n = 4;
	float x0, y0, x1, y1, u0, v0, u1, v1;

	if (ix0 >= cx0 && iy0 >= cy0 && ix1 <= cx1 && iy1 <= cy1) {
		clip->result = CLIP_NONE;
		clip->n = 4;
		x0 = ix0; y0 = iy0;
		x1 = ix1; y1 = iy1;
		u0 = 0; v0 = 0;
		u1 = 1; v1 = 1;
	} else {
		clip->result = CLIP_SOME;
		const float ddx = 1.0f / (ix1-ix0);
		const float ddy = 1.0f / (iy1-iy0);
		x0 = cx0 > ix0 ? cx0 : ix0;
		y0 = cy0 > iy0 ? cy0 : iy0;
		x1 = cx1 < ix1 ? cx1 : ix1;
		y1 = cy1 < iy1 ? cy1 : iy1;
		u0 = cx0 > ix0 ? (cx0-ix0)*ddx : 0.0f;
		v0 = cy0 > iy0 ? (cy0-iy0)*ddy : 0.0f;
		u1 = cx1 < ix1 ? (cx1-ix0)*ddx : 1.0f;
		v1 = cy1 < iy1 ? (cy1-iy0)*ddy : 1.0f;
	}

	clip->vs[0].xy = v2(x0, y0);
	clip->vs[0].uv = v2(u0, v0);

	clip->vs[1].xy = v2(x1, y0);
	clip->vs[1].uv = v2(u1, v0);

	clip->vs[2].xy = v2(x1, y1);
	clip->vs[2].uv = v2(u1, v1);

	clip->vs[3].xy = v2(x0, y1);
	clip->vs[3].uv = v2(u0 ,v1);
}

#define VS_CLIP_INNER(ORD) \
	int nout = 0; \
	union v2 out[7]; \
	int clipped = 0; \
	int i0 = *n-1; \
	for (int i1 = 0; i1 < *n; i1++) { \
		union v2 p0 = vs[i0]; \
		union v2 p1 = vs[i1]; \
		const int p0_inside = (p0.ORD-ORD) * n##ORD >= 0.0f; \
		const int p1_inside = (p1.ORD-ORD) * n##ORD >= 0.0f; \
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
		assert(nout <= ARRAY_LENGTH(out)); \
		i0 = i1; \
	} \
	*n = nout; \
	memcpy(vs, out, sizeof(*vs)*nout); \
	return clipped;

static int vs_xclip(int* n, union v2* vs, const float x, const float nx)
{
	VS_CLIP_INNER(x)
}

static int vs_yclip(int* n, union v2* vs, const float y, const float ny)
{
	VS_CLIP_INNER(y)
}
#undef VS_CLIP_INNER

void clip_triangle(struct clip* clip, const struct rect* clip_rect, const union v2* p0, const union v2* p1, const union v2* p2)
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
		clip->n = n;

		for (int i = 0; i < n; i++) {
			const union v2 p = vs[i];
			clip->vs[i].xy = p;
			const float ppx = p.x - (p2->x);
			const float ppy = p.y - (p2->y);
			const float d = ((p1->y)-(p2->y)) * ((p0->x)-(p2->x)) + ((p2->x)-(p1->x)) * ((p0->y)-(p2->y));
			const float w0 = ((p1->y)-(p2->y))*ppx + ((p2->x)-(p1->x))*ppy;
			const float w1 = ((p2->y)-(p0->y))*ppx + ((p0->x)-(p2->x))*ppy;
			clip->vs[i].uv = v2(w0/d, w1/d);
		}
	}
}
