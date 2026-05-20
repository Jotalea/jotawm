PREFIX  = /usr
X11INC  = /usr/include
X11LIB  = /usr/lib

CFLAGS  = -O2 -Wall -Wextra -I$(X11INC)
LDFLAGS = -L$(X11LIB) -lX11

CC      = cc

all: jwm jwm-session

jwm: jwm.c jwm.h
	$(CC) $(CFLAGS) -o $@ jwm.c $(LDFLAGS)

jwm-session: jwm-session.c
	$(CC) $(CFLAGS) -o $@ jwm-session.c

clean:
	rm -f jwm jwm-session

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 jwm $(DESTDIR)$(PREFIX)/bin/jwm
	install -m 755 jwm-session $(DESTDIR)$(PREFIX)/bin/jwm-session
	mkdir -p $(DESTDIR)$(PREFIX)/share/xsessions
	install -m 644 jwm.desktop $(DESTDIR)$(PREFIX)/share/xsessions/jwm.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/jwm
	rm -f $(DESTDIR)$(PREFIX)/bin/jwm-session
	rm -f $(DESTDIR)$(PREFIX)/share/xsessions/jwm.desktop

.PHONY: all clean install uninstall
