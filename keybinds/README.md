# jotawm-keybinds

A tiny, always-on-top popup that shows jotawm's keybinds. It's meant to be
bound to a key in `jotawm.h` (e.g. `Mod + slash`) as a cheat-sheet.

The keybind list is read from the top-level [`../README.md`](../README.md)
at build time (see the Makefile), so it never drifts out of sync with
jotawm's real bindings.

It closes as soon as it loses focus, the user clicks anywhere outside of
it, or presses any key.

## dependencies

* a Go toolchain (1.21+)
* `fontconfig` at runtime, to locate the default font (falls back to a
  handful of common font paths if `fc-match` isn't available)
* `make`

Written in Go against [jezek/xgb](https://github.com/jezek/xgb) and
[jezek/xgbutil](https://github.com/jezek/xgbutil), pure-Go X11 bindings, so
it builds into a single statically linked binary with no cgo, no libX11,
and no other runtime library dependencies.

## installation

### arch linux
```sh
makepkg -sic
```

### other linux

```sh
make
sudo make install
```

by default, the binary is installed to `/usr/bin/jotawm-keybinds`.

## configuration

configuration is performed entirely at compile time by editing `config.go`:
window size, font (defaults to JetBrains Mono Nerd Font, falling back to
fontconfig's generic `monospace`), font sizes, spacing, and the color
palette (defaults to Catppuccin Mocha with a pink accent). Recompile
(`make`) after making changes.
