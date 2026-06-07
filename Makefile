PREFIX  = /usr
X11INC  = /usr/include
X11LIB  = /usr/lib

CFLAGS  = -O2 -Wall -Wextra -I$(X11INC)
LDFLAGS = -L$(X11LIB) -lX11

CC      = cc

all: jotawm jotawm-session

jotawm: jotawm.c jotawm.h
	$(CC) $(CFLAGS) -o $@ jotawm.c $(LDFLAGS)

jotawm-session: jotawm-session.c
	$(CC) $(CFLAGS) -o $@ jotawm-session.c

clean:
	rm -f jotawm jotawm-session

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 jotawm $(DESTDIR)$(PREFIX)/bin/jotawm
	install -m 755 jotawm-session $(DESTDIR)$(PREFIX)/bin/jotawm-session
	mkdir -p $(DESTDIR)$(PREFIX)/share/xsessions
	install -m 644 jotawm.desktop $(DESTDIR)$(PREFIX)/share/xsessions/jotawm.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/jotawm
	rm -f $(DESTDIR)$(PREFIX)/bin/jotawm-session
	rm -f $(DESTDIR)$(PREFIX)/share/xsessions/jotawm.desktop

.PHONY: all clean install uninstall
