#define WGSLS \
	WGSL("vector.wgsl", "shadersrc_vector")

#define FONTS \
	FONT("Cousine-Regular.ttf", "fontdata_mono") \
	FONT("SourceSansPro-SemiBold.ttf", "fontdata_variable")


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

FILE* c_out;
FILE* h_out;

static uint8_t* read_file(const char* path, size_t* size)
{
	assert(size);
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "%s: no such file\n", path);
		exit(EXIT_FAILURE);
	}
	assert(f);
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* data = malloc(*size);
	assert(data);
	assert(fread(data, *size, 1, f) == 1);
	fclose(f);
	return data;
}


static void add_wgsl(const char* path, const char* symbol)
{
	size_t sz;
	uint8_t* data = read_file(path, &sz);

	fprintf(c_out, "char* %s =\n", symbol);

	int newline = 1;
	char* p = data;
	for (;;) {
		char c = *(p++);
		if (c == 0) break;
		if (newline) {
			fprintf(c_out, "\"");
			newline = 0;
		}
		if (c == '\n') {
			fprintf(c_out, "\\n\"\n");
			newline = 1;
		} else if (c == '\t') {
			fprintf(c_out, "\t");
		} else if (c == '\"') {
			fprintf(c_out, "\\\"");
		} else {
			assert(' ' <= c && c <= '~');
			fprintf(c_out, "%c", c);
		}
	}

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
	char* p = data;
	while (remaining > 0) {
		char c = *(p++);
		char str_add[4];
		int n_add = 0;
		n_add = 4;
		str_add[0] = '\\';
		str_add[1] = 'x';
		str_add[2] = hex_digit((uint8_t)c>>4);
		str_add[3] = hex_digit((uint8_t)c&0xf);
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

	#define WGSL(path,symbol) \
		add_wgsl(path, symbol);
	WGSLS
	#undef WGSL

	#define FONT(path,symbol) \
		add_binary(path,symbol);
	FONTS
	#undef FONT

	fclose(h_out);
	fclose(c_out);

	return EXIT_SUCCESS;
}
