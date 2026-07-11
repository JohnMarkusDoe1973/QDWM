PREFIX ?= /usr/local
CFLAGS ?= -Os -pedantic -Wall
LDFLAGS = -lX11

all:
	$(CC) $(CFLAGS) main.c -o qdwm $(LDFLAGS)

install:
	mkdir -p $(PREFIX)/bin
	cp qdwm $(PREFIX)/bin/

uninstall:
	rm -f $(PREFIX)/bin/qdwm

clean:
	rm -f qdwm
