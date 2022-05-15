#define WGSLS \
	WGSL("tile.wgsl",       "shadersrc_tile"     ) \
	WGSL("tileptn.wgsl",    "shadersrc_tileptn"  ) \
	WGSL("vector.wgsl",     "shadersrc_vector"   ) \
	WGSL("ppgauss.wgsl",    "shadersrc_ppgauss"  )

#define FONTS \
	FONT("Cousine-Regular.ttf",        "fontdata_mono") \
	FONT("SourceSansPro-SemiBold.ttf", "fontdata_variable")

#define TXTS \
	TXT("default_keymap_us.txt",     "default_keymap_us") \
	TXT("default_colorscheme.txt",   "default_colorscheme")

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

FILE* c_out;
FILE* h_out;

static uint8_t* read_file(const char* path, size_t* size)
{
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "%s: no such file\n", path);
		exit(EXIT_FAILURE);
	}
	assert(f);
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* data = malloc(sz+1);
	assert(fread(data, sz, 1, f) == 1);
	data[sz] = 0;
	fclose(f);
	if (size) *size = sz;
	return data;
}

static char** split_lines(char* s)
{
	const int max_lines = 10007;
	char** lines = calloc(max_lines, sizeof(*lines));
	char* p = s;
	int i = 0;
	for (;;) {
		const char* p0 = p;
		while (*p != 0 && *p != '\n') p++;
		const char* p1 = p;
		const int n = p1-p0;
		char* line = malloc(n+1);
		memcpy(line, p0, n);
		line[n] = 0;
		lines[i++] = line;
		assert(i < max_lines);
		if (*p1 == 0) break;
		p++;
	}
	return lines;
}

static char** read_lines(const char* path)
{
	return split_lines((char*)read_file(path, NULL));
}

static void inc_txt(const char* path, int preprocessor)
{
	int line_number = 0;
	for (char** lines = read_lines(path); *lines; lines++) {
		line_number++;
		char* line = *lines;

		// check for #include
		if (preprocessor) {
			char* p = line;
			while (*p != 0 && (*p == ' ' || *p == '\t')) p++;
			if (*p == '#') {
				const char match_include[] = "#include \"";
				if (memcmp(p, match_include, sizeof(match_include)-1) == 0) {
					p += sizeof(match_include)-1;
					const char* p0 = p;
					while (*p != 0 && *p != '\n' && *p != '\"') p++;
					assert((*p == '\"') );
					if (*p != '\"') {
						fprintf(stderr, "#include not terminated at line %d\n", line_number);
						abort();
					}
					const char* p1 = p;
					const int n = p1-p0;
					char* inc_path = malloc(n+1);
					memcpy(inc_path, p0, n);
					inc_path[n] = 0;
					inc_txt(inc_path, preprocessor);
					continue;
				} else {
					fprintf(stderr, "unhandled \"#\"-directive at line %d\n", line_number);
					abort();
				}
			}
		}

		// pass line as-is
		fprintf(c_out, "\"");
		const int n = strlen(line);
		for (int i = 0; i < n; i++) {
			char c = line[i];
			switch (c) {
			case '\t': fputc('\t', c_out); break;
			case '\"': fprintf(c_out, "\\\""); break;
			case '\\': fprintf(c_out, "\\\\"); break;
			default: fputc(c, c_out); break;
			}
		}
		fprintf(c_out, "\\n\"\n");
	}
}

static void add_txt(const char* path, const char* symbol, int preprocessor)
{
	fprintf(c_out, "char* %s =\n", symbol);
	inc_txt(path, preprocessor);
	fprintf(c_out, ";\n\n");

	fprintf(h_out, "extern char* %s;\n", symbol);
}

static void binflush(char* str, int* n)
{
	fputc('"', c_out);
	if (*n > 0) assert(fwrite(str, *n, 1, c_out) == 1);
	fprintf(c_out, "\"\n");
	*n = 0;
}

static char hex_digit(int v)
{
	assert(0x0 <= v && v <= 0xf);
	if (v < 10) {
		return '0'+v;
	} else {
		return 'a'+(v-10);
	}
}

static void add_binary(const char* path, const char* symbol)
{
	size_t sz;
	uint8_t* data = read_file(path, &sz);
	fprintf(c_out, "char %s[] =\n", symbol);

	int n = 0;
	char str[78];
	size_t remaining = sz;
	uint8_t* p = data;
	while (remaining > 0) {
		uint8_t c = *(p++);
		char str_add[4];
		int n_add = 0;
		n_add = 4;
		str_add[0] = '\\';
		str_add[1] = 'x';
		str_add[2] = hex_digit(c>>4);
		str_add[3] = hex_digit(c&0xf);
		if (n+n_add > sizeof(str)) binflush(str, &n);
		assert((n+n_add) <= sizeof(str));
		memcpy(str+n, str_add, n_add);
		n += n_add;
		remaining--;
	}
	binflush(str, &n);
	fprintf(c_out, ";\n\n");

	fprintf(h_out, "extern char %s[];\n", symbol);
}

int main(int argc, char** argv)
{
	c_out = fopen("embedded_resources.c", "w");
	assert(c_out);

	h_out = fopen("embedded_resources.h", "w");
	assert(h_out);

	fprintf(c_out, "// made by `%s`\n\n", argv[0]);
	fprintf(h_out, "// made by `%s`\n\n", argv[0]);

	#define WGSL(path,symbol) add_txt(path, symbol, 1);
	WGSLS
	#undef WGSL

	#define FONT(path,symbol) add_binary(path,symbol);
	FONTS
	#undef FONT

	#define TXT(path,symbol) add_txt(path,symbol, 0);
	TXTS
	#undef TXT

	fclose(h_out);
	fclose(c_out);

	return EXIT_SUCCESS;
}
