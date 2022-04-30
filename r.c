#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "stb_rect_pack.h"
#include "stb_truetype.h"
#include "sokol_time.h"

#include "embedded_resources.h"
#include "r.h"
#include "stb_ds.h"

// couldn't convince myself that dynamic atlas resizing is worth the trouble :)
// having to flush early when the atlas overflows is bad enough, but dynamic
// resizing requires HEURISTICS AND SHIT!
#define ATLAS_WIDTH  (2048)
#define ATLAS_HEIGHT (2048)

#define FRAMEBUFFER_DEFAULT_TEXTURE_FORMAT (WGPUTextureFormat_BGRA8UnormSrgb)

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

union c16 {
	uint16_t c[4];
	union {
		uint16_t r,g,b,a;
	};
};

static inline uint16_t fto16(float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	return roundf(v * 65535.0f);
}

static inline void c16pak(union c16* c, union v4* rgba)
{
	const float s = 1.0f / MAX_INTENSITY;
	for (int i = 0; i < 3; i++) c->c[i] = fto16(rgba->s[i] * s);
	c->c[3] = fto16(rgba->s[3]);
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

struct vector_vtx {
	union v2 a_pos;
	union c16 a_color;
};

struct tile_atlas {
	uint8_t*            bitmap;
	WGPUTexture         texture;
	WGPUTextureView     texture_view;
	WGPUBindGroup       bind_group;
	WGPURenderPipeline  pipeline;

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

struct r {
	WGPUInstance instance;
	WGPUAdapter adapter;
	WGPUDevice device;
	WGPUQueue queue;

	WGPUBuffer static_quad_idxbuf;
	WGPUBuffer vtxbuf;

	WGPUBuffer          draw_unibuf;

	struct tile_atlas   tile_atlas;

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
	//struct window* window;
	WGPUTextureView render_target_texture_view;
	int cursor0;
	int n_static_quad_indices;
	WGPUCommandEncoder encoder;
	WGPURenderPipeline pipeline;
	WGPUBindGroup bind_group;
	float seed;

	union c16 color0, color1, color2, color3;

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
	assert(shader);
	return shader;
}

static WGPUTextureView postproc_begin_frame(int width, int height, struct postproc_window* ppw, WGPUTextureView swap_chain_texture_view)
{
	struct postproc* pp = &rstate.postproc;
	//struct postproc_window* ppw = &window->ppw;
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
					.format = FRAMEBUFFER_DEFAULT_TEXTURE_FORMAT,
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
			.rstep = 17.31f,
			.broken = 0.1f,
			.intensity = 0.7f,
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

			pv[0].a_uv = v2(u,   v);
			pv[1].a_uv = v2(u+w, v);
			pv[2].a_uv = v2(u+w, v+h);
			pv[3].a_uv = v2(u,   v+h);
		}
		pv += 4;
	}
	return n_missing;
}

static void r_flush_submit(int n_bytes, int is_end_frame)
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

	if (is_end_frame) {
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
	int do_submit = is_end_frame || is_vtxbuf_overflow;

	if (r->encoder == NULL) {
		r->encoder = wgpuDeviceCreateCommandEncoder(r->device, &(WGPUCommandEncoderDescriptor){});
		assert(r->encoder != NULL);
	}

	if (do_pass) {
		assert(r->mode > 0);
		assert(!is_end_frame && "not expecting to emit render passes during ''end frame''");

		if (r->mode == R_MODE_TILE) {
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

		assert(r->bind_group != NULL);
		wgpuRenderPassEncoderSetBindGroup(pass, 0, r->bind_group, 0, 0);

		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, r->vtxbuf, r->cursor0, n_bytes);

		wgpuRenderPassEncoderSetIndexBuffer(
			pass,
			r->static_quad_idxbuf,
			WGPUIndexFormat_Uint16,
			0,
			sizeof(uint16_t)*r->n_static_quad_indices);
		wgpuRenderPassEncoderDrawIndexed(pass, r->n_static_quad_indices, 1, 0, 0, 0);

		wgpuRenderPassEncoderEnd(pass);
		r->n_passes++;
		r->cursor0 = r->vtxbuf_cursor;
		r->n_static_quad_indices = 0;
	}

	if (do_submit) {
		r_flush_submit(r->vtxbuf_cursor, is_end_frame);
	}
}

void r_begin_frames()
{
	struct r* r = &rstate;
	assert((!r->begun_frames) && "already inside r_begin_frames()");
	r->begun_frames = 1;
}

void r_end_frames()
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

