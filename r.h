#ifndef R_H

#include "common.h"
#include "gpudl.h"

#define MAX_INTENSITY (16)

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

static inline float u16tof(uint16_t v)
{
	return (float)v * (1.0f / 65535.0f);
}

static inline union c16 c16pak(union v4 rgba)
{
	union c16 c;
	const float s = 1.0f / MAX_INTENSITY;
	for (int i = 0; i < 3; i++) c.c[i] = fto16(rgba.s[i] * s);
	c.c[3] = fto16(rgba.s[3]);
	return c;
}

static inline union v4 c16unpak(union c16 c)
{
	return v4(
		u16tof(c.c[0]) * MAX_INTENSITY,
		u16tof(c.c[1]) * MAX_INTENSITY,
		u16tof(c.c[2]) * MAX_INTENSITY,
		u16tof(c.c[3]));
}

enum {
	R_MODE_TILE = 1,
	R_MODE_TILEPTN,
	R_MODE_VECTOR,
	//R_MODE_PLOT,
};

enum postproc_type {
	PP_NONE,
	PP_GAUSS,
};

struct postproc_framebuf {
	WGPUTexture      texture;
	WGPUTextureView  texture_view;
	WGPUBindGroup    bind_group;
};

#define MAX_POSTPROC_FRAMEBUFS (1)

struct postproc_window {
	int width;
	int height;
	WGPUTextureView swap_chain_texture_view;
	struct postproc_framebuf fb[MAX_POSTPROC_FRAMEBUFS];
};

enum r_glyph0 {
	R_FONT_MONOSPACE,
	R_FONT_VARIABLE,
	// ^^^ add more "font banks" here if required
	RT_ONE,
	RT3x3_RBOX_INNER,
	RT3x3_RBOX_BORDER,
	// XXX jeg kunne måske godt tænke mig at vide om det er en RT3x3...
	R_GLYPH0_END
};

struct r_glyph {
	uint16_t glyph0:12; // one of `enum r_glyph0`
	uint16_t cx:2;
	uint16_t cy:2;
	uint16_t px;
	union {
		uint32_t u0; // codepoint when .glyph0 = R_FONT_*
		float f0;
	};
	#if 0
	union {
		uint32_t u1;
		float f1;
	};
	#endif
}; // 12 bytes


void r_init(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device, WGPUQueue queue);
enum postproc_type r_get_postproc_type();
void r_set_postproc_type(enum postproc_type type);

void r_begin_frames(void);
void r_end_frames(void);
void r_begin_frame(int width, int height, struct postproc_window* ppw, WGPUTextureView swap_chain_texture_view);
void r_end_frame(void);
void r_begin_ptn_frame(int pattern);
void r_end_ptn_frame(void);
void r_begin(int mode);
void r_end(void);

int rptn_new(int width, int height);
void rptn_free(int pattern);
void rptn_set(int pattern, int dx, int dy);

void rcol_plain(union v4 color);
void rcol_lgrad(union v4 color0, union v4 color1, union v2 basis);
void rcol_xgrad(union v4 color0, union v4 color1);
void rcol_ygrad(union v4 color0, union v4 color1);

void r_offset(int x0, int y0);
void r_clip(int x, int y, int w, int h);

void r_get_font_v_metrics(enum r_glyph0 font, int px, int* ascent, int* descent);

// R_MODE_VECTOR
void rv_tri(union v2 p0, union v4 c0, union v2 p1, union v4 c1, union v2 p2, union v4 c2);
void rv_quad(float x, float y, float w, float h);

void rv_move_to(float x, float y);
void rv_line_to(float x, float y);
void rv_bezier_to(float cx0, float cy0, float cx1, float cy1, float x, float y);
void rv_stroke(float width, int close);
void rv_fill(void);

// R_MODE_TILE
void rt_font(enum r_glyph0 font, int px);
void rt_goto(int cx, int cy);
void rt_printf(const char* fmt, ...);
void rt_print_codepoint_array(int* codepoints, int n);
void rt_xpos_codepoint_array(int* xpos, int* codepoints, int n); // xpos must be n+1 long
void rt_get_3x3_inner_dim(struct r_glyph g, int* width, int* height);
void rt_3x3(struct r_glyph g, int x, int y, int w, int h);
void rt_quad(float x, float y, float w, float h);
void rt_clear(void);

static inline struct r_glyph rt3x3_rbox_inner(int px)
{
	return (struct r_glyph) {
		.glyph0 = RT3x3_RBOX_INNER,
		.px = px,
	};
}

static inline struct r_glyph rt3x3_rbox_border(int px, float width)
{
	return (struct r_glyph) {
		.glyph0 = RT3x3_RBOX_BORDER,
		.px = px,
		.f0 = width,
	};
}

#define R_H
#endif
