#ifndef FPS_H

#include "sokol_time.h"

struct fps {
	double fps;
	int frames_per_tick;
	int counter;
	uint64_t t0;
};

static inline struct fps* fps_new(int frames_per_tick)
{
	struct fps* fps = calloc(1, sizeof *fps);
	fps->frames_per_tick = frames_per_tick;
	return fps;
}

static inline int fps_frame(struct fps* fps)
{
	if (((fps->counter++) % fps->frames_per_tick) == 0) {
		uint64_t t1 = stm_now();
		uint64_t dt = stm_diff(t1, fps->t0);
		if (dt > 0) fps->fps = (double)fps->frames_per_tick / stm_sec(dt);
		fps->t0 = t1;
		return 1;
	} else {
		return 0;
	}
}



#define FPS_H
#endif
