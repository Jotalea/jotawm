PREFIX      ?= /usr/local
CC          ?= cc

X11CFLAGS   := $(shell pkg-config --cflags x11 xinerama 2>/dev/null)
X11LIBS     := $(shell pkg-config --libs x11 xinerama 2>/dev/null)
X11CFLAGS   ?= -I/usr/include
X11LIBS     ?= -L/usr/lib -lX11 -lXinerama

CFLAGS      += -O2 -Wall -Wextra $(X11CFLAGS)
LDFLAGS     += $(X11LIBS)

all: jotawm jotawm-session

jotawm: jotawm.c jotawm.h
	$(CC) $(CFLAGS) -o $@ jotawm.c $(LDFLAGS)

jotawm-session: jotawm-session.c
	$(CC) $(CFLAGS) -o $@ jotawm-session.c $(LDFLAGS)

clean:
	rm -rf jotawm/
	rm -f jotawm-session
	rm -rf pkg/ src/
	rm -f jotawm-git-*.pkg.tar.zst

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 jotawm $(DESTDIR)$(PREFIX)/bin/jotawm
	install -m 755 jotawm-session $(DESTDIR)$(PREFIX)/bin/jotawm-session
	install -d $(DESTDIR)$(PREFIX)/share/xsessions
	install -m 644 jotawm.desktop $(DESTDIR)$(PREFIX)/share/xsessions/jotawm.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/jotawm
	rm -f $(DESTDIR)$(PREFIX)/bin/jotawm-session
	rm -f $(DESTDIR)$(PREFIX)/share/xsessions/jotawm.desktop

.PHONY: all clean install uninstall
