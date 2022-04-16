CFLAGS+=-O0 -g
CFLAGS+=-std=gnu99
LDLIBS+=-lX11 -lm

all: mprg

build_embedded_resources_dot_c: build_embedded_resources_dot_c.c
embedded_resources.c embedded_resources.h: build_embedded_resources_dot_c *.wgsl
	./build_embedded_resources_dot_c
embedded_resources.o: embedded_resources.c
stb_ds.o: stb_ds.c stb_ds.h
gpudl.o: gpudl.c gpudl.h
mprg: mprg.o stb_ds.o gpudl.o embedded_resources.o

clean:
	rm -f mprg *.o embedded_resources.c build_embedded_resources_dot_c
