#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "prefs.h"
#include "fs.h"
#include "embedded_resources.h"

struct states states;
struct preferences preferences;
struct keymap keymap;
struct colorscheme colorscheme;

struct loader {
	int more;
	const char* k0;
	const char* k1;
	const char* end;
	char* str;
	size_t strsz;

	int f;
	const void* ptr;
	size_t sz;
	const char* p0;
};

static inline int is_symbol_char(char c)
{
	return
		   ('a' <= c && c <= 'z')
		|| ('A' <= c && c <= 'Z')
		|| ('0' <= c && c <= '9')
		|| c == '_';
}

static inline int is_whitespace(char c)
{
	return c == ' ' || c == '\t';
}

static inline int is_newline(char c)
{
	return c == '\n' || c == '\r';
}


static void loader_next(struct loader* l)
{
	if (l->f < 0) {
		l->more = 0;
		return;
	}

	const char* pend = l->ptr + l->sz;

	while (l->p0 < pend) {
		const char* p0 = l->p0;
		const char* p1 = p0;
		while (p1 < pend && is_symbol_char(*p1)) p1++;
		const char* p2 = p1;
		while (p2 < pend && !is_newline(*p2)) p2++;
		l->p0 = p2+1;

		if (p0 < p1 && p1 < p2 && is_whitespace(*p1)) {
			//printf("proper line [[");
			//fwrite(p0, p2-p0, 1, stdout);
			//printf("]]\n");
			l->k0 = p0;
			l->k1 = p1;
			l->end = p2;
			l->more = 1;
			return;
		}

	}

	if (l->f >= 0) {
		fs_readonly_unmap(l->f);
		l->f = -1;
	}

	if (l->str != NULL) {
		free(l->str);
		l->str = NULL;
	}

	l->more = 0;
}

static int loader_key(struct loader* l, const char* k)
{
	size_t sz0 = strlen(k);
	size_t sz1 = l->k1 - l->k0;
	return (sz0 != sz1) ? 0 : memcmp(l->k0, k, sz0) == 0;
}

static void postinit_loader(struct loader* l)
{
	l->p0 = l->ptr;
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

static int load_double(struct loader* l, double* v)
{
	char buf[65536];
	const char* k1 = l->k1;
	while (is_whitespace(*k1)) k1++;
	const char* end = l->end;
	if (k1 >= end) return BAD_VALUE;
	size_t n = end - k1;
	if (n >= sizeof(buf)) return BAD_VALUE;
	memcpy(buf, k1, n);
	buf[n] = 0;
	*v = strtod(buf, NULL);
	return OK_VALUE;
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
	return BAD_VALUE; // TODO
}

static inline int hexdigit(char c)
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

// string is owned by loader; don't free() it; it is only valid until the next
// load_str() call
static int load_str(struct loader* l, const char** v)
{
	const char* p0 = l->k1;
	while (is_whitespace(*p0)) p0++;
	const char* end = l->end;
	if (p0 >= end) return BAD_VALUE;
	int i = 0;
	char buf[65536];
	if (*(p0++) != '"') return BAD_VALUE;
	if (p0 >= end) return BAD_VALUE;

	int terminated_properly = 0;
	int escape = 0;
	int hexscape = 0;
	char hex = 0;
	while (p0 < end) {
		char c = *(p0++);
		char s = 0;
		if (hexscape) {
			assert(escape);
			int hd = hexdigit(c);
			if (hd < 0) return BAD_VALUE;
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
			assert(hexscape == 0);
			switch (c) {
			case '\\': s='\\'; break;
			case '"':  s='"';  break;
			case 'n':  s='\n'; break;
			case 'r':  s='\r'; break;
			case 't':  s='\t'; break;
			case 'x':  hexscape++; break;
			default: return BAD_VALUE;
			}
		} else {
			assert(hexscape == 0);
			if (c == '"') {
				terminated_properly = 1;
				break;
			} else if (c == '\\') {
				escape = 1;
			} else if (' ' <= c && c <= '~') {
				s = c;
			} else {
				return BAD_VALUE;
			}
		}

		if (s != 0) {
			buf[i++] = s;
			if (i >= sizeof(buf)) return BAD_VALUE;
		}
	}
	if (p0 != end || !terminated_properly || escape) return BAD_VALUE;

	size_t strsz = i+1;
	if (strsz > l->strsz) {
		l->strsz = strsz;
		l->str = realloc(l->str, strsz);
	}
	memcpy(l->str, buf, i);
	l->str[i] = 0;
	*v = l->str;

	return OK_VALUE;
}

static int load_postproc_enum(struct loader* l, postproc_enum* v)
{
	const char* s;
	int r = load_str(l, &s);
	if (r == BAD_VALUE) return r;
	#define ENUM(x) if (strcmp(s, #x) == 0) { *v = POSTPROC_ ## x; /* printf("loaded %s/%d\n", #x, *v); */ return OK_VALUE; }
	POSTPROC_ENUMS
	#undef ENUM
	return BAD_VALUE;
}

static int load_toplvl_layout_enum(struct loader* l, toplvl_layout_enum* v)
{
	const char* s;
	int r = load_str(l, &s);
	if (r == BAD_VALUE) return r;
	#define ENUM(x) if (strcmp(s, #x) == 0) { *v = TOPLVL_LAYOUT_ ## x; /* printf("loaded %s/%d\n", #x, *v); */ return OK_VALUE; }
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
}

static void report(int h, struct loader* l)
{
	if (h == OK_VALUE) return;
	char buf[65536];
	size_t n = l->k1 - l->k0;
	if (n >= sizeof(buf)) {
		printf("WARNING: big key (n=%zd)\n", n);
		return;
	}
	memcpy(buf, l->k0, n);
	buf[n] = 0;
	if (h == NO_VALUE) printf("WARNING: unhandled key: %s\n", buf);
	if (h == BAD_VALUE) printf("WARNING: bad value for key %s\n", buf);
}

static void states_load()
{
	for (struct loader l = file_loader("!mprg/states"); l.more; loader_next(&l)) {
		int h = NO_VALUE;
		#define FIELD(NAME,TYPE,DEFAULT) if (loader_key(&l, #NAME)) h = load_##TYPE(&l, &states.NAME);
		STATE_FIELDS
		#undef FIELD
		report(h, &l);
	}
}

static void preferences_load()
{
	for (struct loader l = file_loader("!mprg/preferences"); l.more; loader_next(&l)) {
		int h = NO_VALUE;
		#define FIELD(NAME,TYPE,DEFAULT) if (loader_key(&l, #NAME)) h = load_##TYPE(&l, &preferences.NAME);
		PREFERENCE_FIELDS
		#undef FIELD
		report(h, &l);
	}
}

static void load_keymap(struct keymap* km, struct loader* l)
{
	for (; l->more; loader_next(l)) {
		int h = NO_VALUE;
		// TODO key sequence loader... it's a variable list of strings,
		// like:
		//   open_assets_left "/" "*OR*" "LSHIFT" "/"
		#define ACTION(NAME)
		ACTIONS
		#undef ACTION
		report(h, l);
	}
}

static void load_colorscheme(struct colorscheme* cs, struct loader* l)
{
	for (; l->more; loader_next(l)) {
		int h = NO_VALUE;
		#define COLOR(NAME) load_v4(l, &cs->NAME);
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
