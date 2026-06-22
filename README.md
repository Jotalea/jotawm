# jotawm

A minimal tiling window manager for X11.

## Showcase

- [screenshots](https://imgur.com/a/sf0llvp)
- [animations](https://www.youtube.com/watch?v=1O3XxlL_rVc)

## Dependencies

* Xlib header files
* A standard C compiler
* `make`

## Installation

```sh
make
sudo make install
```

By default, the binary is installed to `/usr/bin/jotawm`.

## Configuration

Configuration is performed entirely at compile time by editing `jotawm.h`.

To modify the modifier key, default terminal, or keybindings, edit the `Configuration` block at the top of the source file. Recompile and restart `jotawm` to apply changes.

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
Add the following line to your `~/.xinitrc` to start `jotawm-session` using `startx`:

```sh
exec jotawm-session
```

### Display Manager

`jotawm` installs a desktop file to `/usr/share/xsessions/jotawm.desktop`, so it should appear as an option in most display managers (Ly, GDM, LightDM, SDDM, etc.).

## Recommended software
this is software that i personally recommend to use alongside jotawm
- [pulseaudio](http://anongit.freedesktop.org/git/pulseaudio/pulseaudio.git)
- [brightnessctl](https://github.com/Hummer12007/brightnessctl.git)
- [polybar](https://github.com/polybar/polybar)
- [this picom fork](https://github.com/fdev31/picom)
- [ly](https://github.com/fairyglade/ly)
- [rofi](https://github.com/davatorium/rofi)
- [kitty](https://github.com/kovidgoyal/kitty)

## License

BSD-3 CLAUSE
