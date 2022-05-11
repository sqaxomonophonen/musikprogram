#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "stb_ds.h"
#include "sokol_time.h"

#include "common.h"
#include "gpudl.h"
#include "fps.h"
#include "pwr.h"
#include "r.h"
#include "ui.h"

#define MAX_WINDOWS (16)

int ptn0;

struct window_graph {
	int id;
	int x;
	int y;
	int is_panning;
	union v2 pan_anchor;
};

struct window {
	int id;
	int width;
	int height;
	struct postproc_window ppw;
	struct ui_window uw;
	struct window_graph graph;
};

struct mprg {
	int n_windows;
	struct window windows[MAX_WINDOWS];
} mprg;

static void new_window()
{
	if (mprg.n_windows >= MAX_WINDOWS) return;
	struct window* w = &mprg.windows[mprg.n_windows++];
	w->id = gpudl_window_open("musikprogram");
}

static void wgpu_native_log_callback(WGPULogLevel level, const char* msg)
{
	const char* lvl =
		level == WGPULogLevel_Error ? "ERROR" :
		level == WGPULogLevel_Warn  ? "WARN"  :
		level == WGPULogLevel_Info  ? "INFO"  :
		level == WGPULogLevel_Debug ? "DEBUG" :
		level == WGPULogLevel_Trace ? "TRACE" :
		"???";
	printf("WGPU NATIVE [%s] :: %s\n", lvl, msg);
}

static void tracker_present(struct window* window)
{
	r_begin(R_MODE_TILE);
	rcol_ygrad(
		v4(0.0, 0.0, 0.2, 1.0),
		v4(0.0, 0.0, 0.05, 1.0)
	);
	rt_clear();
	r_end();

	r_begin(R_MODE_VECTOR);
	const float x = 0.3f;
	rv_tri(
		v2(10, 100), v4(x,x,0,0),
		v2(400, 300), v4(x,0,x,0),
		v2(50, 200), v4(0,x,x,0)
	);
	rv_tri(
		v2(100, 100), v4(0,0,x,0),
		v2(400, 300), v4(0,x,0,0),
		v2(50, 200), v4(x,0,0,0)
	);
	r_end();
}

static void graph_present(struct window* window)
{
	struct window_graph* g = &window->graph;

	if (ui_clicked(GPUDL_BUTTON_RIGHT)) {
		g->is_panning = 1;
		g->pan_anchor = v2_sub(ui_mpos(), v2(g->x, g->y));
	}

	if (g->is_panning) {
		union v2 p = v2_sub(ui_mpos(), g->pan_anchor);
		g->x = p.x;
		g->y = p.y;
		if (!ui_down(GPUDL_BUTTON_RIGHT)) {
			g->is_panning = 0;
		}
	}

	rptn_set(ptn0, g->x, g->y);
	r_begin(R_MODE_TILEPTN);
	rcol_plain(v4(1.0, 1.0, 1.0, 1.0));
	rt_clear();
	r_end();

	r_begin(R_MODE_TILE);
	rcol_plain(v4(0.1, 0.15, 0.1, 0.9));
	rt_quad(g->x, g->y, 128, 64);
	r_end();
}

static void overlay_present(struct window* window)
{
}

static void window_present(struct window* window)
{
	ui_begin(&window->uw);

	const int w = window->width;
	const int h = window->height;
	const int x1 = w/6; // XXX TODO layouting

	ui_enter(0,0,w,h,CLIP);

	ui_enter(0, 0, x1, h, CLIP);
	tracker_present(window);
	ui_leave();

	ui_enter(x1, 0, w-x1, h, CLIP);
	graph_present(window);
	ui_leave();

	ui_enter(0,0,w,h,CLIP);
	overlay_present(window);
	ui_leave();

	ui_leave();

	ui_end();
}

