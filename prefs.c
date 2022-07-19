#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "prefs.h"
#include "fs.h"
#include "embedded_resources.h"
#include "gpudl.h"

#include "stb_ds.h"

struct states states;
struct preferences preferences;
struct keymap keymap;
struct colorscheme colorscheme;

enum token_type {
	TOKEN_SYMBOL = 1,
	TOKEN_NUMBER,
	TOKEN_STRING,
};

struct token {
	enum token_type t;
	int offset;
	int len;
};

struct loader {
	int more;

	struct token* tokens_arr;

	int f;
	const void* ptr;
	size_t sz;
	const char* p0;
};

static inline int is_symbol_char0(char c)
{
	return
		   ('a' <= c && c <= 'z')
		|| ('A' <= c && c <= 'Z')
		|| c == '_';
}

static inline int is_symbol_char(char c)
{
	return is_symbol_char0(c) || ('0' <= c && c <= '9');
}

static inline int decimal_digit(char c)
{
	if ('0' <= c && c <= '9') {
		return c-'0';
	} else {
		return -1;
	}
}

static inline int hex_digit(char c)
{
	if ('0' <= c && c <= '9') {
		return c-'0';
	} else if ('a' <= c && c <= 'f') {
		return c-'a'+10;
	} else if ('A' <= c && c <= 'F') {
		return c-'A'+10;
	} else {
		return -1;
	}
}

static inline int is_number_char0(char c)
{
	return c=='-' || c=='.' || ('0'<=c && c<='9');
}

static inline int is_whitespace(char c)
{
	return c == ' ' || c == '\t';
}

static inline int is_newline(char c)
{
	return c == '\n' || c == '\r';
}

static int parse_number(const char* p0, const char** p1, double* v)
{
	int points = 0;
	int lexer_error = 0;
	while (p0 < *p1) {
		if (decimal_digit(*p0) >= 0) {
			p0++;
		} else if (*p0 == '.') {
			points++;
			if (points >= 2) {
				lexer_error = 1;
				break;
			}
			p0++;
		} else if (is_whitespace(*p0) || is_newline(*p0)) {
			break;
		} else {
			lexer_error = 1;
			break;
		}
	}
	if (lexer_error) while (p0 < *p1 && !is_newline(*p0)) p0++;
	*p1 = p0;
	return !lexer_error;
}

static int parse_string(const char* p0, const char** p1, char* buf, size_t bufsz)
{
	assert(*p0 == '"');
	p0++;
	int lexer_error = 1; // until terminated properly
	int escape = 0;
	int hexscape = 0;
	char hex = 0;
	int bufn = 0;
	while (p0 < *p1) {
		char c = *(p0++);
		char s = 0;
		if (hexscape) {
			int hd = hex_digit(c);
			if (hd < 0) break;
			assert(0 <= hd && hd < 16);
			if (hexscape == 1) {
				hex = hd << 4;
				hexscape++;
			} else if (hexscape == 2) {
				hex |= hd;
				s = hex;
			} else {
				assert(!"unreachable");
			}
		} else if (escape) {
			escape = 0;
			assert(hexscape == 0);
			int err = 0;
			switch (c) {
			case '\\': s='\\'; break;
			case '"':  s='"';  break;
			case 'n':  s='\n'; break;
			case 'r':  s='\r'; break;
			case 't':  s='\t'; break;
			case 'b':  s='\b'; break;
			case 'x':  hexscape++; break;
			default:   err=1; break;
			}
			if (err) break;
		} else {
			assert(hexscape == 0);
			if (c == '"') {
				lexer_error = 0;
				break;
			} else if (c == '\\') {
				escape = 1;
			} else if ((' ' <= c && c <= '~') || c < 0) {
				s = c;
			} else {
				break;
			}
		}

		if (s != 0 && buf != NULL) {
			buf[bufn++] = s;
			if (bufn >= bufsz) break;
		}
	}

	if (buf != NULL) buf[bufn >= bufsz ? 0 : bufn] = 0;
	if (lexer_error) while (p0 < *p1 && !is_newline(*p0)) p0++;
	*p1 = p0;
	return lexer_error ? -1 : bufn;
}


