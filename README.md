# jotawm

A minimal tiling window manager for X11.

## showcase

- [screenshots](https://imgur.com/a/sf0llvp)
- [animations](https://www.youtube.com/watch?v=1O3XxlL_rVc)

## dependencies

* Xlib header files
* a standard C compiler
* `make`

## installation

### arch linux
```sh
makepkg -si
```

### other linux

```sh
make
sudo make install
```

by default, the binary is installed to `/usr/bin/jotawm`.

## configuration

configuration is performed entirely at compile time by editing `jotawm.h`.

to modify the modifier key, default terminal, or keybinds, edit the variables in the `jotawm.h` file. recompile and restart `jotawm` to apply changes.

the default modifier key (`Mod`) is set to `Mod4Mask` (Super/windows key).

the default terminal is `kitty`, and the default menu is `rofi`. make sure these are installed or edit the `termcmd` and `menucmd` arrays to match your preferred software.

## keybinds

* **Mod + t** : spawn terminal
* **Mod + e** : spawn file explorer
* **Mod + b** : spawn web browser
* **Mod + Space** : spawn application menu
* **Mod + kLeft / kRight** : focus next / previous window
* **Mod + Shift + kLeft / kRight** : move focused window left / right
* **Mod + Alt + kLeft / kRight** : shrink / expand the focused window
* **Mod + f** : toggle fullscreen for the focused window
* **Mod + q** : kill the focused window
* **Mod + Shift + q** : quit wm
* **Mod + [1-9]** : switch to workspace N
* **Mod + Shift + [1-9]**: move focused window to workspace N
* **Mod + s** : take a fullscreen screenshot and copy it to the clipboard
* **Mod + Shift + s** : take a screenshot of a selection and copy it to the clipboard
* **PrntScr** : take a fullscreen screenshot and save it to ~/Pictures/
* **Mod + v** : toggle split direction (horizontal/vertical)
* **Mod + w** : toggle floating state for the focused window
* **Mod + m** : toggle macOS stage manager
* **Mod + PgUp / PgDown** : switch to previous / next workspace
* **XF86AudioRaiseVolume / LowerVolume** : increase / decrease volume (pactl)
* **XF86MonBrightnessUp / Down** : increase / decrease brightness

## running

### xinitrc
add the following line to your `~/.xinitrc` to start `jotawm-session` using `startx`:

```sh
exec jotawm-session
```

### display managers

`jotawm` installs a desktop file to `/usr/share/xsessions/jotawm.desktop`, so it should appear as an option in most display managers (Ly, GDM, LightDM, SDDM, etc.).

## recommended software
this is software that i personally recommend to use alongside jotawm
- [pulseaudio](http://anongit.freedesktop.org/git/pulseaudio/pulseaudio.git)
- [brightnessctl](https://github.com/Hummer12007/brightnessctl.git)
- [polybar](https://github.com/polybar/polybar.git)
- [this picom fork](https://github.com/fdev31/picom.git)
- [ly](https://github.com/fairyglade/ly.git)
- [rofi](https://github.com/davatorium/rofi.git)
- [kitty](https://github.com/kovidgoyal/kitty.git)
- [maim](https://github.com/naelstrof/maim.git)

## ownership

jotawm is a fork of [173duprot/wm](https://github.com/173duprot/wm.git). 
the original author has explicitly encouraged me to refer to this project as my own in casual and social contexts. this is their intended way for their software to be used. 
formal attribution is preserved in the license below and in the repository metadata.

## license

BSD-3 CLAUSE
