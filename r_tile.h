#ifndef R_TILE_H

#define TILE_GROUPS \
	TG(boxes,  8.0,  "Rounded Boxes") \
	TG(boxes2, 10.0, "Other rounded Boxes")

enum r_tile_group {
	#define TG(GROUP,DEFAULT_SZ,DESC) RTG_ ## GROUP,
	TILE_GROUPS
	#undef TG
	RTG_END
};

#define EXT (-1) // XXX or 0?

#define TT0(N,G,W,H,EXPR) TT(N,G,W,H,DX,DY,EXPR)

#define TT3x3(N,G,W,H,EXPR) \
	TT(N ## _x0y0, G, W,   H,   -1, -1, EXPR) \
	TT(N ## _x1y0, G, EXT, H,    0, -1, EXPR) \
	TT(N ## _x2y0, G, W,   H,    0, -1, EXPR) \
	TT(N ## _x0y1, G, W,   EXT, -1,  0, EXPR) \
	TT(N ## _x1y1, G, EXT, EXT,  0,  0, EXPR) \
	TT(N ## _x2y1, G, W,   EXT,  0,  0, EXPR) \
	TT(N ## _x0y2, G, W,   H,   -1,  0, EXPR) \
	TT(N ## _x1y2, G, EXT, H,    0,  0, EXPR) \
	TT(N ## _x2y2, G, W,   H,    0,  0, EXPR)

#define TILES \
	TT0(box,  boxes,  1.0, 1.0, Circle(1.0)) \
	TT0(box2, boxes2, 1.0, 1.0, Circle(1.0)) \
	TT3x3(boxy, boxes, 1.0, 1.0, Circle(1.0))

enum r_tile {
	#define TT(N,G,W,H,DX,DY,EXPR) RT_ ## N,
	TILES
	#undef TT
	RT_END
};

#define R_TILE_H
#endif
