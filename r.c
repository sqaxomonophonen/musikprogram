#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "stb_rect_pack.h"
#include "stb_truetype.h"
#include "sokol_time.h"

#include "embedded_resources.h"
#include "r.h"
#include "clip.h"
#include "stb_ds.h"

// couldn't convince myself that dynamic atlas resizing is worth the trouble :)
// having to flush early when the atlas overflows is bad enough, but dynamic
// resizing requires HEURISTICS AND SHIT!
#define ATLAS_WIDTH  (2048)
#define ATLAS_HEIGHT (2048)

#define VTXBUF_SZ (1<<22) // 4MB

#define STATIC_QUADS_IDXBUF_SZ ((1<<16)+(1<<15))
// The static quad index buffer contains indices like this:
//  [
//   0,1,2,0,2,3,
//   4,5,6,4,6,7,
//   ...
//   65532,65533,65534,65532,65534,65535
//  ]
// It's for drawing quads from triangles like this:
//   0---1
//   |\  |
//   | \ |   (add quad_index*4)
//   |  \|
//   3---2
// I.e. it allows me to add 4 vertices to the vertex buffer in order to draw a
// quad, and the index buffer expands each quad to 2 triangles. Since there are
// 6 indices per 4 vertices, the size must be 65536*(6/4) (to "max it out")



#define GLYPHDEF_CODE_BITS (21)
#define GLYPHDEF_SIZE_BITS (9)
#define GLYPHDEF_BANK_BITS (2)

union glyphdef {
	uint32_t u32;
	struct {
		uint32_t code : GLYPHDEF_CODE_BITS; // highest unicode codepoint is U+10FFFF / 21 bits
		uint32_t size : GLYPHDEF_SIZE_BITS;
		uint32_t bank : GLYPHDEF_BANK_BITS;
	};
};

static inline union glyphdef encode_glyphdef(int bank, int size, int code)
{
	assert(0 <= bank && bank < (1<<GLYPHDEF_BANK_BITS));
	assert(0 <= size && size < (1<<GLYPHDEF_SIZE_BITS));
	assert(0 <= code && code < (1<<GLYPHDEF_CODE_BITS));
	return (union glyphdef) {
		.bank = bank,
		.size = size,
		.code = code,
	};
}

struct ppgauss_uni {
	float width;
	float height;
	float seed;
	float sigma;
	float rstep;
	float broken;
	float intensity;
	int n0;
	int n1;
};

struct postproc {
	enum postproc_type previous_type;
	enum postproc_type type;

	float scalar;

	WGPUBuffer           gauss_unibuf;
	WGPUBindGroupLayout  gauss_bind_group_layout;
	WGPURenderPipeline   gauss_pipeline;
	WGPUSampler          gauss_sampler;
};


struct tile_vtx {
	union v2 a_pos;
	union v2 a_uv;
	union c16 a_color;
};

#define MAX_TILE_QUADS (VTXBUF_SZ / (4*sizeof(struct tile_vtx)))

struct plot_vtx {
	union v2 a_pos;
	union v2 a_uv;
	float a_threshold;
	union c16 a_color;
};

// common for R_MODE_VECTOR/R_MODE_PLOT/R_MODE_TILE
struct draw_uni {
	float width;
	float height;
	float seed;
	float scalar;
};

struct pattern_uni {
	float x0;
	float y0;
	float bxx;
	float bxy;
	float byx;
	float byy;
};

struct pattern {
	int is_free;
	WGPUBindGroup    bind_group;
	WGPUTexture      texture;
	WGPUTextureView  texture_view;
	int width;
	int height;
};

struct vector_vtx {
	union v2 a_pos;
	union c16 a_color;
};

struct tile_atlas {
	uint8_t*            bitmap;
	WGPUTexture         texture;
	WGPUTextureView     texture_view;

	stbrp_context ctx;
	stbrp_rect* rects_arr;
	stbrp_node* nodes;

	int update;
	int update_x0;
	int update_y0;
	int update_x1;
	int update_y1;
};

struct font {
	stbtt_fontinfo info;
	int ascent;
	int descent;
	int line_gap;
};

#define MAX_PATH_VERTICES (1<<16)

struct path {
	float dot_threshold;
	int prepped;
	int n;
	union v2 vs[MAX_PATH_VERTICES];
	union v2 ns[MAX_PATH_VERTICES];
	union v2 vsi[MAX_PATH_VERTICES];
};

#define MAX_BIND_GROUPS (2)

struct r {
	WGPUInstance instance;
	WGPUAdapter adapter;
	WGPUDevice device;
	WGPUQueue queue;

	int width;
	int height;

	WGPUBuffer static_quad_idxbuf;
	WGPUBuffer vtxbuf;

	WGPUBuffer          draw_unibuf;
	WGPUBuffer          pattern_unibuf;
	WGPUSampler         pattern_sampler;

	WGPUBindGroupLayout pattern_bind_group_layout;

	struct tile_atlas   tile_atlas;
	WGPUBindGroup       tile_bind_group;
	WGPURenderPipeline  tile_pipeline;
	WGPURenderPipeline  tileptn_pipeline;

	WGPUBindGroup       vector_bind_group;
	WGPURenderPipeline  vector_pipeline;

	int vtxbuf_cursor;
	uint8_t vtxbuf_data[VTXBUF_SZ];

	struct postproc postproc;
	int begun_frames;
	int begun_frame;
	int n_tile_passes;
	int n_passes;
	int mode;
	WGPUTextureView render_target_texture_view;
	int cursor0;
	int n_static_quad_indices;
	WGPUCommandEncoder encoder;
	WGPURenderPipeline pipeline;
	int n_bind_groups;
	WGPUBindGroup bind_groups[MAX_BIND_GROUPS];
	float seed;

	union c16 color[2];
	union v2  gradient_basis;

	enum r_font font;
	int font_px;
	int font_cx0;
	int font_cx;
	int font_cy;

	struct font font_monospace;
	struct font font_variable;

	struct postproc_window* current_ppw;

	union glyphdef glyphdef_requests[MAX_TILE_QUADS];
	union glyphdef missing_glyphdefs[MAX_TILE_QUADS];

	int offset_x0, offset_y0;
	int clip_x, clip_y, clip_w, clip_h;

	struct path path;

	int              is_pattern_frame;
	WGPUBindGroup    current_pattern_bind_group;
	struct pattern*  patterns;
} rstate;

static void font_init(struct font* font, void* data, int index)
{
	stbtt_InitFont(&font->info, data, stbtt_GetFontOffsetForIndex(data, index));
	stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->line_gap);
}

static WGPUShaderModule mk_shader_module(WGPUDevice device, const char* src)
{
	WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &(WGPUShaderModuleDescriptor) {
		.nextInChain = (const WGPUChainedStruct*) &(WGPUShaderModuleWGSLDescriptor) {
			.chain = { .sType = WGPUSType_ShaderModuleWGSLDescriptor },
			.code = src,
		},
	});
	if (!shader) {
		fprintf(stderr, "%s\n", src);
		abort();
	}
	return shader;
}

static WGPUTextureView postproc_begin_frame(struct postproc_window* ppw, int width, int height, WGPUTextureView swap_chain_texture_view)
{
	struct postproc* pp = &rstate.postproc;
	ppw->swap_chain_texture_view = swap_chain_texture_view;
	rstate.current_ppw = ppw;

	const int type_has_changed = pp->type != pp->previous_type;
	const int dimensions_have_changed = (ppw->width != width) || (ppw->height != height);

	const int must_reinitialize = type_has_changed || dimensions_have_changed;

	if (must_reinitialize) {
		ppw->width = width;
		ppw->height = height;

		// cleanup
		switch (pp->previous_type) {
		case PP_NONE: break;
		case PP_GAUSS: {
			wgpuBindGroupDrop(ppw->fb[0].bind_group);
			wgpuTextureViewDrop(ppw->fb[0].texture_view);
			wgpuTextureDrop(ppw->fb[0].texture);
			} break;
		}

		// create new per-window resources
		switch (pp->type) {
		case PP_NONE: break;
		case PP_GAUSS: {
			WGPUTexture texture = wgpuDeviceCreateTexture(
				rstate.device,
				&(WGPUTextureDescriptor) {
					.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
					.dimension = WGPUTextureDimension_2D,
					.size = (WGPUExtent3D){
						.width = ppw->width,
						.height = ppw->height,
						.depthOrArrayLayers = 1,
					},
					.mipLevelCount = 1,
					.sampleCount = 1,
					.format = gpudl_get_preferred_swap_chain_texture_format(),
				}
			);
			assert(texture);
			ppw->fb[0].texture = texture;

			WGPUTextureView view = wgpuTextureCreateView(texture, &(WGPUTextureViewDescriptor) {});
			assert(view);
			ppw->fb[0].texture_view = view;

			WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
				rstate.device,
				&(WGPUBindGroupDescriptor){
					.layout = pp->gauss_bind_group_layout,
					.entryCount = 3,
					.entries = (WGPUBindGroupEntry[]){
						(WGPUBindGroupEntry){
							.binding = 0,
							.buffer = pp->gauss_unibuf,
							.offset = 0,
							.size = sizeof(struct ppgauss_uni),
						},
						(WGPUBindGroupEntry){
							.binding = 1,
							.textureView = view,
						},
						(WGPUBindGroupEntry){
							.binding = 2,
							.sampler = pp->gauss_sampler,
						},
					},
				}
			);
			assert(bind_group);
			ppw->fb[0].bind_group = bind_group;

			} break;
		}

	}

	switch (pp->type) {
	case PP_NONE: return swap_chain_texture_view;
	case PP_GAUSS: return ppw->fb[0].texture_view;
	default: assert(!"unhandled postproc type");
	}
}

