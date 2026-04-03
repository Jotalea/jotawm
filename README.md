# jwm(1)

A minimalist tiling window manager for X11, written in C.

## Installation

### Prerequisites

You will need the Xlib header files. On Debian-based systems, you can install them with:

```bash
sudo apt-get install libx11-dev
```

### Build and Install

```bash
make
sudo make install
```

By default, the binary is installed to `/usr/local/bin/jwm`.

## Configuration

Configuration is performed entirely at compile time by editing `jwm.h`.

To modify the modifier key, default terminal, or keybindings, edit the `Configuration` block at the top of the header file. Recompile and restart `jwm` to apply changes.

## Default Keybindings

The default modifier key is **Mod4** (usually the Windows key).

* **Mod + Return** : Open a terminal (default: `kitty`)
* **Mod + Space** : Run dmenu (default: `dmenu_run`)
* **Mod + Left / Right** : Cycle between windows
* **Mod + Shift + Left / Right** : Swap focused window with next/previous
* **Mod + Alt + Left / Right** : Resize master/stack split ratio
* **Mod + f** : Toggle fullscreen for focused window
* **Mod + q** : Close focused window
* **Mod + q + Shift** : Quit jwm
* **Mod + [1-9]** : Switch to workspace 1-9
* **Mod + Shift + [1-9]** : Move focused window to workspace 1-9

## Usage

### startx

Add the following line to your `~/.xinitrc` to start `jwm` using `startx`:

```bash
exec jwm
```

### Display Manager

`jwm` installs a desktop file to `/usr/local/share/xsessions/jwm.desktop`, so it should appear as an option in most display managers (GDM, LightDM, SDDM, etc.).

## License

MIT
