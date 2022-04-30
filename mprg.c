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

#define MAX_WINDOWS (16)

struct window {
	int id;
	int width;
	int height;
	struct postproc_window ppw;
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

int main(int argc, char** argv)
{
	stm_setup();
	gpudl_init();

	wgpuSetLogCallback(wgpu_native_log_callback);
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

	#if 0
	WGPUAdapter adapter;
	WGPUDevice device;
	WGPUQueue queue;
	gpudl_get_wgpu(NULL, &adapter, &device, &queue);
	mprg.r.device = device;
	mprg.r.queue = queue;
	#endif

	struct PWR pwr = {0};
	struct fps* fps = fps_new(60);

	int iteration = 0;
	int imax = MAX_INTENSITY;
	while (mprg.n_windows > 0) {
		int on_battery = PWR_on_battery(&pwr);

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
					if (e.key.code == GK_UP) imax++;
					if (e.key.code == GK_DOWN) imax--;
					if (imax < 0) imax = 0;
					if (imax > MAX_INTENSITY) imax = MAX_INTENSITY;

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
		for (int i = 0; i < mprg.n_windows; i++) {
			struct window* window = &mprg.windows[i];
			WGPUTextureView v = gpudl_render_begin(window->id);
			if (!v) continue;

			gpudl_window_get_size(window->id, &window->width, &window->height);
			r_begin_frame(window->width, window->height, &window->ppw, v);

			r_begin(R_MODE_VECTOR);

			#if 0
			const float m1 = imax;;
			{
				int x0 = 200;
				int inc = 200;
				const float m0 = 1;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m0,m0,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m0,m1,m0,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m0,m0,m1,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m0,m1,m1,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m0,m1,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m1,m0,1), v4(0,0,0,0));
				x0 += inc;
				rv_quad_ygrad(x0, 0, inc/2, window->height, v4(m1,m1,m1,1), v4(0,0,0,0));
				x0 += inc;
			}

			{
				int y = 0;
				int inc = 10;
				while (y < window->height) {
					rv_quad(0, y, window->width, inc/5, v4(0,0,0,0));
					y += inc;
					inc += 10;
				}
			}

			rv_quad_ygrad(80, 0, 5, window->height, v4(m1,m1,m1,1), v4(0,0,0,0));
			#endif

			{
				const float S = 50;
				r_color_ygrad(v4(0.0,0.02,0,0), v4(0.0,0.06,0,0));
				rv_quad(window->width/2-S, 0, S*2, window->height);
				r_color_plain(v4(0,0,0.7,0.0));
				rv_quad(0, window->height/2-S, window->width, S*2);

			}

			r_end();

			r_begin(R_MODE_TILE);

			r_color_plain(v4(0.0, 0.8, 0.0, 1.0));
			rt_font(R_FONT_VARIABLE, 50);
			rt_goto(5, window->height-50);
			rt_printf("hello       / fps=%.2f", fps->fps);

			rt_3x3(T3x3(boxy), 100, 100, 100, 200);



			r_end();

			r_end_frame();

			gpudl_render_end();
		}
		r_end_frames();

		if (fps_frame(fps)) printf("fps: %.2f / battery=%d\n", fps->fps, on_battery);
		iteration++;
	}

	return EXIT_SUCCESS;
}