static void postproc_end_frame(WGPUCommandEncoder encoder)
{
	struct postproc* pp = &rstate.postproc;
	struct postproc_window* ppw = rstate.current_ppw;

	switch (pp->type) {
	case PP_NONE: break;
	case PP_GAUSS: {
		struct ppgauss_uni u = {
			.width = ppw->width,
			.height = ppw->height,
			.seed = rstate.seed,
			.sigma = 0.003f,
			.rstep = 15.31f,
			.broken = 0.0f,
			.intensity = 0.4f,
			.n0 = 3,
			.n1 = 4,
		};

		wgpuQueueWriteBuffer(rstate.queue, pp->gauss_unibuf, 0, &u, sizeof u);

		assert(ppw->swap_chain_texture_view);
		WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
			encoder,
			&(WGPURenderPassDescriptor){
				.colorAttachmentCount = 1,
				.colorAttachments = &(WGPURenderPassColorAttachment){
					.view = ppw->swap_chain_texture_view,
					.resolveTarget = 0,
					.loadOp = WGPULoadOp_Clear,
					.storeOp = WGPUStoreOp_Store,
					.clearValue = (WGPUColor){.r=1,.g=0,.b=1,.a=1},
				},
				.depthStencilAttachment = NULL,
			}
		);
		wgpuRenderPassEncoderSetPipeline(pass, pp->gauss_pipeline);

		WGPUBindGroup bind_group = ppw->fb[0].bind_group;
		assert(bind_group != NULL);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, 0);

		wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

		wgpuRenderPassEncoderEnd(pass);
		} break;
	}
}

static int glyphdef_compare(union glyphdef a, union glyphdef b)
{
	uint32_t a32 = a.u32;
	uint32_t b32 = b.u32;
	if (a32 < b32) return -1;
	if (a32 > b32) return 1;
	return 0;
}

static int glyphdef_compar(const void* va, const void* vb)
{
	return glyphdef_compare(
		*(const union glyphdef*)va,
		*(const union glyphdef*)vb);
}

static inline int stbrect_compare(const stbrp_rect* a, const stbrp_rect* b)
{
	union glyphdef ua = { .u32 = a->id };
	union glyphdef ub = { .u32 = b->id };
	return glyphdef_compare(ua, ub);
}

static int stbrect_compar(const void* va, const void* vb)
{
	return stbrect_compare(va, vb);
}

static int get_glyph_index(union glyphdef glyphdef)
{
	struct r* r = &rstate;
	struct tile_atlas* a = &r->tile_atlas;
	stbrp_rect* rs = a->rects_arr;
	int left = 0;
	int right = arrlen(rs) - 1;
	while (left <= right) {
		int mid = (left+right) >> 1;
		int d = glyphdef_compare((union glyphdef) {.u32 = rs[mid].id}, glyphdef);
		if (d < 0) {
			left = mid + 1;
		} else if (d > 0) {
			right = mid - 1;
		} else {
			return mid;
		}
	}
	return -1;
}

static float tile_dimensions_in_units[RT_END][2] = {
	#define DEF_TILE(N,G,W,H,X0,Y0,EXPR) { (float)(W), (float)(H) },
	TILES
	#undef DEF_TILE
};

static inline int getpx(float units, int size)
{
	int v = ceilf(units * (float)size);
	if (v <= 0) v = 1;
	return v;
}

static struct font* get_font_for_bank(int bank)
{
	struct r* r = &rstate;
	switch (bank) {
	case R_FONT_MONOSPACE: return &r->font_monospace;
	case R_FONT_VARIABLE:  return &r->font_variable;
	default: assert(!"invalid font");
	}
}

static void get_glyph_dim(union glyphdef glyphdef, int* w, int* h)
{
	const int bank = glyphdef.bank;
	const int size = glyphdef.size;
	const int code = glyphdef.code;
	if (bank == R_TILES) {
		assert(0 <= code && code < RT_END);
		float wu = tile_dimensions_in_units[code][0];
		float hu = tile_dimensions_in_units[code][1];
		if (w) *w = wu == EXT ? 1 : getpx(wu, size);
		if (h) *h = hu == EXT ? 1 : getpx(hu, size);
	} else {
		struct font* font = get_font_for_bank(bank);
		const float scale = stbtt_ScaleForPixelHeight(&font->info, size);
		int x0=0,y0=0,x1=0,y1=0;
		stbtt_GetCodepointBitmapBox(&font->info, code, scale, scale, &x0, &y0, &x1, &y1);
		int ww = x1-x0;
		int hh = y1-y0;
		if (w) *w = ww;
		if (h) *h = hh;
	}
}

static void glyph_raster(stbrp_rect* r)
{
	struct tile_atlas* a = &rstate.tile_atlas;
	uint8_t* p = a->bitmap + r->x + r->y * ATLAS_WIDTH;
	const int stride = ATLAS_WIDTH;
	assert(stride > 0);
	union glyphdef glyphdef = {.u32 = r->id};
	int bank = glyphdef.bank;
	int size = glyphdef.size;
	int code = glyphdef.code;
	//uint64_t t0 = stm_now();
	int w,h;
	get_glyph_dim(glyphdef, &w, &h);
	if (w > 0 && h > 0) {
		if (bank == R_TILES) {
			r_tile_raster(code, w, h, p, stride);
		} else {
			struct font* font = get_font_for_bank(bank);
			const float scale = stbtt_ScaleForPixelHeight(&font->info, size);
			stbtt_MakeCodepointBitmap(&font->info, p, w, h, ATLAS_WIDTH, scale, scale, code);
		}
	}
	//uint64_t t1 = stm_now();
	//printf("glyph_raster() for bank=%d, size=%d, code=%d took %f seconds (%dx%d)\n", bank, size, code, (double)stm_ns(stm_diff(t1,t0))*1e-9, w, h);

	const int ux0 = r->x;
	const int uy0 = r->y;
	const int ux1 = r->x + r->w;
	const int uy1 = r->y + r->h;
	if (!a->update) {
		a->update = 1;
		a->update_x0 = ux0;
		a->update_y0 = uy0;
		a->update_x1 = ux1;
		a->update_y1 = uy1;
	} else {
		if (ux0 < a->update_x0) a->update_x0 = ux0;
		if (ux1 > a->update_x1) a->update_x1 = ux1;
		if (uy0 < a->update_y0) a->update_y0 = uy0;
		if (uy1 > a->update_y1) a->update_y1 = uy1;
	}
}

static int patch_tile_uvs(int n_quads)
{
	struct r* r = &rstate;
	struct tile_atlas* a = &r->tile_atlas;
	struct tile_vtx* pv = (struct tile_vtx*)(r->vtxbuf_data + r->cursor0);
	int n_missing = 0;
	for (int i = 0; i < n_quads; i++) {
		const union glyphdef glyphdef = r->glyphdef_requests[i];
		const int glyph_index = get_glyph_index(glyphdef);
		if (glyph_index < 0) {
			r->missing_glyphdefs[n_missing++] = glyphdef;
		} else {
			stbrp_rect* r = &a->rects_arr[glyph_index];

			float u = r->x;
			float v = r->y;
			float w, h;
			if (glyphdef.bank == R_TILES) {
				const int code = glyphdef.code;
				assert(0 <= code && code < RT_END);
				const int ext_x = tile_dimensions_in_units[code][0] == EXT;
				const int ext_y = tile_dimensions_in_units[code][1] == EXT;
				if (ext_x) u += 0.5f;
				if (ext_y) v += 0.5f;
				w = ext_x ? 0.0f : r->w;
				h = ext_y ? 0.0f : r->h;
			} else {
				w = r->w;
				h = r->h;
			}

			for (int i = 0; i < 4; i++) {
				pv[i].a_uv = v2(
					lerp(pv[i].a_uv.u, u, u+w),
					lerp(pv[i].a_uv.v, v, v+h)
				);
			}
		}
		pv += 4;
	}
	return n_missing;
}

static void r_flush_submit(int n_bytes, int do_postproc)
{
	struct r* r = &rstate;
	assert(r->encoder != NULL);

	if (n_bytes > 0) {
		assert(n_bytes <= r->vtxbuf_cursor);
		wgpuQueueWriteBuffer(r->queue, r->vtxbuf, 0, &r->vtxbuf_data, n_bytes);
		int remaining = r->vtxbuf_cursor - n_bytes;
		if (remaining) {
			memmove(r->vtxbuf_data, r->vtxbuf_data + n_bytes, remaining);
		}
		r->vtxbuf_cursor = remaining;
		r->cursor0 = 0;
	}

	struct tile_atlas* a = &rstate.tile_atlas;
	if (a->update) {
		const int h = a->update_y1 - a->update_y0;
		wgpuQueueWriteTexture(
			r->queue,
			&(WGPUImageCopyTexture) {
				.texture = a->texture,
				.origin = {
					.x = 0,
					.y = a->update_y0,
				},
			},
			a->bitmap + a->update_y0 * ATLAS_WIDTH,
			ATLAS_WIDTH * h,
			&(WGPUTextureDataLayout) {
				.bytesPerRow = ATLAS_WIDTH,
			},
			&(WGPUExtent3D) {
				.width = ATLAS_WIDTH,
				.height = h,
				.depthOrArrayLayers = 1,
			}
		);
		a->update = 0;
	}

	if (do_postproc) {
		postproc_end_frame(r->encoder);
	}

	WGPUCommandBuffer cmdbuf = wgpuCommandEncoderFinish(r->encoder, &(WGPUCommandBufferDescriptor){});
	wgpuQueueSubmit(r->queue, 1, &cmdbuf);
	r->encoder = NULL;
}


