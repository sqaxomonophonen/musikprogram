#ifndef CLIP_H

#include "common.h"
#include "r.h"

#define CLIP_NONE (1)
#define CLIP_SOME (2)
#define CLIP_ALL  (3)

// UVs are different based on the type of clipping:
//  - When using clip_rectangle(), UVs are (0,0),(1,0),(1,1),(0,1)
//  - When using clip_triangle(), UVs are barycentric coordinates;
//    (1,0,0),(0,1,0),(0,0,1); obtain w (the last ordinate) using
//    clip_get_barycentric_coord()
struct clip_vertex {
	union v2 xy;
	union v2 uv;
};

struct clip {
	int result;
	int n;
	struct clip_vertex vs[7];
};
void clip_rectangle(struct clip* clip, struct rect* clip_rect, struct rect* input_rect);
void clip_triangle(struct clip* clip, struct rect* clip_rect, union v2* p0, union v2* p1, union v2* p2);

// only works (properly) for clip_triangle() results
static inline union v3 clip_get_barycentric_coord(struct clip* clip, int index) {
	union v2 uv = clip->vs[index].uv;
	return v3(uv.u, uv.v, 1.0f - uv.u - uv.v);
}

#define CLIP_H
#endif