static void loader_next(struct loader* l)
{
	if (!l->more) return;

	const char* pend = l->ptr + l->sz;
	const char* p0 = l->p0;

	arrsetlen(l->tokens_arr, 0);

	#define SKIP_P0_TO_NEWLINE while (p0 < pend && !is_newline(*p0)) p0++;
	#define YIELD_IF_TOKENS if (arrlen(l->tokens_arr) > 0) break;
	#define SKIP_P0_TO_NEWLINE_AND_YIELD_IF_TOKENS \
		SKIP_P0_TO_NEWLINE \
		YIELD_IF_TOKENS

	while (p0 < pend) {
		while (p0 < pend && is_whitespace(*p0)) p0++;
		if (p0 >= pend) break;

		assert(!is_whitespace(*p0));

		const int offset = p0 - (const char*)l->ptr;

		if (is_symbol_char0(*p0)) {
			const char* p1 = p0;
			while (p1 < pend && is_symbol_char(*p1)) p1++;
			arrput(l->tokens_arr, ((struct token) {
				.t      = TOKEN_SYMBOL,
				.offset = offset,
				.len    = p1 - p0,
			}));
			p0 = p1;
		} else if (is_number_char0(*p0)) {
			const char* p1 = pend;
			if (parse_number(p0, &p1, NULL)) {
				arrput(l->tokens_arr, ((struct token) {
					.t      = TOKEN_NUMBER,
					.offset = offset,
					.len    = p1 - p0,
				}));
			} else {
				printf("TODO: number lexer error\n");
			}
			p0 = p1;
		} else if (*p0 == '"') {
			const char* p1 = pend;
			if (parse_string(p0, &p1, NULL, 0) >= 0) {
				arrput(l->tokens_arr, ((struct token) {
					.t      = TOKEN_STRING,
					.offset = offset,
					.len    = p1 - p0,
				}));
			} else {
				printf("TODO: string lexer error\n");
			}
			p0 = p1;
		} else if (is_newline(*p0)) {
			YIELD_IF_TOKENS
			p0++;
		} else if (*p0 == '#') {
			SKIP_P0_TO_NEWLINE_AND_YIELD_IF_TOKENS
		} else {
			printf("TODO: lexer error (2)\n");
			SKIP_P0_TO_NEWLINE_AND_YIELD_IF_TOKENS
		}
	}

	#undef SKIP_P0_TO_NEWLINE_AND_YIELD_IF_TOKENS
	#undef YIELD_IF_TOKENS
	#undef SKIP_P0_TO_NEWLINE

	l->more = arrlen(l->tokens_arr);
	l->p0 = p0;

	if (!l->more) {
		if (l->f >= 0) {
			fs_readonly_unmap(l->f);
			l->f = -1;
		}
	}
}

static int loader_key(struct loader* l, const char* k)
{
	if (arrlen(l->tokens_arr) < 1 || l->tokens_arr[0].t != TOKEN_SYMBOL) return 0;
	return memcmp(k, l->ptr + l->tokens_arr[0].offset, l->tokens_arr[0].len) == 0;
}

static void postinit_loader(struct loader* l)
{
	l->p0 = l->ptr;
	l->more = 1;
	loader_next(l);
}

static struct loader file_loader(const char* path)
{
	struct loader l = {0};
	l.f = fs_readonly_map(path, &l.ptr, &l.sz);
	postinit_loader(&l);
	return l;
}

static struct loader cstr_loader(const char* cstr)
{
	struct loader l = {0};
	l.f = -1;
	l.ptr = cstr;
	l.sz = strlen(cstr);
	postinit_loader(&l);
	return l;
}

#define NO_VALUE  (0)
#define OK_VALUE  (1)
#define BAD_VALUE (2)

static int token_get_double(struct loader* l, struct token* tok, double* v)
{
	if (tok->t != TOKEN_NUMBER) return BAD_VALUE;
	char buf[65536];
	if (tok->len >= sizeof(buf)) return BAD_VALUE;
	memcpy(buf, l->ptr + tok->offset, tok->len);
	buf[tok->len] = 0;
	*v = strtod(buf, NULL);
	return OK_VALUE;
}

static int token_get_string(struct loader* l, struct token* tok, char* buf, size_t bufsz)
{
	if (tok->t != TOKEN_STRING) return BAD_VALUE;
	const char* p0 = l->ptr + tok->offset;
	const char* p1 = p0 + tok->len;
	assert(parse_string(p0, &p1, buf, bufsz) >= 0);
	assert(p1 == p0 + tok->len);
	return OK_VALUE;
}

