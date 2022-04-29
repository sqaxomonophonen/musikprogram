#ifndef R_H

#include "common.h"
#include "gpudl.h"

#define MAX_INTENSITY (16)

enum {
	R_MODE_TILE = 1,
	R_MODE_PLOT,
	R_MODE_VECTOR,
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

enum r_font {
	R_TILES,
	R_FONT_MONOSPACE,
	R_FONT_VARIABLE,
	R_FONT_END
};

void r_init(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device, WGPUQueue queue);
enum postproc_type r_get_postproc_type();
void r_set_postproc_type(enum postproc_type type);

void r_begin_frames();
void r_end_frames();
void r_begin_frame(int width, int height, struct postproc_window* ppw, WGPUTextureView swap_chain_texture_view);
void r_end_frame();
void r_begin(int mode);
void r_end();

void r_color_plain(union v4 color);

// R_MODE_VECTOR
void rv_quad(float x, float y, float w, float h);

// R_MODE_TILE
void rt_font(enum r_font font, int px);
void rt_goto(int cx, int cy);
void rt_printf(const char* fmt, ...);

#include "r_tile.h"

#define R_H
#endif
