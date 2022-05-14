#include <stdio.h>

#include "prefs.h"
#include "fs.h"

// TODO FILES:
//  ~/.local/share/mprg/
//    /state
//    /prefs
//    /colorschemes/*
//    /keymaps/*

struct states states;
struct preferences preferences;

void prefs_init()
{
	// set defaults for states
	#define E(a,b) a
	#define RR(a,b,c) a
	#define S(a) a
	#define I(a) a
	#define FIELD(NAME,TYPVAL) states.NAME = TYPVAL;
	STATES
	#undef FIELD
	#undef I
	#undef S
	#undef RR
	#undef E

	// set defaults for preferences
	#define RR(a,b,c) a
	#define RI(a,b,c) a
	#define FIELD(NAME,TYPVAL) preferences.NAME = TYPVAL;
	PREFERENCES
	#undef FIELD
	#undef RI
	#undef RR

	{
		void* p;
		size_t sz;
		int f = fs_readonly_map("!mprg/states", &p, &sz);
		if (f >= 0) {
			printf("opened states! (sz=%zd)\n", sz);
			fs_readonly_unmap(f);
		} else {
			printf("no states :-(\n");
		}
	}


	{
		void* p;
		size_t sz;
		int f = fs_readonly_map("!mprg/preferences", &p, &sz);
		if (f >= 0) {
			printf("opened preferences! (sz=%zd)\n", sz);
			fs_readonly_unmap(f);
		} else {
			printf("no preferences :-(\n");
		}
	}
}
