#ifndef BSM_H // bitmap stack machine

#include <stdint.h>

void bsm_begin(int width, int height);
void bsm_one();
void bsm_to8(uint8_t* dst);
int bsm_n();

#define BSM_H
#endif