#define FF_END_MODE         (1<<0)
#define FF_END_FRAME        (1<<1)
#define FF_VTXBUF_OVERFLOW  (1<<2)
#define FF_IDXBUF_OVERFLOW  (1<<3)
#define FF__REENTRY         (1<<4)
static void r_flush(int flags)
{
	assert(((flags == FF_END_MODE) || (flags == FF_END_FRAME) || ((flags&FF_VTXBUF_OVERFLOW) || (flags&FF_IDXBUF_OVERFLOW) || (flags&FF__REENTRY))) && "invalid r_flush() flags");

	struct r* r = &rstate;
	assert(r->vtxbuf_cursor >= r->cursor0);
	const int n_bytes = (r->vtxbuf_cursor - r->cursor0);
	assert(n_bytes >= 0);

	const int is_end_mode         = flags & FF_END_MODE;
	const int is_end_frame        = flags & FF_END_FRAME;
	const int is_vtxbuf_overflow  = flags & FF_VTXBUF_OVERFLOW;
	const int is_idxbuf_overflow  = flags & FF_IDXBUF_OVERFLOW;
	const int is_reentry          = flags & FF__REENTRY;

	assert((!is_end_frame || n_bytes == 0) && "cannot end frame with elements still in vtxbuf");

	const int do_pass   = (n_bytes > 0) && (is_end_mode || is_vtxbuf_overflow || is_idxbuf_overflow);
	const int do_submit = is_end_frame || is_vtxbuf_overflow;

	if (r->encoder == NULL) {
		r->encoder = wgpuDeviceCreateCommandEncoder(r->device, &(WGPUCommandEncoderDescriptor){});
		assert(r->encoder != NULL);
	}

	if (do_pass) {
		assert(r->mode > 0);
		assert(!is_end_frame && "not expecting to emit render passes during ''end frame''");

		if (r->mode == R_MODE_TILE || r->mode == R_MODE_TILEPTN) {
			const int has_previous_tile_pass = (r->n_tile_passes++) > 0;

			const int n_divisor = 4*sizeof(struct tile_vtx);
			assert((n_bytes % n_divisor) == 0);
			const int n_quads = n_bytes / n_divisor;
			assert(n_quads < MAX_TILE_QUADS);
			struct tile_atlas* a = &r->tile_atlas;

			const int n_missing = patch_tile_uvs(n_quads);

			if (n_missing > 0) {
				qsort(r->missing_glyphdefs, n_missing, sizeof r->missing_glyphdefs[0], glyphdef_compar);

				union glyphdef last_glyphdef = {.u32=-1};
				const int r0 = arrlen(a->rects_arr);
				for (int i = 0; i < n_missing; i++) {
					union glyphdef missing_glyphdef = r->missing_glyphdefs[i];
					if (missing_glyphdef.u32 == last_glyphdef.u32) continue;

					int w=0, h=0;
					get_glyph_dim(missing_glyphdef, &w, &h);
					stbrp_rect r = { .id = missing_glyphdef.u32, .w = w, .h = h };
					arrput(a->rects_arr, r);

					last_glyphdef = missing_glyphdef;
				}
				const int r1 = arrlen(a->rects_arr);
				assert(r1 > r0);

				if (r0 == 0) {
					const int n_nodes = ATLAS_WIDTH;
					a->nodes = realloc(a->nodes, n_nodes * sizeof(*a->nodes));
					stbrp_init_target(&a->ctx, ATLAS_WIDTH, ATLAS_HEIGHT, a->nodes, n_nodes);
				}

				int success = stbrp_pack_rects(
					&a->ctx,
					&a->rects_arr[r0],
					r1-r0);
				if (success) {
					for (int i = r0; i < r1; i++) {
						glyph_raster(&a->rects_arr[i]);
					}
					qsort(a->rects_arr, arrlen(a->rects_arr), sizeof *a->rects_arr, stbrect_compar);
					assert(patch_tile_uvs(n_quads) == 0);
				} else if (!is_reentry) {
					if (has_previous_tile_pass) {
						r_flush_submit(r->cursor0, 0);
						flags &= ~FF_VTXBUF_OVERFLOW;
					}
					flags |= FF__REENTRY;
					arrsetlen(a->rects_arr, 0);
					r_flush(flags);
					return;
				}
			}
		}

		assert(r->encoder != NULL);
		assert(r->render_target_texture_view != NULL);

		WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
			r->encoder,
			&(WGPURenderPassDescriptor){
				.colorAttachmentCount = 1,
				.colorAttachments = &(WGPURenderPassColorAttachment){
					.view = r->render_target_texture_view,
					.resolveTarget = 0,
					.loadOp = r->n_passes == 0 ? WGPULoadOp_Clear : WGPULoadOp_Load, // first pass clears the target
					.storeOp = WGPUStoreOp_Store,
					.clearValue = (WGPUColor){},
				},
				.depthStencilAttachment = NULL,
			}
		);

		assert(r->pipeline != NULL);
		wgpuRenderPassEncoderSetPipeline(pass, r->pipeline);

		for (int i = 0; i < r->n_bind_groups; i++) {
			WGPUBindGroup g = r->bind_groups[i];
			assert(g != NULL);
			wgpuRenderPassEncoderSetBindGroup(pass, i, g, 0, 0);
		}

		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, r->vtxbuf, r->cursor0, n_bytes);

		switch (r->mode) {
		case R_MODE_TILE:
		case R_MODE_TILEPTN: {
			wgpuRenderPassEncoderSetIndexBuffer(
				pass,
				r->static_quad_idxbuf,
				WGPUIndexFormat_Uint16,
				0,
				sizeof(uint16_t)*r->n_static_quad_indices);
			wgpuRenderPassEncoderDrawIndexed(pass, r->n_static_quad_indices, 1, 0, 0, 0);
			} break;
		case R_MODE_VECTOR: {
			const int divisor = (3*sizeof(struct vector_vtx));
			assert((n_bytes % divisor) == 0);
			wgpuRenderPassEncoderDraw(pass, 3*(n_bytes/divisor), 1, 0, 0);
			} break;
		default: assert(!"unhandled mode");
		}

		wgpuRenderPassEncoderEnd(pass);
		r->n_passes++;
		r->cursor0 = r->vtxbuf_cursor;
		r->n_static_quad_indices = 0;
	}

	if (do_submit) {
		const int do_postproc = is_end_frame && !r->is_pattern_frame;
		r_flush_submit(r->vtxbuf_cursor, do_postproc);
	}
}

void r_begin_frames(void)
{
	struct r* r = &rstate;
	assert((!r->begun_frames) && "already inside r_begin_frames()");
	r->begun_frames = 1;
}

void r_end_frames(void)
{
	struct r* r = &rstate;
	assert((r->begun_frames) && "not inside r_begin_frames()");

	// each r_begin_frame()/r_end_frame() cycle needs to update
	// window-local resources when postproc type changes. since
	// type!=previous_type is used to detect these changes, we can't update
	// previous_type until ALL windows have been rendered
	struct postproc* pp = &r->postproc;
	pp->previous_type = pp->type;

	r->begun_frames = 0;

	r->seed = fmodf(r->seed + 0.1f, 11.11f);
}

static void begin_frame(int width, int height, WGPUTextureView target, float draw_scalar)
{
	struct r* r = &rstate;

	assert((r->begun_frames) && "not inside r_begin_frames()");
	assert((!r->begun_frame) && "frame already begun");

	r->width = width;
	r->height = height;

	assert(r->vtxbuf_cursor == 0);

	r->n_passes = 0;
	r->n_tile_passes = 0;
	assert(r->n_static_quad_indices == 0);
	r->render_target_texture_view = target;

	struct draw_uni u = {
		.width = width,
		.height = height,
		.seed = r->seed,
		.scalar = draw_scalar,
	};
	wgpuQueueWriteBuffer(r->queue, r->draw_unibuf, 0, &u, sizeof u);

	r->begun_frame = 1;
}

static void end_frame(void)
{
	struct r* r = &rstate;
	assert((r->begun_frame) && "not inside frame");
	assert((r->mode == 0) && "mode not r_end()'d");
	r_flush(FF_END_FRAME);
	r->begun_frame = 0;
	r->width = 0;
	r->height = 0;
}

void r_begin_frame(int width, int height, struct postproc_window* ppw, WGPUTextureView swap_chain_texture_view)
{
	const enum postproc_type t = rstate.postproc.type;
	const float scalar =
		t == PP_NONE ? MAX_INTENSITY :
		t == PP_GAUSS ? 1.0f :
		0.0f;
	begin_frame(
		width, height,
		postproc_begin_frame(ppw, width, height, swap_chain_texture_view),
		scalar);
	rstate.is_pattern_frame = 0;
}

void r_end_frame(void)
{
	assert(!rstate.is_pattern_frame);
	end_frame();
}

static struct pattern* get_pattern(int pattern)
{
	struct r* r = &rstate;
	assert(0 <= pattern && pattern < arrlen(r->patterns));
	struct pattern* p = &r->patterns[pattern];
	assert(!p->is_free);
	return p;
}

void r_begin_ptn_frame(int pattern)
{
	struct pattern* p = get_pattern(pattern);
	begin_frame(
		p->width, p->height,
		p->texture_view,
		1.0f);
	rstate.is_pattern_frame = 1;
}

void r_end_ptn_frame(void)
{
	assert(rstate.is_pattern_frame);
	end_frame();
}

void r_begin(int mode)
{
	struct r* r = &rstate;
	assert((r->begun_frame) && "not inside r_begin_frame()");
	assert((r->mode == 0) && "already in a mode");

	switch (mode) {
	case R_MODE_TILE:
		r->pipeline = r->tile_pipeline;
		r->n_bind_groups = 1;
		r->bind_groups[0] = r->tile_bind_group;
		break;
	case R_MODE_TILEPTN:
		r->pipeline = r->tileptn_pipeline;
		r->n_bind_groups = 2;
		r->bind_groups[0] = r->tile_bind_group;
		r->bind_groups[1] = r->current_pattern_bind_group;
		break;
	case R_MODE_VECTOR:
		r->pipeline = r->vector_pipeline;
		r->n_bind_groups = 1;
		r->bind_groups[0] = r->vector_bind_group;
		break;
	default: assert(!"unhandled mode");
	}

	r->cursor0 = r->vtxbuf_cursor;
	r->mode = mode;
}

