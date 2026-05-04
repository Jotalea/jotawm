# jotawm

A minimal tiling window manager for X11.

## Showcase

- [screenshots](https://imgur.com/a/sf0llvp)
- [animations](https://www.youtube.com/watch?v=pJ3VZGY-KsU)

## Dependencies

* Xlib header files
* A standard C compiler
* `make`

## Installation

```sh
make
sudo make install
```

By default, the binary is installed to `/usr/bin/jwm`.

## Configuration

Configuration is performed entirely at compile time by editing `jwm.h`.

To modify the modifier key, default terminal, or keybindings, edit the `Configuration` block at the top of the source file. Recompile and restart `jwm` to apply changes.

The default modifier key (`Mod`) is set to `Mod4Mask` (Super/Windows key).

The default terminal is `kitty`, and the default menu is `dmenu_run`. Ensure these are installed or edit the `termcmd` and `menucmd` arrays to match your preferred software.

## Usage

* **Mod + t** : Spawn terminal
* **Mod + Space** : Spawn application menu
* **Mod + kLeft / kRight** : Focus next / previous window
* **Mod + Shit + kLeft / kRight** : Move focused window left / right
* **Mod + Alt + kLeft / kRight** : Shrink / expand the master area
* **Mod + f** : Toggle fullscreen for the focused window
* **Mod + q** : Kill the focused window
* **Mod + Shift + q** : Quit wm
* **Mod + [1-9]** : Switch to workspace N
* **Mod + Shift + [1-9]**: Move focused window to workspace N

## Running

### xinitrc
Add the following line to your `~/.xinitrc` to start `jwm` using `startx`:

```sh
exec jwm
```

### Display Manager

`jwm` installs a desktop file to `/usr/share/xsessions/jwm.desktop`, so it should appear as an option in most display managers (Ly, GDM, LightDM, SDDM, etc.).

## Recommended software
this is software that i personally recommend to use with jwm
- [pulseaudio](http://anongit.freedesktop.org/git/pulseaudio/pulseaudio.git)
- [brightnessctl](https://github.com/Hummer12007/brightnessctl.git)
- [polybar](https://github.com/polybar/polybar)
- [this picom fork](https://github.com/fdev31/picom)

## License

MIT
