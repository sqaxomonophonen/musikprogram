CFLAGS+=-O0 -g
CFLAGS+=-std=gnu99
LDLIBS+=-lX11 -lm

all: mprg

make_embedded_resources_dot_c: make_embedded_resources_dot_c.c
embedded_resources.c embedded_resources.h: make_embedded_resources_dot_c vector.wgsl
	./make_embedded_resources_dot_c
embedded_resources.o: embedded_resources.c
stb_ds.o: stb_ds.c stb_ds.h
gpudl.o: gpudl.c gpudl.h
mprg: mprg.o stb_ds.o gpudl.o embedded_resources.o

clean:
	rm -f mprg *.o embedded_resources.c make_embedded_resources_dot_c
