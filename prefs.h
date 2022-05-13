#ifndef PREFS_H


#define POSTPROC_ENUMS \
	ENUM(NONE) \
	ENUM(GAUSS)


#define TOPLVL_LAYOUT_ENUMS \
	ENUM(FULL_TRACKER) \
	ENUM(FULL_GRAPH) \
	ENUM(X_SPLIT) \
	ENUM(FOCUSED)

enum postproc_enums {
	#define ENUM(x) POSTPROC_ ## x,
	POSTPROC_ENUMS
	#undef ENUM
};

enum toplvl_layout_enums {
	#define ENUM(x) TOPLVL_LAYOUT_ ## x,
	TOPLVL_LAYOUT_ENUMS
	#undef ENUM
};

#define STATES \
	FIELD( toplvl_layout,           E(TOPLVL_LAYOUT_X_SPLIT,TOPLVL_LAYOUT_ENUMS)      ) \
	FIELD( postproc,                E(POSTPROC_GAUSS,POSTPROC_ENUMS)                  ) \
	FIELD( toplvl_x_split,          RR(0.33, 0.0, 1.0)                                ) \
	FIELD( last_folder,             S("~")                                            ) \
	FIELD( run_count,               I(0)                                              )


// XXX how to integrate TILE_GROUPS from r_tile.h? each tile group needs a size
// preference, similar to font size
// NOTE some preferences might be too "complex" and handled separately... such
// as audio interface setup? maybe?
#define PREFERENCES \
	FIELD( font_mono_size,          RI(24, 10, 200)                                   ) \
	FIELD( font_variable_size,      RI(24, 10, 200)                                   ) \
	FIELD( transition_duration,     RR(0.05, 0, 1)                                    )


#define COLORS \
	COLOR( background ) \
	COLOR( color0 )


#define ACTIONS \
	ACTION( next_postproc ) \
	ACTION( open_assets_left ) \
	ACTION( open_assets_right )

struct states {
	#define E(a,b) int
	#define RR(a,b,c) float
	#define S(a) char* // XXX or some string struct?
	#define I(a) int
	#define FIELD(NAME,TYPVAL) TYPVAL NAME;
	STATES
	#undef FIELD
	#undef I
	#undef S
	#undef RR
	#undef E
};


struct preferences {
	#define RR(a,b,c) float
	#define RI(a,b,c) int
	#define FIELD(NAME,TYPVAL) TYPVAL NAME;
	PREFERENCES
	#undef FIELD
	#undef RI
	#undef RR
};

extern struct states states;
extern struct preferences preferences;

void prefs_init();

#define PREFS_H
#endif
