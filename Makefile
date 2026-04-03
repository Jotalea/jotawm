PREFIX  = /usr/local
X11INC  = /usr/include
X11LIB  = /usr/lib

CFLAGS  = -O2 -Wall -Wextra -I$(X11INC)
LDFLAGS = -L$(X11LIB) -lX11

CC      = cc

all: jwm

jwm: jwm.c jwm.h
	$(CC) $(CFLAGS) -o $@ jwm.c $(LDFLAGS)

clean:
	rm -f jwm

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 jwm $(DESTDIR)$(PREFIX)/bin/jwm
	mkdir -p $(DESTDIR)$(PREFIX)/share/xsessions
	install -m 644 jwm.desktop $(DESTDIR)$(PREFIX)/share/xsessions/jwm.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/jwm
	rm -f $(DESTDIR)$(PREFIX)/share/xsessions/jwm.desktop

.PHONY: all clean install uninstall
