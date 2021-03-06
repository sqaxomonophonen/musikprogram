#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>

#include "stb_ds.h"
#include "sokol_time.h"

#include "common.h"
#include "gpudl.h"
#include "fps.h"
#include "pwr.h"
#include "r.h"
#include "ui.h"
#include "prefs.h"

#define MAX_WINDOWS (16)

int ptn0;
struct ui_style_text_input style_asset_pane_text_input;

struct window_graph {
	int id;
	int x;
	int y;
	int is_panning;
	union v2 pan_anchor;
};

struct toggle {
	int value;
	uint64_t t0;
	int animating;
};

struct asset_pane {
	struct ui_text_input text_input;
};

struct window {
	int id;
	int width;
	int height;
	struct postproc_window ppw;
	struct ui_window uw;
	struct window_graph graph;
	int overlay_assets;
	struct toggle overlay_assets_toggle;
	int grp_tracker, grp_graph, grp_assets_left, grp_assets_right;
	int goto_next_focus;
	struct asset_pane asset_panes[2];
};

struct mprg {
	int n_windows;
	struct window windows[MAX_WINDOWS];
	uint64_t ts;
	int goto_next_postproc;
} mprg;

static void toggle_set(struct toggle* tgl, int value)
{
	value = !!value;
	if (tgl->value != value) {
		tgl->value = value;
		tgl->t0 = mprg.ts;
		tgl->animating = 1;
	}
}

static float toggle_eval(struct toggle* tgl, float duration)
{
	if (!tgl->animating) {
		return tgl->value ? 1.0f : 0.0f;
	} else {
		float x = stm_sec(stm_diff(mprg.ts, tgl->t0)) / duration;
		if (x >= 1.0f) {
			tgl->animating = 0;
			return tgl->value ? 1.0f : 0.0f;
		}
		if (x < 0.0f) x = 0.0f;
		return tgl->value ? x : 1.0f - x;
	}
}

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

	ui_pan(g->x, g->y);

	rptn_set(ptn0, 0, 0);
	r_begin(R_MODE_TILEPTN);
	rcol_plain(v4(1.0, 1.0, 1.0, 1.0));
	rt_clear();
	r_end();

	r_begin(R_MODE_TILE);
	rcol_plain(v4(0.1, 0.15, 0.1, 0.9));
	rt_quad(0, 0, 128, 64);
	r_end();
}

static void asset_pane_present(struct window* window, int right, float x)
{
	struct asset_pane* pane = &window->asset_panes[right ? 1 : 0];

	const int pad0 = 16;

	int w,h;
	ui_dim(&w,&h);

	ui_enter(pad0, pad0, w-pad0*2, h-pad0*2, 0);
	ui_dim(&w,&h);

	r_begin(R_MODE_TILE);
	const float s = 0.01f;
	//rcol_plain(pma_alpha(s,s,s*1.5f,lerp(x, 0, 0.9)));
	rcol_plain(pma_alpha(s,s,s, 0.95));
	rt_3x3(rt3x3_rbox_inner(5), 0,0,w,h);
	r_end();

	const int pad1 = 8;
	ui_enter(pad1, pad1, w-pad1*2, h-pad1*2, 0);
	ui_dim(&w,&h);

	int sig = ui_text_input_handle(&pane->text_input, &style_asset_pane_text_input, 0, w);
	if (sig) {
		ui_text_input_debug(&pane->text_input);
		if (sig == UI_SIGNAL_ESCAPE) {
			window->overlay_assets = 0;
		}
	}

	ui_leave();
	ui_leave();
}

static int overlay_wants_focus(struct window* window)
{
	return !!window->overlay_assets;
}

static float curve_in_sin(float x)
{
	if (x < 0.0f) return 0.0f;
	if (x > 1.0f) return 1.0f;
	return sin(x*PI*0.5f);
}

#if 0
static float curve_out_sin(float x)
{
	return 1.0f - curve_in_sin(1.0f - x);
}
#endif

