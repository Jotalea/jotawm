# wm(1)

A minimal tiling window manager for X11. 

Designed with strict adherence to simplicity and high quality code. It provides a single master-stack layout, 9 mutual-exclusion workspaces, sloppy mouse focus, and uses `pledge(2)` for process sandboxing when on OpenBSD. 

There is no floating window support, no window decorations, and no runtime configuration parsing.

## Dependencies

* Xlib header files
* A standard C compiler
* `make`

## Installation

```sh
make
doas make install
```

By default, the binary is installed to `/usr/local/bin/wm`.

## Configuration

Configuration is performed entirely at compile time by editing `wm.h`.

To modify the modifier key, default terminal, or keybindings, edit the `Configuration` block at the top of the source file. Recompile and restart `wm` to apply changes.

The default modifier key (`Mod`) is set to `Mod4Mask` (Super/Windows key). 

The default terminal is `st`, and the default menu is `dmenu_run`. Ensure these are installed or edit the `termcmd` and `menucmd` arrays to match your preferred software.

## Usage

* **Mod + Return** : Spawn terminal
* **Mod + Space** : Spawn application menu
* **Mod + j / k** : Focus next / previous window
* **Mod + j / k + Shift** : Move focused window left / right
* **Mod + h / l** : Shrink / expand the master area
* **Mod + f** : Toggle fullscreen for the focused window
* **Mod + q** : Kill the focused window
* **Mod + q + Shift** : Quit wm
* **Mod + [1-9]** : Switch to workspace N
* **Mod + [1-9] + Shift**: Move focused window to workspace N

## Running

Add the following line to your `~/.xinitrc` to start `wm` using `startx`:

```sh
exec wm
```
