#ifndef BSM_H // bitmap stack machine

#include <stdint.h>

void bsm_begin(int width, int height);
void bsm_tx_3x3(int cx, int cy);
void bsm_one();
void bsm_circle(float r);
void bsm_sub();
void bsm_to8(uint8_t* dst, int stride);
int bsm_n();

#define BSM_H
#endif