void r_end(void)
{
	r_flush(FF_END_MODE);
	struct r* r = &rstate;
	assert(r->mode > 0);
	r->mode = 0;
}

void r_offset(int x0, int y0)
{
	struct r* r = &rstate;
	r->offset_x0 = x0;
	r->offset_y0 = y0;
}

void r_clip(int x, int y, int w, int h)
{
	struct r* r = &rstate;
	r->clip_x = x;
	r->clip_y = y;
	r->clip_w = w;
	r->clip_h = h;
}

static void* r_request(size_t vtxbuf_requested, int idxbuf_requested)
{
	assert(((vtxbuf_requested&3) == 0) && "vtxbuf_requested must be 4-byte aligned");
	struct r* r = &rstate;
	const int vtxbuf_required = r->vtxbuf_cursor + vtxbuf_requested;
	const int idxbuf_required = r->n_static_quad_indices + idxbuf_requested;
	const int vtxbuf_overflow = (vtxbuf_required > VTXBUF_SZ);
	const int idxbuf_overflow = (idxbuf_required > STATIC_QUADS_IDXBUF_SZ);
	int ff =
		  (vtxbuf_overflow ? FF_VTXBUF_OVERFLOW : 0)
		+ (idxbuf_overflow ? FF_IDXBUF_OVERFLOW : 0);
	if (ff) r_flush(ff);
	assert((r->vtxbuf_cursor + vtxbuf_requested) <= VTXBUF_SZ);
	assert((r->n_static_quad_indices + idxbuf_requested ) <= STATIC_QUADS_IDXBUF_SZ);

	void* p = r->vtxbuf_data + r->vtxbuf_cursor;
	r->vtxbuf_cursor += vtxbuf_requested;
	assert((r->vtxbuf_cursor&3) == 0);

	r->n_static_quad_indices += idxbuf_requested;

	return p;
}

int rptn_new(int width, int height)
{
	struct r* r = &rstate;
	const int n_patterns = arrlen(r->patterns);
	struct pattern* p = NULL;
	for (int i = 0; i < n_patterns; i++) {
		struct pattern* ptmp  = &r->patterns[i];
		if (ptmp->is_free) {
			p = ptmp;
			break;
		}
	}
	if (p == NULL) p = arraddnptr(r->patterns, 1);
	assert(p != NULL);
	memset(p, 0, sizeof *p);

	p->width = width;
	p->height = height;

	p->texture = wgpuDeviceCreateTexture(
		rstate.device,
		&(WGPUTextureDescriptor) {
			.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
			.dimension = WGPUTextureDimension_2D,
			.size = (WGPUExtent3D){
				.width = width,
				.height = height,
				.depthOrArrayLayers = 1,
			},
			.mipLevelCount = 1,
			.sampleCount = 1,
			.format = gpudl_get_preferred_swap_chain_texture_format(),
		}
	);
	assert(p->texture);

	p->texture_view = wgpuTextureCreateView(p->texture, &(WGPUTextureViewDescriptor) {});
	assert(p->texture_view);

	p->bind_group = wgpuDeviceCreateBindGroup(r->device, &(WGPUBindGroupDescriptor){
		.layout = r->pattern_bind_group_layout,
		.entryCount = 3,
		.entries = (WGPUBindGroupEntry[]){
			(WGPUBindGroupEntry){
				.binding = 0,
				.buffer = r->pattern_unibuf,
				.offset = 0,
				.size = sizeof(struct pattern_uni),
			},
			(WGPUBindGroupEntry){
				.binding = 1,
				.textureView = p->texture_view,
			},
			(WGPUBindGroupEntry){
				.binding = 2,
				.sampler = r->pattern_sampler,
			},
		},
	});
	assert(p->bind_group);

	return p - r->patterns;
}

void rptn_free(int pattern)
{
	struct pattern* p = get_pattern(pattern);
	p->is_free = 1;
	wgpuBindGroupDrop(p->bind_group);
	wgpuTextureViewDrop(p->texture_view);
	wgpuTextureDrop(p->texture);
}

void rptn_set(int pattern, int dx, int dy)
{
	struct r* r = &rstate;
	assert(r->mode == 0);
	struct pattern* p = get_pattern(pattern);
	r->current_pattern_bind_group = p->bind_group;
	const float sx = 1.0f / (float)p->width;
	const float sy = 1.0f / (float)p->height;
	struct pattern_uni u = {
		.x0 = (float)(dx + r->offset_x0) * -sx,
		.y0 = (float)(dy + r->offset_y0) * -sy,
		.bxx = sx,
		.bxy = 0,
		.byx = 0,
		.byy = sy,
	};
	wgpuQueueWriteBuffer(r->queue, r->pattern_unibuf, 0, &u, sizeof u);
}

void rcol_lgrad(union v4 color0, union v4 color1, union v2 basis)
{
	struct r* r = &rstate;
	r->color[0] = c16pak(color0);
	r->color[1] = c16pak(color1);
	r->gradient_basis = basis;
}

void rcol_plain(union v4 color)
{
	rcol_lgrad(color, color, v2(0,0));
}

void rcol_xgrad(union v4 color0, union v4 color1)
{
	rcol_lgrad(color0, color1, v2(1,0));
}

void rcol_ygrad(union v4 color0, union v4 color1)
{
	rcol_lgrad(color0, color1, v2(0,1));
}

void rt_font(enum r_font font, int px)
{
	assert((R_TILES < font) && (font < R_FONT_END) && "invalid font");
	assert(0 < px && px < 512);
	struct r* r = &rstate;
	r->font = font;
	r->font_px = px;
}

void rt_goto(int cx, int cy)
{
	struct r* r = &rstate;
	r->font_cx0 = r->font_cx = cx;
	r->font_cy = cy;
}

static int utf8_decode(const char** c0z, int* n)
{
	const unsigned char** c0 = (const unsigned char**)c0z;
	if (*n <= 0) return -1;
	unsigned char c = **c0;
	(*n)--;
	(*c0)++;
	if ((c & 0x80) == 0) return c & 0x7f;
	int mask = 192;
	int d;
	for (d = 1; d <= 3; d++) {
		int match = mask;
		mask = (mask >> 1) | 0x80;
		if ((c & mask) == match) {
			int codepoint = (c & ~mask) << (6*d);
			while (d > 0 && *n > 0) {
				c = **c0;
				if ((c & 192) != 128) return -1;
				(*c0)++;
				(*n)--;
				d--;
				codepoint += (c & 63) << (6*d);
			}
			return d == 0 ? codepoint : -1;
		}
	}
	return -1;
}

static union v2 get_offset()
{
	return v2(rstate.offset_x0, rstate.offset_y0);
}

static struct rect get_clip_rect()
{
	struct r* r = &rstate;
	return rect(r->clip_x, r->clip_y, r->clip_w, r->clip_h);
}

static int quadclip(struct clip* clip, int x, int y, int w, int h)
{
	struct rect clip_rect = get_clip_rect();
	const union v2 o = get_offset();
	struct rect input_rect = rect(x+o.x, y+o.y, w, h);
	clip_rectangle(clip, &clip_rect, &input_rect);
	if (clip->result == CLIP_ALL) {
		assert(clip->n == 0);
		return 0;
	} else {
		assert(clip->n == 4);
		return 1;
	}
}

static int triclip(struct clip* clip, union v2* p0, union v2* p1, union v2* p2)
{
	struct rect clip_rect = get_clip_rect();
	const union v2 o = get_offset();
	union v2 pp0 = v2_add(*p0, o);
	union v2 pp1 = v2_add(*p1, o);
	union v2 pp2 = v2_add(*p2, o);
	clip_triangle(clip, &clip_rect, &pp0, &pp1, &pp2);
	return clip->result != CLIP_ALL;
}

static void rt_put(int bank, int size, int code, int x, int y, int w, int h)
{
	struct r* r = &rstate;
	assert(r->mode == R_MODE_TILE || r->mode == R_MODE_TILEPTN);
	if ((w <= 0) || (h <= 0)) return;

	struct clip clip;
	if (!quadclip(&clip, x, y, w, h)) return;

	struct tile_vtx* pv = r_request(4 * sizeof(*pv), 6);

	for (int i = 0; i < 4; i++) {
		pv[i].a_pos   = clip.vs[i].xy;
		pv[i].a_uv    = clip.vs[i].uv; // these are temporary UVs; expanded by r_flush()
		const float t = v2_dot(clip.vs[i].uv, r->gradient_basis);
		pv[i].a_color = c16pak(v4_lerp(t, c16unpak(r->color[0]), c16unpak(r->color[1])));
	}

	const int index = ((r->vtxbuf_cursor - r->cursor0) / (4*sizeof(*pv))) - 1;
	assert(0 <= index && index < MAX_TILE_QUADS);
	r->glyphdef_requests[index] = encode_glyphdef(bank, size, code);
}