int main(int argc, char** argv)
{
	stm_setup();
	gpudl_init();

	gpudl_set_required_limits(&(WGPULimits){
		.maxBindGroups = 2,
	});

	wgpuSetLogCallback(wgpu_native_log_callback);
	//wgpuSetLogLevel(WGPULogLevel_Trace);
	//wgpuSetLogLevel(WGPULogLevel_Debug);
	//wgpuSetLogLevel(WGPULogLevel_Info);
	wgpuSetLogLevel(WGPULogLevel_Warn);

	new_window();

	{
		WGPUInstance instance;
		WGPUAdapter adapter;
		WGPUDevice device;
		WGPUQueue queue;
		gpudl_get_wgpu(&instance, &adapter, &device, &queue);
		r_init(instance, adapter, device, queue);
	}

	struct PWR pwr = {0};
	struct fps* fps = fps_new(60);

	const int ptn0_sz = 128;
	ptn0 = rptn_new(ptn0_sz, ptn0_sz);

	int iteration = 0;
	while (mprg.n_windows > 0) {
		int on_battery = PWR_on_battery(&pwr);

		struct gpudl_event e;
		while (gpudl_poll_event(&e)) {
			struct window* w = NULL;
			int widx = -1;
			struct ui_window* uw = NULL;

			// find event window
			for (int i = 0; i < mprg.n_windows; i++) {
				struct window* ww = &mprg.windows[i];
				if (ww->id == e.window_id) {
					w = ww;
					widx = i;
					uw = &ww->uw;
					break;
				}
			}
			int do_close = 0;

			switch (e.type) {
			case GPUDL_CLOSE:
				do_close = 1;
				break;
			case GPUDL_KEY:
				if (e.key.pressed) {
					if (e.key.code == '\033') do_close = 1;
					if (e.key.code == 'p') {
						enum postproc_type t = r_get_postproc_type();
						if (t == PP_GAUSS) {
							r_set_postproc_type(PP_NONE);
						} else {
							r_set_postproc_type(PP_GAUSS);
						}
					}

				}
				break;
			case GPUDL_MOTION:
				if (uw) {
					uw->mpos.x = e.motion.x;
					uw->mpos.y = e.motion.y;
				}
				break;
			case GPUDL_BUTTON:
				if (uw) {
					assert(0 <= e.button.which && e.button.which < GPUDL_BUTTON_END);
					struct ui_mbtn* bt = &uw->mbtn[e.button.which];
					const int p = e.button.pressed;
					bt->down = p;
					if (p) bt->clicked++;
					bt->mpos.x = e.button.x; // XXX or only if p?
					bt->mpos.y = e.button.y; // XXX or only if p?
				}
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

		// render all windows
		r_begin_frames();

		if ((iteration & 0) == 0) {
			r_begin_ptn_frame(ptn0);
			ui_enter(0, 0, ptn0_sz, ptn0_sz, CLIP);
			r_begin(R_MODE_TILE);
			rcol_plain(v4(0.0, 0.0, 0.0, 1.0));
			rt_clear();
			const float m = 1;
			rcol_plain(v4(0.05, 0.05, 0.05, 1.0));
			rt_quad(m, m, ptn0_sz-m*2, ptn0_sz-m*2);
			rcol_plain(v4(0.0, 0.0, 0.0, 0.6));
			rt_quad(0, m*3, ptn0_sz, m*2);
			rt_quad(0, ptn0_sz-m*5, ptn0_sz, m*2);
			rt_quad(m*3, 0, m*2, ptn0_sz);
			rt_quad(ptn0_sz-m*5, 0, m*2, ptn0_sz);
			rcol_plain(v4(0.0, 0.0, 0.0, 0.3));
			rt_quad(0, m*7, ptn0_sz, m*2);
			rt_quad(0, ptn0_sz-m*9, ptn0_sz, m*2);
			rt_quad(m*7, 0, m*2, ptn0_sz);
			rt_quad(ptn0_sz-m*9, 0, m*2, ptn0_sz);
			/*
			const int hsz = ptn0_sz/2;
			for (int y = 0; y < 2; y++) {
				const float y0 = y*hsz;
				for (int x = 0; x < 2; x++) {
					const float x0 = x*hsz;
					int o = (x+y)&1;
					const float m = 3.0f;
					const float s = 
					for (int i = 0; i < 3; i++) {
						if (o) {
							//rt_quad(x0+m, y0,
							//rt_quad(x0+m, y0,
							rt_quad(x0+m, y0,
						} else {
							//rt_quad(
						}
					}
				}
			}
			*/
			//rt_quad(0, 0, iteration % 128, (iteration*2) % 128);
			//rcol_plain(v4(1.0, 0.0, 1.0, 1.0));
			//rt_quad(0, 0, iteration % 128, (iteration*2) % 128);
			r_end();
			ui_leave();
			r_end_ptn_frame();
		}

		for (int i = 0; i < mprg.n_windows; i++) {
			struct window* window = &mprg.windows[i];
			WGPUTextureView v = gpudl_render_begin(window->id);
			if (!v) continue;

			gpudl_window_get_size(window->id, &window->width, &window->height);
			r_begin_frame(window->width, window->height, &window->ppw, v);

			window_present(window);

			r_end_frame();

			gpudl_render_end();
		}
		r_end_frames();

		if (fps_frame(fps)) printf("fps: %.2f / battery=%d\n", fps->fps, on_battery);
		iteration++;
	}

	return EXIT_SUCCESS;
}
