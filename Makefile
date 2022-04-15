CFLAGS+=-O0 -g
CFLAGS+=-std=gnu99
LDLIBS+=-lX11 -lm

all: mprg

stb_ds.o: stb_ds.c stb_ds.h
gpudl.o: gpudl.c gpudl.h
mprg: mprg.o stb_ds.o gpudl.o