void rt_printf(const char* fmt, ...)
{
	struct r* r = &rstate;

	char buffer[1<<14];
	va_list ap;
	va_start(ap, fmt);
	const int n0 = vsnprintf(buffer, sizeof buffer, fmt, ap);
	va_end(ap);

	const int px = r->font_px;
	struct font* font = get_font_for_bank(r->font);
	const float scale = stbtt_ScaleForPixelHeight(&font->info, px);

	//const int ascent_px = roundf((float)(font->ascent) * scale);
	const int linebreak_dy = roundf((float)(font->ascent - font->descent + font->line_gap) * scale);

	int cursor_x = r->font_cx;
	int cursor_y = r->font_cy;

	const char* p = buffer;
	int n = n0;
	int last_codepoint = -1;
	for (;;) {
		int codepoint = utf8_decode(&p, &n);
		if (codepoint < 0) break;

		if (codepoint < ' ') {
			if (codepoint == '\n') {
				cursor_x = r->font_cx0;
				cursor_y += linebreak_dy;
			}
			last_codepoint = -1;
		} else {
			// NOTE stb_truetype.h recommends caching this, but... is "my
			// binary search better than theirs"?
			const int glyph_index = stbtt_FindGlyphIndex(&font->info, codepoint);

			int advance_width = 0;
			int left_side_bearing = 0;
			stbtt_GetGlyphHMetrics(&font->info, glyph_index, &advance_width, &left_side_bearing);
			int kern = last_codepoint > 0 ? stbtt_GetCodepointKernAdvance(&font->info, last_codepoint, codepoint) : 0;
			const int dx = roundf((float)(advance_width + kern) * scale);

			int x0, y0, x1, y1;
			stbtt_GetGlyphBitmapBox(&font->info, glyph_index, scale, scale, &x0, &y0, &x1, &y1);
			const int w = x1-x0;
			const int h = y1-y0;

			if (w > 0 && h > 0) {
				const int lsb = (int)roundf((float)left_side_bearing * scale);
				//printf("plot %d at %d,%d %dx%d\n", codepoint, cursor_x + lsb, cursor_y + y0, w, h);
				rt_put(rstate.font, rstate.font_px, codepoint, cursor_x + lsb, cursor_y + y0, w, h);
			}

			cursor_x += dx;
			last_codepoint = codepoint;
		}
	}

	r->font_cx = cursor_x;
	r->font_cy = cursor_y;
}

static int get_tile_px(enum r_tile t)
{
	return 8; // TODO tile group size configuration, or at least return default
}

void rt_3x3(enum r_tile t00, int x, int y, int w, int h)
{
	assert(0 <= t00 && t00 < RT_END);
	const int px = get_tile_px(t00);

	const float wu = tile_dimensions_in_units[t00][0];
	const float hu = tile_dimensions_in_units[t00][1];

	assert((wu != EXT) && (hu != EXT) && "3x3 x0y0 cannot have EXT width nor height");

	assert(EXT == tile_dimensions_in_units[t00+1][0]);
	assert(EXT == tile_dimensions_in_units[t00+4][0]);
	assert(EXT == tile_dimensions_in_units[t00+7][0]);
	assert(wu  == tile_dimensions_in_units[t00+2][0]);
	assert(wu  == tile_dimensions_in_units[t00+3][0]);
	assert(wu  == tile_dimensions_in_units[t00+5][0]);
	assert(wu  == tile_dimensions_in_units[t00+6][0]);
	assert(wu  == tile_dimensions_in_units[t00+8][0]);

	assert(EXT == tile_dimensions_in_units[t00+3][1]);
	assert(EXT == tile_dimensions_in_units[t00+4][1]);
	assert(EXT == tile_dimensions_in_units[t00+5][1]);
	assert(hu  == tile_dimensions_in_units[t00+1][1]);
	assert(hu  == tile_dimensions_in_units[t00+2][1]);
	assert(hu  == tile_dimensions_in_units[t00+6][1]);
	assert(hu  == tile_dimensions_in_units[t00+7][1]);
	assert(hu  == tile_dimensions_in_units[t00+8][1]);

	const int wpx = getpx(wu, px);
	const int hpx = getpx(hu, px);
	assert((wpx >= 1) && (hpx >= 1));

	const int midw = w-2*wpx;
	const int midh = h-2*hpx;

	const int bank = R_TILES;

	if (midw < 0 || midh < 0) {
		// no room for corners; just plot the middle
		rt_put(bank, px, t00+4, x, y, w, h);
		return;
	}

	const int x0 = x;
	const int x1 = x+wpx;
	const int x2 = x+w-wpx;
	const int y0 = y;
	const int y1 = y+hpx;
	const int y2 = y+h-hpx;

	rt_put(bank, px, t00+0, x0, y0, wpx,  hpx);
	rt_put(bank, px, t00+1, x1, y0, midw, hpx);
	rt_put(bank, px, t00+2, x2, y0, wpx,  hpx);

	rt_put(bank, px, t00+3, x0, y1, wpx,  midh);
	if (r_tile_sample(t00+4, v2(0,0)) > 0.0) {
		rt_put(bank, px, t00+4, x1, y1, midw, midh);
	}
	rt_put(bank, px, t00+5, x2, y1, wpx,  midh);

	rt_put(bank, px, t00+6, x0, y2, wpx,  hpx);
	rt_put(bank, px, t00+7, x1, y2, midw, hpx);
	rt_put(bank, px, t00+8, x2, y2, wpx,  hpx);
}

void rt_quad(float x, float y, float w, float h)
{
	rt_put(R_TILES, 0, RT_one, x, y, w, h);
}

void rt_clear(void)
{
	struct r* r = &rstate;
	rt_quad(0, 0, r->width, r->height);
}

void rv_tri_c16(union v2 p0, union c16 c0, union v2 p1, union c16 c1, union v2 p2, union c16 c2)
{
	struct r* r = &rstate;
	assert(r->mode == R_MODE_VECTOR);

	struct clip clip;
	if (!triclip(&clip, &p0, &p1, &p2)) return;

	if (clip.result == CLIP_NONE) {
		// cheap path; no clipping
		assert(clip.n == 3);
		struct vector_vtx* pv = r_request(3*sizeof(*pv), 0);
		pv[0].a_pos = clip.vs[0].xy;
		pv[0].a_color = c0;
		pv[1].a_pos = clip.vs[1].xy;
		pv[1].a_color = c1;
		pv[2].a_pos = clip.vs[2].xy;
		pv[2].a_color = c2;
	} else {
		const int n_tris = clip.n - 2;

		union c16 cs[ARRAY_LENGTH(clip.vs)];
		const union v4 cc0 = c16unpak(c0);
		const union v4 cc1 = c16unpak(c1);
		const union v4 cc2 = c16unpak(c2);
		for (int i = 0; i < clip.n; i++) {
			union v3 uvw = clip_get_barycentric_coord(&clip, i);
			cs[i] = c16pak(v4_add(v4_scale(uvw.u, cc0), v4_add(v4_scale(uvw.v, cc1), v4_scale(uvw.w, cc2))));
		}

		struct vector_vtx* pv = r_request(n_tris*3*sizeof(*pv), 0);
		for (int i = 0; i < n_tris; i++) {
			pv[0].a_pos = clip.vs[0].xy;
			pv[0].a_color = cs[0];
			pv[1].a_pos = clip.vs[1+i].xy;
			pv[1].a_color = cs[1+i];
			pv[2].a_pos = clip.vs[2+i].xy;
			pv[2].a_color = cs[2+i];
			pv += 3;
		}
	}
}

void rv_tri(union v2 p0, union v4 c0, union v2 p1, union v4 c1, union v2 p2, union v4 c2)
{
	rv_tri_c16(p0, c16pak(c0), p1, c16pak(c1), p2, c16pak(c2));
}

#define R_EXPAND_QUAD_TO_TRIS(verts) \
	memcpy(&(verts)[4], &(verts)[0], sizeof((verts)[0])); \
	memcpy(&(verts)[5], &(verts)[2], sizeof((verts)[0]));

void rv_quad(float x, float y, float w, float h)
{
	struct r* r = &rstate;
	assert(r->mode == R_MODE_VECTOR);

	struct clip clip;
	if (!quadclip(&clip, x, y, w, h)) return;

	struct vector_vtx* pv = r_request(6 * sizeof(*pv), 0);
	for (int i = 0; i < 4; i++) {
		pv[i].a_pos   = clip.vs[i].xy;
		const float t = v2_dot(clip.vs[i].uv, r->gradient_basis);
		pv[i].a_color = c16pak(v4_lerp(t, c16unpak(r->color[0]), c16unpak(r->color[1])));
	}

	R_EXPAND_QUAD_TO_TRIS(pv)
}

void rv_move_to(float x, float y)
{
	struct path* path = &rstate.path;
	path->vs[0] = v2(x,y);
	path->n = 1;
	path->prepped = 0;
}

void rv_line_to(float x, float y)
{
	struct path* path = &rstate.path;
	assert((path->n > 0) && "path not begun");
	if (path->n >= MAX_PATH_VERTICES) return;
	path->vs[path->n++] = v2(x,y);
}

void rv_bezier_to(float cx0, float cy0, float cx1, float cy1, float x, float y)
{
	// TODO Ramer–Douglas–Peucker algorithm? or something better?
	struct path* path = &rstate.path;
	assert((path->n > 0) && "path not begun");
	const union v2 p0 = path->vs[path->n - 1];
	const union v2 p1 = v2(cx0,cy0);
	const union v2 p2 = v2(cx1,cy1);
	const union v2 p3 = v2(x,y);
	const int N = 75;
	for (int i = 1; i <= N; i++) {
		const float t = (float)i / (float)N;
		const float ts = t*t;   // t^2
		const float tss = ts*t; // t^3
		const float t1 = 1.0f - t; // (1-t)
		const float t1s = t1*t1;   // (1-t)^2
		const float t1ss = t1s*t1; // (1-t)^3
		const float c0 = t1ss;
		const float c1 = 3.0f * t1s * t;
		const float c2 = 3.0f * t1 * ts;
		const float c3 = tss;

		union v2 p = v2_scale(c0, p0);
		p = v2_add(p, v2_scale(c1, p1));
		p = v2_add(p, v2_scale(c2, p2));
		p = v2_add(p, v2_scale(c3, p3));

		rv_line_to(p.x, p.y);
	}
}

