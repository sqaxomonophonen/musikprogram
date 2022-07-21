#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "bsm.h"

struct bsm {
	int width;
	int height;
	int n;
	size_t cap;
	float* stack;
	float x0, y0;
	float xstep, ystep;
} bsm;

static inline int get_n_pixels()
{
	return bsm.width * bsm.height;
}

static float* top()
{
	assert(bsm.n > 0);
	return &bsm.stack[(bsm.n-1) * get_n_pixels()];
}

static float* push()
{
	const int n_pixels = get_n_pixels();
	bsm.n++;
	const size_t req = bsm.n * n_pixels * sizeof(*bsm.stack);
	if (req > bsm.cap) {
		bsm.stack = realloc(bsm.stack, req);
		bsm.cap = req;
		assert(bsm.stack != NULL);
	}
	return top();
}

static float* pop()
{
	bsm.n--;
	assert(bsm.n >= 0);
	return &bsm.stack[(bsm.n) * get_n_pixels()];
}

void bsm_begin(int width, int height)
{
	bsm.width = width;
	bsm.height = height;
	bsm.n = 0;
	bsm.x0 = bsm.y0 = 0.0f;
	bsm.xstep = 1.0f / (float)width;
	bsm.ystep = 1.0f / (float)height;
}

static void handle_component(int component, float sz, float* c0, float* step)
{
	float set_c0;
	float set_step;
	switch (component) {
	case 0:
		set_c0 = -sz;
		set_step = 1.0f;
		break;
	case 1:
		set_c0 = 0.0f;
		set_step = 0.0f;
		break;
	case 2:
		set_c0 = 0.0f;
		set_step = 1.0f;
		break;
	default: assert(!"invalid c-value");
	}
	if (c0) *c0 = set_c0;
	if (step) *step = set_step;
}

void bsm_tx_3x3(int cx, int cy)
{
	handle_component(cx, bsm.width,  &bsm.x0, &bsm.xstep);
	handle_component(cy, bsm.height, &bsm.y0, &bsm.ystep);
}

void bsm_one()
{
	float* b = push();
	const int n = get_n_pixels();
	for (int i = 0; i < n; i++) *(b++) = 1.0f;
}

void bsm_circle(float r)
{
	float* b = push();
	const int w = bsm.width;
	const int h = bsm.height;
	const float xstep = bsm.xstep;
	const float ystep = bsm.ystep;
	const float r2 = r*r;
	float y = bsm.y0;
	for (int py = 0; py < h; py++) {
		float x = bsm.x0;
		for (int px = 0; px < w; px++) {
			const float d2 = x*x + y*y;
			*(b++) = d2 < r2 ? 1.0f : 0.0f;
			x += xstep;
		}
		y += ystep;
	}
}

void bsm_sub()
{
	float* y = pop();
	float* x = top();
	const int n = get_n_pixels();
	for (int i = 0; i < n; i++) *(x++) -= *(y++);
}

void bsm_to8(uint8_t* dst, int stride)
{
	const float* b = pop();
	const int w = bsm.width;
	const int h = bsm.height;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float x = *(b++);
			if (x < 0.0f) x = 0.0f;
			if (x > 1.0f) x = 1.0f;
			int i = (x * 255.0f);
			*(dst++) = i;
		}
		dst += (stride-w);
	}
}

int bsm_n()
{
	return bsm.n;
}
