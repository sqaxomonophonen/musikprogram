#ifndef PWR_H

#include "common.h"

struct PWR {
	uint64_t t0;
	int on_battery;
};

static int PWR_on_battery(struct PWR* pwr)
{
	uint64_t t1 = stm_now();
	double elapsed = stm_sec(stm_diff(t1, pwr->t0));
	if (pwr->t0 == 0 || elapsed > 0.5) {
		pwr->t0 = t1;
		ASSERT_LINUX
		FILE* f = fopen("/sys/class/power_supply/AC/online", "r");
		if (f) {
			pwr->on_battery = (fgetc(f) == '0');
			fclose(f);
		}
	}
	return pwr->on_battery;
}

#define PWR_H
#endif
