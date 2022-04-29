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

#define EXPAND3x3 (1)

#define TILES \
	T(box,  boxes,  EXPAND3x3, 1.0, 1.0, Circle(1.0)) \
	T(box2, boxes2, EXPAND3x3, 1.0, 1.0, Circle(1.0))

enum r_tile {
	#define T(TILE,GROUP,FLAGS,WIDTH,HEIGHT,EXPR) RT_ ## TILE,
	RT_END
};

#define R_TILE_H
#endif
