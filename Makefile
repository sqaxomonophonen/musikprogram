CFLAGS+=-O0 -g
#CFLAGS+=-O2
CFLAGS+=-Wall
CFLAGS+=-std=gnu99
LDLIBS+=-lX11 -lm -ldl

all: mprg

build_embedded_resources_dot_c: build_embedded_resources_dot_c.c
embedded_resources.h : build_embedded_resources_dot_c *.wgsl *.txt
	./build_embedded_resources_dot_c
embedded_resources.o: embedded_resources.h
	$(CC) $(CFLAGS) -c embedded_resources.c
stb_ds.o: stb_ds.c stb_ds.h
stb_rect_pack.o: stb_rect_pack.c stb_rect_pack.h
stb_truetype.o: stb_truetype.c stb_truetype.h
sokol_time.o: sokol_time.c sokol_time.h
gpudl.o: gpudl.c gpudl.h
clip.o: clip.c clip.h common.h
ui.o: ui.c ui.h common.h embedded_resources.h
r.o: r.c r.h
prefs.o: prefs.c prefs.h embedded_resources.h
fs.o: fs.c fs.h
bsm.o: bsm.c bsm.h
mprg.o: mprg.c stb_ds.h sokol_time.h common.h gpudl.h fps.h pwr.h stb_rect_pack.h stb_truetype.h prefs.h
mprg: mprg.o prefs.o fs.o bsm.o clip.o ui.o r.o stb_ds.o sokol_time.o gpudl.o embedded_resources.o stb_rect_pack.o stb_truetype.o

clean:
	rm -f mprg *.o embedded_resources.c build_embedded_resources_dot_c
