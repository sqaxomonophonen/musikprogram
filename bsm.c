#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

#include "bsm.h"

struct bsm {
	int width;
	int height;
	int n;
	size_t cap;
	float* stack;
} bsm;

static int get_n_pixels()
{
	return bsm.width * bsm.height;
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
	return &bsm.stack[(bsm.n-1) * n_pixels];
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
}

void bsm_one()
{
	float* b = push();
	const int n = get_n_pixels();
	for (int i = 0; i < n; i++) *(b++) = 1.0f;
}

void bsm_to8(uint8_t* dst)
{
	float* b = pop();
	const int n = get_n_pixels();
	for (int i = 0; i < n; i++) {
		float x = *(b++);
		if (x < 0.0f) x = 0.0f;
		if (x > 1.0f) x = 1.0f;
		int i = (x * 255.0f);
		*(dst++) = i;
	}
}

int bsm_n()
{
	return bsm.n;
}
