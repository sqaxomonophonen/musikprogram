#ifndef COMMON_H

#include <assert.h>
#include <math.h>
#include <stdint.h>

#define PI (3.141592653589793)
#define PI2 (PI*2.0)

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))
#define MEMBER_SIZE(t,m) sizeof(((t *)0)->m)
#define MEMBER_OFFSET(t,m) (void*)((size_t)&(((t *)0)->m))

#define MAX(a,b) ((a)>(b)?(a):(b))

extern int verbosity;
#define VB(x) (verbosity >= (x))
#define VB0 VB(0)
#define VB1 VB(1)
#define VB2 VB(2)
#define VB3 VB(3)
#define VB4 VB(4)
#define VB5 VB(5)
#define VB6 VB(6)

#define TODO assert(!"TODO");

union v2 {
	float s[2];
	struct { float x,y; };
	struct { float u,v; };
};

union v3 {
	float s[3];
	struct { float x,y,z; };
	struct { float r,g,b; };
};

union v4 {
	float s[4];
	struct { float x,y,z,w; };
	struct { float r,g,b,a; };
};

static inline union v2 v2(float x, float y) { return (union v2) {.x=x, .y=y}; }
static inline union v2 v2_add(union v2 a, union v2 b) { return v2(a.x+b.x, a.y+b.y); }
static inline union v2 v2_sub(union v2 a, union v2 b) { return v2(a.x-b.x, a.y-b.y); }
static inline float v2_dot(union v2 a, union v2 b) { return a.x*b.x + a.y*b.y; }
static inline float v2_len(union v2 a) { return sqrtf(v2_dot(a,a)); }
static inline union v2 v2_scale(float scalar, union v2 a) { return v2(a.x*scalar, a.y*scalar); }
static inline union v2 v2_unit(union v2 a) { return v2_scale(1.0f / v2_len(a), a); }
static inline union v2 v2_normal(union v2 a) { return v2(a.y, -a.x); }

static inline union v4 v4(float x, float y, float z, float w) { return (union v4) {.x=x, .y=y, .z=z, .w=w}; }

static inline uint8_t f2u8(float value)
{
	int v = roundf(value * 255.0f);
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}

static inline float u8tof(uint8_t value)
{
	return (float)value * (1.0f / 255.0f);
}


#define COMMON_H
#endif
