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

# The keybinds popup is a separate Go program under keybinds/. It is kept out
# of `all` deliberately: building the window manager needs only a C compiler
# and Xlib, and pulling a Go toolchain into that path for an optional
# cheat-sheet would slow every build and CI run that only cares about the WM.
#
#   make keybinds          build it
#   make install-keybinds  install it
#
# Both honour PREFIX and DESTDIR the same way the C targets do.
keybinds:
	$(MAKE) -C keybinds

install-keybinds: keybinds
	$(MAKE) -C keybinds install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)

clean:
	rm -rf jotawm
	rm -f jotawm-session
	rm -rf pkg/ src/
	rm -f jotawm-git-*.pkg.tar.zst
	rm -rf debian-build/
	rm -rf debian/.debhelper/ debian/jotawm/
	rm -f debian/*.substvars debian/*.debhelper.log debian/files
	-$(MAKE) -C keybinds clean

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
	-$(MAKE) -C keybinds uninstall PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)

.PHONY: all clean install uninstall keybinds install-keybinds
