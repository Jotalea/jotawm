PREFIX ?= /usr/local
GO     ?= go

BIN := jotawm-keybinds

all: $(BIN)

# readme_embed.md is a copy of the top-level README.md, embedded into the
# binary at build time so this popup's keybind list stays in sync with it.
readme_embed.md: ../README.md
	cp ../README.md readme_embed.md

$(BIN): readme_embed.md *.go go.mod
	CGO_ENABLED=0 $(GO) build -trimpath -ldflags="-s -w" -o $(BIN) .

clean:
	rm -f $(BIN)
	rm -rf pkg/ src/
	rm -f *.pkg.tar.zst

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

.PHONY: all clean install uninstall
