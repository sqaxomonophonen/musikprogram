#include <assert.h>

#include "common.h"
#include "r_tile.h"

#define CSQR(r) (v2_dot(p,p)<r)

void r_tile_raster(enum r_tile tile, int w, int h, uint8_t* bitmap, int stride)
{
	assert(0 <= tile && tile < RT_END);
	assert(w > 0 && h > 0);
	const float sx = 1.0f / (float)w;
	const float sy = 1.0f / (float)h;
	const int supersampling = 16;
	const float ssi = 1.0f / (float)supersampling;
	const int n_inside_max = supersampling*supersampling;
	const float n_inside_scale = (1.0f / (float)n_inside_max);
	uint8_t* bp = bitmap;
	int yinc = stride-w;
	switch (tile) {
	// macro magic meets macro magic
	#define DEF_TILE(N,G,W,H,X0,Y0,EXPR) \
		case RT_ ## N: { \
			const float x0step = W*sx; \
			const float y0step = H*sy; \
			const float x1step = x0step * ssi; \
			const float y1step = y0step * ssi; \
			float py0 = Y0; \
			for (int y0 = 0; y0 < h; y0++) { \
				float px0 = X0; \
				for (int x0 = 0; x0 < w; x0++) { \
					int n_inside = 0; \
					float py1 = py0; \
					for (int ssy = 0; ssy < supersampling; ssy++) { \
						float px1 = px0; \
						for (int ssx = 0; ssx < supersampling; ssx++) { \
							union v2 p = v2(px1,py1); \
							int inside = EXPR; \
							if (inside) n_inside++; \
							px1 += x1step; \
						} \
						py1 += y1step; \
					} \
					float value = (float)n_inside * n_inside_scale; \
					*(bp++) = f2u8(value); \
					px0 += x0step; \
				} \
				bp += yinc; \
				py0 += y0step; \
			} \
		} break;
	TILES
	#undef DEF_TILE
	default: assert(!"UNREACHABLE");
	}
}