static int load_double(struct loader* l, double* v)
{
	if (arrlen(l->tokens_arr) != 2) return BAD_VALUE;
	return token_get_double(l, &l->tokens_arr[1], v);
}

static int load_int(struct loader* l, int* v)
{
	double d;
	int r = load_double(l, &d);
	*v = d;
	return r;
}

static int load_float(struct loader* l, float* v)
{
	double d;
	int r = load_double(l, &d);
	*v = d;
	return r;
}

static int load_v4(struct loader* l, union v4* v)
{
	if (arrlen(l->tokens_arr) != 5) return BAD_VALUE;
	for (int i = 0; i < 4; i++) {
		double d;
		int r = token_get_double(l, &l->tokens_arr[1+i], &d);
		if (r == BAD_VALUE) return r;
		v->s[i] = d;
	}
	return OK_VALUE;
}

static int str2modmask(const char* s)
{
	#define MOD(M) \
		if (strcmp(s, "L" #M) == 0) return (1<<UI_L ## M); \
		if (strcmp(s, "R" #M) == 0) return (1<<UI_R ## M); \
		if (strcmp(s,     #M) == 0) return (1<<UI_L ## M) + (1<<UI_R ## M);
	UI_MODIFIERS
	#undef MOD

	return 0;
}

static int load_shortcutn(struct loader* l, int n_shortcuts, struct ui_keypress *shortcuts)
{
	memset(shortcuts, 0, n_shortcuts * sizeof(*shortcuts));

	int n_tokens = arrlen(l->tokens_arr);
	if (n_tokens < 2) return OK_VALUE;

	int sci = 0;
	struct ui_keypress *sc = shortcuts;

	for (int i = 1; i < n_tokens; i++) {
		struct token* tok = &l->tokens_arr[i];
		char buf[1024];
		int r = token_get_string(l, tok, buf, sizeof buf);
		if (r == BAD_VALUE) return BAD_VALUE;
		int len = strlen(buf);
		if (len == 1 && buf[0] < '~') {
			if (sc->keysym) return BAD_VALUE;
			sc->keysym = buf[0];
		} else if (len > 1 && buf[0] < 0) {
			const char* p = buf;
			int n = len;
			sc->keysym = gpudl_utf8_decode(&p, &n);
			if (n != 0) return BAD_VALUE;
		} else if (len == 4 && memcmp(buf, "*OR*", 4) == 0) {
			sci++;
			if (sci >= n_shortcuts) return BAD_VALUE;
			sc++;
			continue;
		} else {
			int modmask = str2modmask(buf);
			if (sc->keysym) return BAD_VALUE; // modifier after key
			if (modmask != 0) {
				sc->modmask |= modmask;
			} else {
				#define GPUDL_KEY(NAME) if (len == strlen(#NAME) && memcmp(buf, #NAME, strlen(#NAME)) == 0) sc->keysym = GK_ ## NAME;
				GPUDL_KEYS
				#undef GPUDL_KEY
			}
		}
	}

	return OK_VALUE;
}

static int load_shortcut2(struct loader* l, struct ui_keypress *shortcut)
{
	return load_shortcutn(l, 2, shortcut);
}

static int load_postproc_enum(struct loader* l, postproc_enum* v)
{
	if (arrlen(l->tokens_arr) != 2) return BAD_VALUE;
	char buf[1024];
	int r = token_get_string(l, &l->tokens_arr[1], buf, sizeof buf);
	if (r == BAD_VALUE) return r;
	#define ENUM(x) if (strcmp(buf, #x) == 0) { *v = POSTPROC_ ## x; /* printf("loaded %s/%d\n", #x, *v); */ return OK_VALUE; }
	POSTPROC_ENUMS
	#undef ENUM
	return BAD_VALUE;
}

static int load_toplvl_layout_enum(struct loader* l, toplvl_layout_enum* v)
{
	if (arrlen(l->tokens_arr) != 2) return BAD_VALUE;
	char buf[1024];
	int r = token_get_string(l, &l->tokens_arr[1], buf, sizeof buf);
	if (r == BAD_VALUE) return r;
	#define ENUM(x) if (strcmp(buf, #x) == 0) { *v = TOPLVL_LAYOUT_ ## x; /* printf("loaded %s/%d\n", #x, *v); */ return OK_VALUE; }
	TOPLVL_LAYOUT_ENUMS
	#undef ENUM
	return BAD_VALUE;
}

static void states_set_defaults()
{
	#define FIELD(NAME,TYPE,DEFAULT) states.NAME = DEFAULT;
	STATE_FIELDS
	#undef FIELD
}

static void preferences_set_defaults()
{
	#define FIELD(NAME,TYPE,DEFAULT) preferences.NAME = DEFAULT;
	PREFERENCE_FIELDS
	#undef FIELD
	#define TG(GROUP,DEFAULT_SZ,DESC) preferences.tgsz_ ## GROUP = DEFAULT_SZ;
	TILE_GROUPS
	#undef TG
}

static void report(int h, struct loader* l)
{
	if (h == OK_VALUE) return;
	char buf[1024];
	snprintf(buf, sizeof buf, "???");
	if (arrlen(l->tokens_arr) >= 1 && l->tokens_arr[0].t == TOKEN_SYMBOL) {
		struct token* tok = &l->tokens_arr[0];
		if (tok->len < sizeof(buf)) {
			memcpy(buf, l->ptr + tok->offset, tok->len);
			buf[tok->len] = 0;
		}
	}
	if (h == NO_VALUE) printf("WARNING: unhandled key: %s\n", buf);
	if (h == BAD_VALUE) printf("WARNING: bad value for key %s\n", buf);
}

static const char* STATES_PATH = "!mprg/states";
static const char* PREFERENCES_PATH = "!mprg/preferences";

static void states_load()
{
	for (struct loader l = file_loader(STATES_PATH); l.more; loader_next(&l)) {
		int h = NO_VALUE;
		#define FIELD(NAME,TYPE,DEFAULT) if (loader_key(&l, #NAME)) h = load_##TYPE(&l, &states.NAME);
		STATE_FIELDS
		#undef FIELD
		report(h, &l);
	}
}

static void preferences_load()
{
	for (struct loader l = file_loader(PREFERENCES_PATH); l.more; loader_next(&l)) {
		int h = NO_VALUE;
		#define FIELD(NAME,TYPE,DEFAULT) if (loader_key(&l, #NAME)) h = load_##TYPE(&l, &preferences.NAME);
		PREFERENCE_FIELDS
		#undef FIELD
		#define TG(GROUP,DEFAULT_SZ,DESC) if (loader_key(&l, "tgsz_" #GROUP)) h = load_int(&l, &preferences.tgsz_ ## GROUP);
		TILE_GROUPS
		#undef TG
		report(h, &l);
	}
}

static void load_keymap(struct keymap* km, struct loader* l)
{
	for (; l->more; loader_next(l)) {
		int h = NO_VALUE;
		#define ACTION(NAME,SCOPE) if (loader_key(l, #NAME)) h = load_shortcut2(l, &km->NAME[0]);
		ACTIONS
		#undef ACTION
		report(h, l);
	}
}

static void load_colorscheme(struct colorscheme* cs, struct loader* l)
{
	for (; l->more; loader_next(l)) {
		int h = NO_VALUE;
		#define COLOR(NAME) if (loader_key(l, #NAME)) h = load_v4(l, &cs->NAME);
		COLORS
		#undef COLOR
		report(h, l);
	}
}

static void keymap_set_defaults()
{
	struct loader l = cstr_loader(default_keymap_us);
	load_keymap(&keymap, &l);
}

static void colorscheme_set_defaults()
{
	struct loader l = cstr_loader(default_colorscheme);
	load_colorscheme(&colorscheme, &l);
}

void prefs_init()
{
	states_set_defaults();
	states_load();
	preferences_set_defaults();
	preferences_load();
	keymap_set_defaults();
	colorscheme_set_defaults();
}

void prefs_save()
{
	// TODO
	//  - should I write config files from scratch, or instead attempt to
	//    patch in changes? (keeping custom stuff as-is)
}

int prefs_get_tile_group_sz(enum r_tile_group tg)
{
	switch (tg) {
	#define TG(GROUP,DEFAULT_SZ,DESC) case RTG_ ## GROUP: return preferences.tgsz_ ## GROUP;
	TILE_GROUPS
	#undef TG
	default: assert(!"invalid tile group");
	}
}
