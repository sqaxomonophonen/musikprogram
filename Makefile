CFLAGS+=-O0 -g
CFLAGS+=-Wall
CFLAGS+=-std=gnu99
LDLIBS+=-lX11 -lm -ldl

all: mprg

build_embedded_resources_dot_c: build_embedded_resources_dot_c.c
embedded_resources.c embedded_resources.h: build_embedded_resources_dot_c *.wgsl
	./build_embedded_resources_dot_c
embedded_resources.o: embedded_resources.c
stb_ds.o: stb_ds.c stb_ds.h
stb_rect_pack.o: stb_rect_pack.c stb_rect_pack.h
stb_truetype.o: stb_truetype.c stb_truetype.h
sokol_time.o: sokol_time.c sokol_time.h
gpudl.o: gpudl.c gpudl.h
mprg.o: mprg.c stb_ds.h sokol_time.h common.h gpudl.h fps.h pwr.h stb_rect_pack.h stb_truetype.h embedded_resources.h r_tile.h
mprg: mprg.o stb_ds.o sokol_time.o gpudl.o embedded_resources.o stb_rect_pack.o stb_truetype.o

clean:
	rm -f mprg *.o embedded_resources.c build_embedded_resources_dot_c