#if 0
static int line_segment_intersect(union v2 a0, union v2 a1, union v2 b0, union v2 b1)
{
	const union v2 s1 = v2_sub(a1,a0);
	const union v2 s2 = v2_sub(b1,b0);

	const float d = (-s2.x * s1.y + s1.x * s2.y);
	if (d == 0.0) return 0;
	const float dinv = 1.0f / d;

	const float s = (-s1.y * (a0.x - b0.x) + s1.x * (a0.y - b0.y)) * dinv;
	if (!(s >= 0 && s <= 1)) return 0;
	const float t = ( s2.x * (a0.y - b0.y) - s2.y * (a0.x - b0.x)) * dinv;
	return (t >= 0 && t <= 1);
}

#endif

struct tritest {
	union v2 a,v0,v1;
	float dot00, dot01, dot11;
	float denom_inv;
};

static void tritest_init(struct tritest* tt, union v2 a, union v2 b, union v2 c)
{
	tt->a = a;
	tt->v0 = v2_sub(c,a);
	tt->v1 = v2_sub(b,a);
	tt->dot00 = v2_dot(tt->v0,tt->v0);
	tt->dot01 = v2_dot(tt->v0,tt->v1);
	tt->dot11 = v2_dot(tt->v1,tt->v1);
	const float denom = (tt->dot00 * tt->dot11 - tt->dot01 * tt->dot01);
	tt->denom_inv = 0.0f;
	if (fabsf(denom) < 1e-10) return;
	tt->denom_inv = 1.0f / denom;
}

static int tritest_inside(struct tritest* tt, union v2 p)
{
	if (tt->denom_inv == 0.0f) return 0;
	const union v2 v2 = v2_sub(p,tt->a);
	const float dot02 = v2_dot(tt->v0,v2);
	const float dot12 = v2_dot(tt->v1,v2);
	const float u = (tt->dot11 * dot02 - tt->dot01 * dot12) * tt->denom_inv;
	const float v = (tt->dot00 * dot12 - tt->dot01 * dot02) * tt->denom_inv;
	return (u >= 0.0f) && (v >= 0.0f) && (u+v < 1.0f);
}

static void pathprep()
{
	struct path* path = &rstate.path;
	if (path->prepped) return;
	path->prepped = 1;

	const int n = path->n;
	if (n < 3) return;

	union v2 p0 = path->vs[n-2];
	int i1 = n-1;
	union v2 p1 = path->vs[i1];
	for (int i2 = 0; i2 < n; i2++) {
		const union v2 p2  = path->vs[i2];
		const union v2 n01 = v2_unit(v2_normal(v2_sub(p1,p0)));
		const union v2 n12 = v2_unit(v2_normal(v2_sub(p2,p1)));
		const union v2 n   = v2_unit(v2_add(n01,n12));
		const float s      = 1.0f / v2_dot(n, n01);
		path->ns[i1] = v2_scale(s,n);
		i1 = i2;
		p0 = p1;
		p1 = p2;
	}
}

void rv_fill(void)
{
	pathprep();

	struct path* path = &rstate.path;

	int n = path->n;
	if (n < 3) return;
	for (int i = 0; i < n; i++) {
		path->vsi[i] = v2_sub(path->vs[i], path->ns[i]);
	}

	{
		union c16 cc = rstate.color[0];
		union c16 cz = {0};
		int i0 = n-1;
		for (int i1 = 0; i1 < n; i1++) {
			union v2 p0 = path->vs[i0];
			union v2 p1 = path->vs[i1];
			union v2 p2 = path->vsi[i1];
			union v2 p3 = path->vsi[i0];
			rv_tri_c16(p0,cz, p1,cz, p2,cc);
			rv_tri_c16(p0,cz, p2,cc, p3,cc);
			i0 = i1;
		}
	}

	while (n > 3) {
		int ev = 0;

		union v2 p0 = path->vsi[0];
		union v2 p1 = path->vsi[1];

		for (int i0 = 2; i0 < n; i0++) {
			union v2 p2 = path->vsi[i0];

			const float c = v2_cross(v2_sub(p1,p0), v2_sub(p2,p1));
			if (c > 0) {
				struct tritest tt;
				tritest_init(&tt, p0, p1, p2);

				int principal = 1;
				for (int i1 = 0; i1 < n; i1++) {
					if (i1 >= i0-2 && i1 <= i0) continue;
					if (tritest_inside(&tt, path->vsi[i1])) {
						principal = 0;
						break;
					}
				}

				if (principal) {
					ev = i0-1; // p1
					break;
				}
			}

			p0=p1;
			p1=p2;
		}

		{
			const union v2 p0 = path->vsi[(ev+n-1)%n];
			const union v2 p1 = path->vsi[ev];
			const union v2 p2 = path->vsi[(ev+1)%n];
			union c16 c = rstate.color[0];
			rv_tri_c16(p0,c, p1,c, p2,c);
		}

		int to_move = (n - ev) - 1;
		if (to_move > 0) memmove(&path->vsi[ev], &path->vsi[ev+1], to_move*sizeof(path->vsi[0]));

		n--;
	}

	if (n == 3) {
		const union v2 p0 = path->vsi[0];
		const union v2 p1 = path->vsi[1];
		const union v2 p2 = path->vsi[2];
		union c16 c = rstate.color[0];
		rv_tri_c16(p0,c, p1,c, p2,c);
	}
}

void rv_stroke(float width, int close)
{
	if (width <= 0.0f) return;

	pathprep();

	struct path* path = &rstate.path;
	const int n = path->n;

	const float mid_width = width-1.0f;
	const int has_mid = mid_width > 0.0f;
	const float side_width = has_mid ? 1.0f : 1.0f+mid_width;

	int n_lanes = has_mid ? 3 : 2;

	float lane_t[4];
	union c16 lane_c16[4];
	if (has_mid) {
		lane_t[1] = -mid_width * 0.5;
		lane_t[2] = mid_width * 0.5;
		lane_t[0] = lane_t[1] - side_width;
		lane_t[3] = lane_t[2] + side_width;

		lane_c16[0] = lane_c16[3] = (union c16){0};
		lane_c16[1] = lane_c16[2] = rstate.color[0];
	} else {
		lane_t[0] = -side_width;
		lane_t[1] = 0.0f;
		lane_t[2] = -lane_t[0];

		lane_c16[0] = lane_c16[2] = (union c16){0};
		lane_c16[1] = c16pak(v4_scale(side_width, c16unpak(rstate.color[0])));
	}

	union v2 b0s[4];
	union v2 b1s[4];

	union v2* p0s = b0s;
	union v2* p1s = b1s;

	const int nc = n + (close?1:0);
	for (int ii = 0; ii < nc; ii++) {
		const int i = ii % n;
		for (int j = 0; j <= n_lanes; j++) {
			p1s[j] = v2_add(path->vs[i], v2_scale(lane_t[j], path->ns[i]));
		}

		if (ii > 0) {
			for (int j0 = 0; j0 < n_lanes; j0++) {
				const int j1 = j0+1;
				union v2 p0 = p0s[j0];
				union v2 p1 = p1s[j0];
				union v2 p2 = p1s[j1];
				union v2 p3 = p0s[j1];
				union c16 c0 = lane_c16[j0];
				union c16 c1 = lane_c16[j1];
				rv_tri_c16(p0,c0, p1,c0, p2,c1);
				rv_tri_c16(p0,c0, p2,c1, p3,c1);
			}
		}

		union v2* tmp = p0s;
		p0s = p1s;
		p1s = tmp;
	}
}