static void overlay_present(struct window* window)
{
	int w,h;
	ui_dim(&w,&h);

	{ // assets
		toggle_set(&window->overlay_assets_toggle, window->overlay_assets);
		float x = toggle_eval(&window->overlay_assets_toggle, preferences.transition_duration);
		if (x > 0.0f) {
			const int w2 = w/2;

			int left_dx = 0;
			int left_dy = 0;
			int right_dx = 0;
			int right_dy = 0;

			if (x < 1.0f) {
				const float dur = 0.55f;
				const float idur = 1.0f / dur;
				const float x0 = curve_in_sin(clamp(x*idur,0,1));
				const float x1 = curve_in_sin(clamp(x*idur - (idur-1.0f),0,1));
				if (window->overlay_assets == 1) {
					left_dy = (float)h * (1.0f-x0);
					right_dx = (float)w2 * (1.0f-x1);
				} else if (window->overlay_assets == 2) {
					left_dx = (float)w2 * -(1.0f-x1);
					right_dy = (float)h * (1.0f-x0);
				} else {
					left_dy = right_dy = (float)h * -(1.0f-x0);
				}
			}

			// first group entered gets new focus; using this
			// "hack" to allow left/right initial focus
			// (ACTION_open_assets_left vs
			// ACTION_open_assets_right)
			for (int i = 0; i < 2; i++) {
				const int ii = window->overlay_assets == 1 ? i : 1-i;
				if (ii == 0) {
					ui_enter_group(0+left_dx,0+left_dy,w2,h,CLIP,&window->grp_assets_left);
				} else {
					ui_enter_group(w2+right_dx,0+right_dy,w2,h,CLIP,&window->grp_assets_right);
				}
				asset_pane_present(window, ii, x);
				ui_leave();
			}
		}
	}
}

static void execute_action(struct window* window, enum action action)
{
	switch (action) {
	case ACTION_next_postproc:
		mprg.goto_next_postproc = 1;
		break;
	case ACTION_next_colorscheme:
		printf("TODO next colorscheme\n");
		break;
	case ACTION_next_focus:
		window->goto_next_focus = 1;
		break;
	case ACTION_open_assets_left:
		if (window->overlay_assets) {
			window->overlay_assets = 0; // XXX no
		} else {
			window->overlay_assets = 1;
		}
		break;
	case ACTION_open_assets_right:
		if (window->overlay_assets) {
			window->overlay_assets = 0; // XXX no
		} else {
			window->overlay_assets = 2;
		}
		break;
	case ACTION_END: assert(!"XXX");
	}
}

static void handle_actions(struct window* window, int scope_mask)
{
	int action = -1;
	#define ACTION(NAME,SCOPE) \
		if ((SCOPE) & scope_mask) { \
			if (ui_shortcut(keymap.NAME[0])) { \
				action = ACTION_ ## NAME; \
			} \
			if (ui_shortcut(keymap.NAME[1])) { \
				action = ACTION_ ## NAME; \
			} \
		}
	ACTIONS
	#undef ACTION
	if (action >= 0) {
		execute_action(window, action);
	}
}

static void window_present(struct window* window)
{
	ui_begin(&window->uw);

	const int w = window->width;
	const int h = window->height;
	const int x1 = w * states.toplvl_x_split;

	ui_enter(0,0,w,h,0);
	handle_actions(window, SCOPE_TOP);
	ui_leave();

	if (window->goto_next_focus) {
		ui_goto_next_focus();
		window->goto_next_focus = 0;
	}

	ui_enter(0,0,w,h, CLIP | (overlay_wants_focus(window) ? NO_INPUT : 0));

	handle_actions(window, SCOPE_UNDERLAY);

	ui_enter_group(0, 0, x1, h, CLIP, &window->grp_tracker);
	tracker_present(window);
	ui_leave();

	ui_enter_group(x1, 0, w-x1, h, CLIP, &window->grp_graph);
	graph_present(window);
	ui_leave();

	ui_leave();

	ui_enter(0,0,w,h,CLIP);
	overlay_present(window);
	ui_leave();

	ui_end();
}

static void init_styles()
{
	{
		struct ui_style_text_input* st = &style_asset_pane_text_input;
		st->font = R_FONT_VARIABLE;
		st->font_px = 40;
		st->x_padding = 3;
		st->y_padding = 3;
		const int rsz = 8;
		st->bg3x3 = rt3x3_rbox_inner(rsz);
		st->border3x3 = rt3x3_rbox_border(rsz, 1.5f);
	}
}

int main(int argc, char** argv)
{
	prefs_init();

	init_styles();

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
		mprg.ts = stm_now();
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
				ui_window_key_event(uw, &e.key);
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

		if (mprg.goto_next_postproc) {
			mprg.goto_next_postproc = 0;
			enum postproc_type t = r_get_postproc_type();
			if (t == PP_GAUSS) {
				r_set_postproc_type(PP_NONE);
			} else {
				r_set_postproc_type(PP_GAUSS);
			}
		}
	}

	prefs_save();

	return EXIT_SUCCESS;
}
