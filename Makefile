PREFIX ?= /usr/local
OPS = luz-ui.so luz-script.so
BINS = dump-spectrum
CFLAGS = -DGEGL_OP_NO_SOURCE -O2 -fpic -shared -I. -g

all: $(OPS) $(BINS)

dump-spectrum: dump-spectrum.c luz.c
	gcc -O2 -fpic  -I. \
    `pkg-config gegl-0.3 --cflags --libs` -g \
    -o $@ $< luz.c

luz-ui.so: luz-ui.c luz.c
	gcc $(CFLAGS) \
    `pkg-config gegl-0.3 --cflags --libs` \
    -o $@ $< luz.c

luz-script.so: luz-script.c luz.c
	gcc $(CFLAGS) \
    `pkg-config gegl-0.3 --cflags --libs` \
    -o $@ $< luz.c

install: $(OPS)
	for a in $(OPS); do install $$a $(PREFIX)/lib/gegl-0.3/ ;done
uninstall:
	for a in $(OPS); do rm $(PREFIX)/lib/gegl-0.3/$$a ;done
clean:
	rm $(OPS) $(BINS) && :