void r_begin_frame(int width, int height, struct postproc_window* ppw, WGPUTextureView swap_chain_texture_view)
{
	struct r* r = &rstate;

	assert((r->begun_frames) && "not inside r_begin_frames()");
	assert((!r->begun_frame) && "already inside r_begin_frame()");

	assert(r->vtxbuf_cursor == 0);

	r->n_passes = 0;
	r->n_tile_passes = 0;
	assert(r->n_static_quad_indices == 0);
	r->render_target_texture_view = postproc_begin_frame(width, height, ppw, swap_chain_texture_view);

	enum postproc_type t = r->postproc.type;
	const float scalar =
		t == PP_NONE ? MAX_INTENSITY :
		t == PP_GAUSS ? 1.0f :
		0.0f;

	struct draw_uni u = {
		.width = width,
		.height = height,
		.seed = r->seed,
		.scalar = scalar,
	};
	wgpuQueueWriteBuffer(r->queue, r->draw_unibuf, 0, &u, sizeof u);

	r->begun_frame = 1;
}

void r_end_frame()
{
	struct r* r = &rstate;
	assert((r->begun_frame) && "not inside r_begin_frame()");
	assert((r->mode == 0) && "mode not r_end()'d");
	r_flush(FF_END_FRAME);
	r->begun_frame = 0;
}

void r_begin(int mode)
{
	struct r* r = &rstate;
	assert((r->begun_frame) && "not inside r_begin_frame()");
	assert((r->mode == 0) && "already in a mode");

	switch (mode) {
	case R_MODE_TILE: {
		struct tile_atlas* a = &r->tile_atlas;
		r->pipeline = a->pipeline;
		r->bind_group = a->bind_group;
		}; break;
	case R_MODE_VECTOR:
		r->pipeline = r->vector_pipeline;
		r->bind_group = r->vector_bind_group;
		break;
	default: assert(!"unhandled mode");
	}

	r->cursor0 = r->vtxbuf_cursor;
	r->mode = mode;
}

void r_end()
{
	r_flush(FF_END_MODE);
	struct r* r = &rstate;
	assert(r->mode > 0);
	r->mode = 0;
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


void r_color_plain(union v4 color)
{
	struct r* r = &rstate;
	c16pak(&r->color0, &color);
	r->color1 = r->color0;
	r->color2 = r->color0;
	r->color3 = r->color0;
}

void r_color_xgrad(union v4 color0, union v4 color1)
{
	struct r* r = &rstate;
	c16pak(&r->color0, &color0);
	c16pak(&r->color1, &color1);
	r->color2 = r->color1;
	r->color3 = r->color0;
}

void r_color_ygrad(union v4 color0, union v4 color1)
{
	struct r* r = &rstate;
	c16pak(&r->color0, &color0);
	r->color1 = r->color0;
	c16pak(&r->color2, &color1);
	r->color3 = r->color2;
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

static void rt_put(int bank, int size, int code, int x, int y, int w, int h)
{
	assert(rstate.mode == R_MODE_TILE);
	if ((w <= 0) || (h <= 0)) return;

	struct tile_vtx* pv = r_request(4 * sizeof(*pv), 6);

	union v2 dst = v2(x,y);

	union v2 d1 = v2(w,0);
	union v2 d2 = v2(w,h);
	union v2 d3 = v2(0,h);

	// NOTE a_uv is set by r_flush()

	pv[0].a_pos   = dst;
	pv[0].a_color = rstate.color0;

	pv[1].a_pos   = v2_add(dst, d1);
	pv[1].a_color = rstate.color1;

	pv[2].a_pos   = v2_add(dst, d2);
	pv[2].a_color = rstate.color2;

	pv[3].a_pos   = v2_add(dst, d3);
	pv[3].a_color = rstate.color3;

	const int index = ((rstate.vtxbuf_cursor - rstate.cursor0) / (4*sizeof(*pv))) - 1;
	assert(0 <= index && index < MAX_TILE_QUADS);
	rstate.glyphdef_requests[index] = encode_glyphdef(bank, size, code);
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

void rv_quad(float x, float y, float w, float h)
{
	struct r* r = &rstate;
	assert(r->mode == R_MODE_VECTOR);
	struct vector_vtx* pv = r_request(4 * sizeof(*pv), 6);

	pv[0].a_pos.x = x;
	pv[0].a_pos.y = y;
	pv[0].a_color = r->color0;

	pv[1].a_pos.x = x+w;
	pv[1].a_pos.y = y;
	pv[1].a_color = r->color1;

	pv[2].a_pos.x = x+w;
	pv[2].a_pos.y = y+h;
	pv[2].a_color = r->color2;

	pv[3].a_pos.x = x;
	pv[3].a_pos.y = y+h;
	pv[3].a_color = r->color3;
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

	// R_MODE_TILE
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

		const size_t unisz = sizeof(struct draw_uni);

		WGPUShaderModule shader = mk_shader_module(device, shadersrc_tile);

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

		a->bind_group = wgpuDeviceCreateBindGroup(device, &(WGPUBindGroupDescriptor){
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
					.textureView = a->texture_view,
				},
			},
		});
		assert(a->bind_group);

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

		a->pipeline = wgpuDeviceCreateRenderPipeline(
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
						.format = FRAMEBUFFER_DEFAULT_TEXTURE_FORMAT,
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
