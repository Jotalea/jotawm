PREFIX  = /usr/local
X11INC  = /usr/include
X11LIB  = /usr/lib

CFLAGS  = -O2 -Wall -Wextra -I$(X11INC)
LDFLAGS = -L$(X11LIB) -lX11

CC      = cc

all: wm

wm: wm.c
	$(CC) $(CFLAGS) -o $@ wm.c $(LDFLAGS)

clean:
	rm -f wm

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 wm $(DESTDIR)$(PREFIX)/bin/wm

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wm

.PHONY: all clean install uninstall