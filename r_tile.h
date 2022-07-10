#ifndef R_TILE_H

#include <stdint.h>

#define TILE_GROUPS \
	TG(boxes,  8.0,  "Rounded Boxes") \
	TG(boxes2, 10.0, "Other rounded Boxes")

enum r_tile_group {
	RTG_nil = 0,
	#define TG(GROUP,DEFAULT_SZ,DESC) RTG_ ## GROUP,
	TILE_GROUPS
	#undef TG
	RTG_END
};

#define EXT (0)

#define DEF_TILE0(N,G,W,H,EXPR) DEF_TILE(N,G,W,H,0.0,0.0,EXPR)

#define T3x3(N) RT_ ## N ## _x0y0

#define DEF_TILE3x3(N,G,W,H,EXPR) \
	DEF_TILE(N ## _x0y0, G, W,   H,   -1, -1, EXPR) \
	DEF_TILE(N ## _x1y0, G, EXT, H,    0, -1, EXPR) \
	DEF_TILE(N ## _x2y0, G, W,   H,    0, -1, EXPR) \
	DEF_TILE(N ## _x0y1, G, W,   EXT, -1,  0, EXPR) \
	DEF_TILE(N ## _x1y1, G, EXT, EXT,  0,  0, EXPR) \
	DEF_TILE(N ## _x2y1, G, W,   EXT,  0,  0, EXPR) \
	DEF_TILE(N ## _x0y2, G, W,   H,   -1,  0, EXPR) \
	DEF_TILE(N ## _x1y2, G, EXT, H,    0,  0, EXPR) \
	DEF_TILE(N ## _x2y2, G, W,   H,    0,  0, EXPR)

#define TILES \
	DEF_TILE0(one,   nil,   EXT, EXT, ((void)p,1.0)) \
	DEF_TILE3x3(box, boxes, 1.0, 1.0, (Circle(1.0) && !Circle(0.9)) || (Circle(0.7) && !Circle(0.4)))

enum r_tile {
	RT_NONE = -1,
	#define DEF_TILE(N,G,W,H,X0,Y0,EXPR) RT_ ## N,
	TILES
	#undef DEF_TILE
	RT_END
};

float r_tile_sample(enum r_tile tile, union v2 p);
void r_tile_raster(enum r_tile tile, int w, int h, uint8_t* bitmap, int stride); // r_tile_raster.c

#define R_TILE_H
#endif