void r_init(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device, WGPUQueue queue)
{
	rstate.instance = instance;
	rstate.adapter = adapter;
	rstate.device = device;
	rstate.queue = queue;

#if 1
        WGPUAdapterProperties properties = {0};
        wgpuAdapterGetProperties(adapter, &properties);
        printf("vendor id: %d\n", properties.vendorID);
        printf("device id: %d\n", properties.deviceID);
        printf("adapter type: %d\n", properties.adapterType);
        printf("backend type: %d\n", properties.backendType);
        //printf("name: %s\n", properties.name);
        //printf("driver: %s\n", properties.driverDescription);

        WGPUSupportedLimits limits = {0};
        wgpuAdapterGetLimits(adapter, &limits);

        #define DUMP32(s) printf("  " #s ": %u\n", limits.limits.s);
        #define DUMP64(s) printf("  " #s ": %lu\n", limits.limits.s);
        DUMP32(maxTextureDimension1D)
        DUMP32(maxTextureDimension2D)
        DUMP32(maxTextureDimension3D)
        DUMP32(maxTextureArrayLayers)
        DUMP32(maxBindGroups)
        DUMP32(maxDynamicUniformBuffersPerPipelineLayout)
        DUMP32(maxDynamicStorageBuffersPerPipelineLayout)
        DUMP32(maxSampledTexturesPerShaderStage)
        DUMP32(maxSamplersPerShaderStage)
        DUMP32(maxStorageBuffersPerShaderStage)
        DUMP32(maxStorageTexturesPerShaderStage)
        DUMP32(maxUniformBuffersPerShaderStage)
        DUMP64(maxUniformBufferBindingSize)
        DUMP64(maxStorageBufferBindingSize)
        DUMP32(minUniformBufferOffsetAlignment)
        DUMP32(minStorageBufferOffsetAlignment)
        DUMP32(maxVertexBuffers)
        DUMP32(maxVertexAttributes)
        DUMP32(maxVertexBufferArrayStride)
        DUMP32(maxInterStageShaderComponents)
        DUMP32(maxComputeWorkgroupStorageSize)
        DUMP32(maxComputeInvocationsPerWorkgroup)
        DUMP32(maxComputeWorkgroupSizeX)
        DUMP32(maxComputeWorkgroupSizeY)
        DUMP32(maxComputeWorkgroupSizeZ)
        DUMP32(maxComputeWorkgroupsPerDimension)
        #undef DUMP64
        #undef DUMP32
        #endif

	{
		const float min_angle = 20.0f;
		rstate.path.dot_threshold = cosf(PI + ((min_angle * (1.0f/360.0f)) * PI2));
	}

	font_init(&rstate.font_monospace, fontdata_mono, 0);
	font_init(&rstate.font_variable, fontdata_variable, 0);

	rstate.postproc.type = PP_GAUSS;

	{
		// prepare "static quads index buffer"; see
		// STATIC_QUADS_IDXBUF_SZ comment
		uint16_t indices[STATIC_QUADS_IDXBUF_SZ];
		size_t size = sizeof(indices[0])*STATIC_QUADS_IDXBUF_SZ;
		rstate.static_quad_idxbuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
			.size = size,
		});
		assert(rstate.static_quad_idxbuf);
		int offset = 0;
		for (int i = 0; i < STATIC_QUADS_IDXBUF_SZ; i += 6, offset += 4) {
			assert(0 <= offset && offset <= (65536-4));
			indices[i+0] = offset+0;
			indices[i+1] = offset+1;
			indices[i+2] = offset+2;
			indices[i+3] = offset+0;
			indices[i+4] = offset+2;
			indices[i+5] = offset+3;
		}
		wgpuQueueWriteBuffer(queue, rstate.static_quad_idxbuf, 0, indices, size);
	}

	// prepare dynamic vertex buffer
	rstate.vtxbuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
		.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
		.size = sizeof(rstate.vtxbuf_data),
	});
	assert(rstate.vtxbuf);
	//printf("vtxbuf is %zd bytes\n", sizeof(rstatetxbuf_data));

	// Pre-multiplied alpha:
	//   output = src + (1 - src.alpha) * dst
	// For additive blending, set alpha to zero, and color as-is
	// For alpha blending, set color to color*alpha, and alpha as-is
	const WGPUBlendState* premultiplied_alpha_blend_state = &(WGPUBlendState) {
		.color = (WGPUBlendComponent){
			.srcFactor = WGPUBlendFactor_One,
			.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
			.operation = WGPUBlendOperation_Add,
		},
		.alpha = (WGPUBlendComponent){
			.srcFactor = WGPUBlendFactor_One,
			.dstFactor = WGPUBlendFactor_Zero,
			.operation = WGPUBlendOperation_Add,
		}
	};

	// R_*
	{
		rstate.draw_unibuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
			.size = sizeof(struct draw_uni),
		});
		assert(rstate.draw_unibuf);
	}

	// pattern stuff
	{
		rstate.pattern_unibuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
			.size = sizeof(struct pattern_uni),
		});
		assert(rstate.pattern_unibuf);

		rstate.pattern_bind_group_layout = wgpuDeviceCreateBindGroupLayout(
			device,
			&(WGPUBindGroupLayoutDescriptor){
				.entryCount = 3,
				.entries = (WGPUBindGroupLayoutEntry[]){
					(WGPUBindGroupLayoutEntry){
						.binding = 0,
						.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
						.buffer = (WGPUBufferBindingLayout){
							.type = WGPUBufferBindingType_Uniform,
							.hasDynamicOffset = false,
							.minBindingSize = sizeof(struct pattern_uni),
						},
					},
					(WGPUBindGroupLayoutEntry){
						.binding = 1,
						.visibility = WGPUShaderStage_Fragment,
						.texture = (WGPUTextureBindingLayout){
							.sampleType = WGPUTextureSampleType_Float,
							.viewDimension = WGPUTextureViewDimension_2D,
							.multisampled = false,
						},
					},
					(WGPUBindGroupLayoutEntry){
						.binding = 2,
						.visibility = WGPUShaderStage_Fragment,
						.sampler = (WGPUSamplerBindingLayout){
							.type = WGPUSamplerBindingType_Filtering,
						},
					},
				},
			}
		);
		assert(rstate.pattern_bind_group_layout);

		rstate.pattern_sampler = wgpuDeviceCreateSampler(rstate.device, &(WGPUSamplerDescriptor) {
			.addressModeU = WGPUAddressMode_Repeat,
			.addressModeV = WGPUAddressMode_Repeat,
			.addressModeW = WGPUAddressMode_Repeat,
			.magFilter = WGPUFilterMode_Linear,
			.minFilter = WGPUFilterMode_Linear,
			.mipmapFilter = WGPUMipmapFilterMode_Nearest,
			.lodMinClamp = 0.0f,
			.lodMaxClamp = 0.0f,
		});
		assert(rstate.pattern_sampler);
	}

	// atlas stuff
	{
		struct tile_atlas* a = &rstate.tile_atlas;

		a->bitmap = calloc(ATLAS_WIDTH*ATLAS_HEIGHT, sizeof *a->bitmap);

		a->texture = wgpuDeviceCreateTexture(
			rstate.device,
			&(WGPUTextureDescriptor) {
				.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
				.dimension = WGPUTextureDimension_2D,
				.size = (WGPUExtent3D){
					.width = ATLAS_WIDTH,
					.height = ATLAS_HEIGHT,
					.depthOrArrayLayers = 1,
				},
				.mipLevelCount = 1,
				.sampleCount = 1,
				.format = WGPUTextureFormat_R8Unorm,
			}
		);
		assert(a->texture);

		a->texture_view = wgpuTextureCreateView(a->texture, &(WGPUTextureViewDescriptor) {});
		assert(a->texture_view);

	}

	// R_MODE_TILE / R_MODE_TILEPTN
	{
		const size_t unisz = sizeof(struct draw_uni);

		WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(
			device,
			&(WGPUBindGroupLayoutDescriptor){
				.entryCount = 2,
				.entries = (WGPUBindGroupLayoutEntry[]){
					(WGPUBindGroupLayoutEntry){
						.binding = 0,
						.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
						.buffer = (WGPUBufferBindingLayout){
							.type = WGPUBufferBindingType_Uniform,
							.hasDynamicOffset = false,
							.minBindingSize = unisz,
						},
					},
					(WGPUBindGroupLayoutEntry){
						.binding = 1,
						.visibility = WGPUShaderStage_Fragment,
						.texture = (WGPUTextureBindingLayout){
							.sampleType = WGPUTextureSampleType_Float,
							.viewDimension = WGPUTextureViewDimension_2D,
							.multisampled = false,
						},
					},
				},
			}
		);
		assert(bind_group_layout);

		rstate.tile_bind_group = wgpuDeviceCreateBindGroup(device, &(WGPUBindGroupDescriptor){
			.layout = bind_group_layout,
			.entryCount = 2,
			.entries = (WGPUBindGroupEntry[]){
				(WGPUBindGroupEntry){
					.binding = 0,
					.buffer = rstate.draw_unibuf,
					.offset = 0,
					.size = unisz,
				},
				(WGPUBindGroupEntry){
					.binding = 1,
					.textureView = rstate.tile_atlas.texture_view,
				},
			},
		});
		assert(rstate.tile_bind_group);

		{
			WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
				device,
				&(WGPUPipelineLayoutDescriptor){
					.bindGroupLayoutCount = 1,
					.bindGroupLayouts = (WGPUBindGroupLayout[]){
						bind_group_layout,
					},
				}
			);
			assert(pipeline_layout);

			WGPUShaderModule shader = mk_shader_module(device, shadersrc_tile);
			rstate.tile_pipeline = wgpuDeviceCreateRenderPipeline(
				device,
				&(WGPURenderPipelineDescriptor){
					.layout = pipeline_layout,
					.vertex = (WGPUVertexState){
						.module = shader,
						.entryPoint = "vs_main",
						.bufferCount = 1,
						.buffers = (WGPUVertexBufferLayout[]){
							(WGPUVertexBufferLayout){
								.arrayStride = sizeof(struct tile_vtx),
								.stepMode = WGPUVertexStepMode_Vertex,
								.attributeCount = 3,
								.attributes = (WGPUVertexAttribute[]) {
									(WGPUVertexAttribute){
										.format = WGPUVertexFormat_Float32x2,
										.offset = (uint64_t)MEMBER_OFFSET(struct tile_vtx, a_pos),
										.shaderLocation = 0,
									},
									(WGPUVertexAttribute){
										.format = WGPUVertexFormat_Float32x2,
										.offset = (uint64_t)MEMBER_OFFSET(struct tile_vtx, a_uv),
										.shaderLocation = 1,
									},
									(WGPUVertexAttribute){
										.format = WGPUVertexFormat_Unorm16x4,
										.offset = (uint64_t)MEMBER_OFFSET(struct tile_vtx, a_color),
										.shaderLocation = 2,
									},
								},
							},
						},
					},
					.primitive = (WGPUPrimitiveState){
						.topology = WGPUPrimitiveTopology_TriangleList,
						.frontFace = WGPUFrontFace_CCW,
						.cullMode = WGPUCullMode_None
					},
					.multisample = (WGPUMultisampleState){
						.count = 1,
						.mask = ~0,
						.alphaToCoverageEnabled = false,
					},
					.fragment = &(WGPUFragmentState){
						.module = shader,
						.entryPoint = "fs_main",
						.targetCount = 1,
						.targets = &(WGPUColorTargetState){
							.format = gpudl_get_preferred_swap_chain_texture_format(),
							.blend = premultiplied_alpha_blend_state,
							.writeMask = WGPUColorWriteMask_All
						},
					},
				}
			);
			assert(rstate.tile_pipeline);
		}

		{
			WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
				device,
				&(WGPUPipelineLayoutDescriptor){
					.bindGroupLayoutCount = 2,
					.bindGroupLayouts = (WGPUBindGroupLayout[]){
						bind_group_layout,
						rstate.pattern_bind_group_layout,
					},
				}
			);
			assert(pipeline_layout);

			WGPUShaderModule shader = mk_shader_module(device, shadersrc_tileptn);
			rstate.tileptn_pipeline = wgpuDeviceCreateRenderPipeline(
				device,
				&(WGPURenderPipelineDescriptor){
					.layout = pipeline_layout,
					.vertex = (WGPUVertexState){
						.module = shader,
						.entryPoint = "vs_main",
						.bufferCount = 1,
						.buffers = (WGPUVertexBufferLayout[]){
							(WGPUVertexBufferLayout){
								.arrayStride = sizeof(struct tile_vtx),
								.stepMode = WGPUVertexStepMode_Vertex,
								.attributeCount = 3,
								.attributes = (WGPUVertexAttribute[]) {
									(WGPUVertexAttribute){
										.format = WGPUVertexFormat_Float32x2,
										.offset = (uint64_t)MEMBER_OFFSET(struct tile_vtx, a_pos),
										.shaderLocation = 0,
									},
									(WGPUVertexAttribute){
										.format = WGPUVertexFormat_Float32x2,
										.offset = (uint64_t)MEMBER_OFFSET(struct tile_vtx, a_uv),
										.shaderLocation = 1,
									},
									(WGPUVertexAttribute){
										.format = WGPUVertexFormat_Unorm16x4,
										.offset = (uint64_t)MEMBER_OFFSET(struct tile_vtx, a_color),
										.shaderLocation = 2,
									},
								},
							},
						},
					},
					.primitive = (WGPUPrimitiveState){
						.topology = WGPUPrimitiveTopology_TriangleList,
						.frontFace = WGPUFrontFace_CCW,
						.cullMode = WGPUCullMode_None
					},
					.multisample = (WGPUMultisampleState){
						.count = 1,
						.mask = ~0,
						.alphaToCoverageEnabled = false,
					},
					.fragment = &(WGPUFragmentState){
						.module = shader,
						.entryPoint = "fs_main",
						.targetCount = 1,
						.targets = &(WGPUColorTargetState){
							.format = gpudl_get_preferred_swap_chain_texture_format(),
							.blend = premultiplied_alpha_blend_state,
							.writeMask = WGPUColorWriteMask_All
						},
					},
				}
			);
			assert(rstate.tileptn_pipeline);
		}
	}

	// R_MODE_VECTOR
	{
		const size_t unisz = sizeof(struct draw_uni);

		WGPUShaderModule shader = mk_shader_module(device, shadersrc_vector);

		WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(
			device,
			&(WGPUBindGroupLayoutDescriptor){
				.entryCount = 1,
				.entries = (WGPUBindGroupLayoutEntry[]){
					(WGPUBindGroupLayoutEntry){
						.binding = 0,
						.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
						.buffer = (WGPUBufferBindingLayout){
							.type = WGPUBufferBindingType_Uniform,
							.hasDynamicOffset = false,
							.minBindingSize = unisz,
						},
					},
				},
			}
		);
		assert(bind_group_layout);

		rstate.vector_bind_group = wgpuDeviceCreateBindGroup(device, &(WGPUBindGroupDescriptor){
			.layout = bind_group_layout,
			.entryCount = 1,
			.entries = (WGPUBindGroupEntry[]){
				(WGPUBindGroupEntry){
					.binding = 0,
					.buffer = rstate.draw_unibuf,
					.offset = 0,
					.size = unisz,
				},
			},
		});

		WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
			device,
			&(WGPUPipelineLayoutDescriptor){
				.bindGroupLayoutCount = 1,
				.bindGroupLayouts = (WGPUBindGroupLayout[]){
					bind_group_layout,
				},
			}
		);
		assert(pipeline_layout);

		rstate.vector_pipeline = wgpuDeviceCreateRenderPipeline(
			device,
			&(WGPURenderPipelineDescriptor){
				.layout = pipeline_layout,
				.vertex = (WGPUVertexState){
					.module = shader,
					.entryPoint = "vs_main",
					.bufferCount = 1,
					.buffers = (WGPUVertexBufferLayout[]){
						(WGPUVertexBufferLayout){
							.arrayStride = sizeof(struct vector_vtx),
							.stepMode = WGPUVertexStepMode_Vertex,
							.attributeCount = 2,
							.attributes = (WGPUVertexAttribute[]) {
								(WGPUVertexAttribute){
									.format = WGPUVertexFormat_Float32x2,
									.offset = (uint64_t)MEMBER_OFFSET(struct vector_vtx, a_pos),
									.shaderLocation = 0,
								},
								(WGPUVertexAttribute){
									.format = WGPUVertexFormat_Unorm16x4,
									.offset = (uint64_t)MEMBER_OFFSET(struct vector_vtx, a_color),
									.shaderLocation = 1,
								},
							},
						},
					},
				},
				.primitive = (WGPUPrimitiveState){
					.topology = WGPUPrimitiveTopology_TriangleList,
					.frontFace = WGPUFrontFace_CCW,
					.cullMode = WGPUCullMode_None
				},
				.multisample = (WGPUMultisampleState){
					.count = 1,
					.mask = ~0,
					.alphaToCoverageEnabled = false,
				},
				.fragment = &(WGPUFragmentState){
					.module = shader,
					.entryPoint = "fs_main",
					.targetCount = 1,
					.targets = &(WGPUColorTargetState){
						.format = gpudl_get_preferred_swap_chain_texture_format(),
						.blend = premultiplied_alpha_blend_state,
						.writeMask = WGPUColorWriteMask_All
					},
				},
			}
		);
	}

	// PP_GAUSS
	{
		struct postproc* pp = &rstate.postproc;

		const size_t unisz = sizeof(struct ppgauss_uni);

		pp->gauss_unibuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
			.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
			.size = unisz,
		});
		assert(pp->gauss_unibuf);

		WGPUShaderModule shader = mk_shader_module(device, shadersrc_ppgauss);

		pp->gauss_bind_group_layout = wgpuDeviceCreateBindGroupLayout(
			device,
			&(WGPUBindGroupLayoutDescriptor){
				.entryCount = 3,
				.entries = (WGPUBindGroupLayoutEntry[]){
					(WGPUBindGroupLayoutEntry){
						.binding = 0,
						.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
						.buffer = (WGPUBufferBindingLayout){
							.type = WGPUBufferBindingType_Uniform,
							.hasDynamicOffset = false,
							.minBindingSize = unisz,
						},
					},
					(WGPUBindGroupLayoutEntry){
						.binding = 1,
						.visibility = WGPUShaderStage_Fragment,
						.texture = (WGPUTextureBindingLayout){
							.sampleType = WGPUTextureSampleType_Float,
							.viewDimension = WGPUTextureViewDimension_2D,
							.multisampled = false,
						},
					},
					(WGPUBindGroupLayoutEntry){
						.binding = 2,
						.visibility = WGPUShaderStage_Fragment,
						.sampler = (WGPUSamplerBindingLayout){
							.type = WGPUSamplerBindingType_Filtering,
						},
					},
				},
			}
		);
		assert(pp->gauss_bind_group_layout);

		pp->gauss_sampler = wgpuDeviceCreateSampler(rstate.device, &(WGPUSamplerDescriptor) {
			.addressModeU = WGPUAddressMode_ClampToEdge,
			.addressModeV = WGPUAddressMode_ClampToEdge,
			.addressModeW = WGPUAddressMode_ClampToEdge,
			.magFilter = WGPUFilterMode_Linear,
			.minFilter = WGPUFilterMode_Linear,
			.mipmapFilter = WGPUMipmapFilterMode_Nearest,
			.lodMinClamp = 0.0f,
			.lodMaxClamp = 0.0f,
		});
		assert(pp->gauss_sampler);

		WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
			device,
			&(WGPUPipelineLayoutDescriptor){
				.bindGroupLayoutCount = 1,
				.bindGroupLayouts = (WGPUBindGroupLayout[]){
					pp->gauss_bind_group_layout,
				},
			}
		);
		assert(pipeline_layout);

		pp->gauss_pipeline = wgpuDeviceCreateRenderPipeline(
			device,
			&(WGPURenderPipelineDescriptor){
				.layout = pipeline_layout,
				.vertex = (WGPUVertexState){
					.module = shader,
					.entryPoint = "vs_main",
					.bufferCount = 0, // using @builtin(vertex_index)
				},
				.primitive = (WGPUPrimitiveState){
					.topology = WGPUPrimitiveTopology_TriangleList,
					.frontFace = WGPUFrontFace_CCW,
					.cullMode = WGPUCullMode_None
				},
				.multisample = (WGPUMultisampleState){
					.count = 1,
					.mask = ~0,
					.alphaToCoverageEnabled = false,
				},
				.fragment = &(WGPUFragmentState){
					.module = shader,
					.entryPoint = "fs_main",
					.targetCount = 1,
					.targets = &(WGPUColorTargetState){
						.format = gpudl_get_preferred_swap_chain_texture_format(),
						.blend = &(WGPUBlendState){
							.color = (WGPUBlendComponent){
								.srcFactor = WGPUBlendFactor_One,
								.dstFactor = WGPUBlendFactor_Zero,
								.operation = WGPUBlendOperation_Add,
							},
							.alpha = (WGPUBlendComponent){
								.srcFactor = WGPUBlendFactor_One,
								.dstFactor = WGPUBlendFactor_Zero,
								.operation = WGPUBlendOperation_Add,
							}
						},
						.writeMask = WGPUColorWriteMask_All
					},
				},
			}
		);
		assert(pp->gauss_pipeline);
	}
}

enum postproc_type r_get_postproc_type()
{
	return rstate.postproc.type;
}

void r_set_postproc_type(enum postproc_type type)
{
	rstate.postproc.type = type;
}
