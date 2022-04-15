#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "stb_ds.h"
#include "common.h"
#include "gpudl.h"

#define MAX_WINDOWS (16)

struct window {
	int id;
	int width;
	int height;
	WGPUTexture stage;
	WGPUTextureView stage_view;
};

#define ATLAS_VTXBUF_LENGTH  (1<<15)
#define PLOT_VTXBUF_LENGTH   (1<<6)
#define VECTOR_VTXBUF_LENGTH (1<<16)

struct atlas_vertex {
	union v2 a_pos;
	union v2 a_uv;
	uint32_t a_color;
};

struct plot_vertex {
	union v2 a_pos;
	union v2 a_uv;
	float a_threshold;
	uint32_t a_color;
};

struct vector_vertex {
	union v2 a_pos;
	uint32_t a_color;
};

struct mprg {
	int n_windows;
	struct window windows[MAX_WINDOWS];

	WGPUBuffer vtxbuf;
	union {
		struct atlas_vertex   atlas[ATLAS_VTXBUF_LENGTH];
		struct plot_vertex    plot[PLOT_VTXBUF_LENGTH];
		struct vector_vertex  vector[VECTOR_VTXBUF_LENGTH];
	} vtxbuf_data;

	WGPUBuffer unibuf;
} mprg;

static void new_window()
{
	if (mprg.n_windows >= MAX_WINDOWS) return;
	struct window* w = &mprg.windows[mprg.n_windows++];
	w->id = gpudl_window_open("musikprogram");
}

int main(int argc, char** argv)
{
	gpudl_init();

	new_window();

	WGPUAdapter adapter;
	WGPUDevice device;
	WGPUQueue queue;
	gpudl_get_wgpu(NULL, &adapter, &device, &queue);

	//printf("vtxbuf is %zd bytes\n", sizeof(mprg.vtxbuf_data));
	mprg.vtxbuf = wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
		.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
		.size = sizeof(mprg.vtxbuf_data),
	});
	assert(mprg.vtxbuf);


	while (mprg.n_windows > 0) {
		struct gpudl_event e;
		while (gpudl_poll_event(&e)) {
			struct window* w = NULL;
			int widx = -1;

			// find event window
			for (int i = 0; i < mprg.n_windows; i++) {
				struct window* ww = &mprg.windows[i];
				if (ww->id == e.window_id) {
					w = ww;
					widx = i;
					break;
				}
			}
			int do_close = 0;

			switch (e.type) {
			case GPUDL_CLOSE:
				do_close = 1;
				break;
			default:
				break;
			}

			if (do_close && widx >= 0) {
				assert(0 <= widx && widx < mprg.n_windows);
				gpudl_window_close(w->id);
				int n_move = (mprg.n_windows - widx) - 1;
				if (n_move > 0) {
					memmove(&mprg.windows[widx], &mprg.windows[widx+1], n_move * sizeof(mprg.windows[0]));
				}
				mprg.n_windows--;
			}
		}

		for (int i = 0; i < mprg.n_windows; i++) {
			struct window* w = &mprg.windows[i];
			WGPUTextureView v = gpudl_render_begin(w->id);
			if (!v) continue;

			int prev_width = w->width;
			int prev_height = w->height;
			gpudl_window_get_size(w->id, &w->width, &w->height);

			if (w->width != prev_width || w->height != prev_height) {
				if (w->stage != NULL) {
					wgpuTextureDrop(w->stage);
					assert(w->stage_view != NULL);
					wgpuTextureViewDrop(w->stage_view);
				}
				w->stage = wgpuDeviceCreateTexture(device, &(WGPUTextureDescriptor) {
					.usage = WGPUTextureUsage_TextureBinding,
					.dimension = WGPUTextureDimension_2D,
					.size = (WGPUExtent3D){
						.width = w->width,
						.height = w->height,
						.depthOrArrayLayers = 1,
					},
					.mipLevelCount = 1,
					.sampleCount = 1,
					.format = WGPUTextureFormat_R8Uint,
				});
				assert(w->stage);

				w->stage_view = wgpuTextureCreateView(w->stage, &(WGPUTextureViewDescriptor) {});
				assert(w->stage_view);
			}

			gpudl_render_end();
		}
	}

	return EXIT_SUCCESS;
}
