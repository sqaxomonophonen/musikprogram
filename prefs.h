#ifndef PREFS_H

#include "ui.h"
#include "common.h"

#define POSTPROC_ENUMS \
	ENUM(NONE) \
	ENUM(GAUSS)


#define TOPLVL_LAYOUT_ENUMS \
	ENUM(FULL_TRACKER) \
	ENUM(FULL_GRAPH) \
	ENUM(X_SPLIT) \
	ENUM(FOCUSED)

typedef enum {
	#define ENUM(x) POSTPROC_ ## x,
	POSTPROC_ENUMS
	#undef ENUM
} postproc_enum;

typedef enum {
	#define ENUM(x) TOPLVL_LAYOUT_ ## x,
	TOPLVL_LAYOUT_ENUMS
	#undef ENUM
} toplvl_layout_enum;


#define STATE_FIELDS \
	FIELD( toplvl_layout,           toplvl_layout_enum,   TOPLVL_LAYOUT_X_SPLIT       ) \
	FIELD( postproc,                postproc_enum,        POSTPROC_GAUSS              ) \
	FIELD( toplvl_x_split,          float,                0.33                        ) \
	FIELD( run_count,               int,                  0                           )

// TODO
//FIELD( last_folder,             String,               NewString("~")              )

// XXX how to integrate TILE_GROUPS from r_tile.h? each tile group needs a size
// preference, similar to font size
// NOTE some preferences might be too "complex" and handled separately... such
// as audio interface setup? maybe?
#define PREFERENCE_FIELDS \
	FIELD( font_mono_size,          int,      24          ) \
	FIELD( font_variable_size,      int,      24          ) \
	FIELD( transition_duration,     float,    0.05        )


#define ACTIONS \
	ACTION( next_postproc ) \
	ACTION( next_colorscheme ) \
	ACTION( open_assets_left ) \
	ACTION( open_assets_right )


#define COLORS \
	COLOR( background ) \
	COLOR( color0 )


struct states {
	#define FIELD(NAME,TYPE,DEFAULT) TYPE NAME;
	STATE_FIELDS
	#undef FIELD
};

struct preferences {
	#define FIELD(NAME,TYPE,DEFAULT) TYPE NAME;
	PREFERENCE_FIELDS
	#undef FIELD
};

enum action {
	#define ACTION(NAME) ACTION_ ## NAME,
	ACTIONS
	#undef ACTION
	ACTION_END
};

struct keymap {
	#define ACTION(NAME) struct ui_keyseq NAME[2];
	ACTIONS
	#undef ACTION
};

struct colorscheme {
	#define COLOR(NAME) union v4 NAME;
	COLORS
	#undef COLOR
};

extern struct states states;
extern struct preferences preferences;
extern struct keymap keymap;
extern struct colorscheme colorscheme;

void prefs_init();

#define PREFS_H
#endif
