package main

import "image/color"

// Configuration is performed entirely at compile time by editing this file,
// the same way jotawm itself is configured through jotawm.h. Recompile
// (`make`) after making changes.

// ---- window ----
var (
	// WindowWidth and WindowHeight are the fixed size of the popup.
	WindowWidth  = 860
	WindowHeight = 680
)

// ---- font ----
var (
	// FontPath, if non-empty, is loaded directly and skips font discovery
	// entirely. Leave empty to auto-detect via fontconfig.
	FontPath = ""

	// FontQuery is resolved with `fc-match` to find the primary font.
	FontQuery = "JetBrainsMono Nerd Font:style=Regular"

	// FontFallbackQuery is tried if FontQuery can't be resolved to a file,
	// or if the file fontconfig points at can't be parsed as TrueType.
	FontFallbackQuery = "monospace:style=Regular"

	TitleFontSize  = 19.0
	BodyFontSize   = 11.0
	FooterFontSize = 9.5
)

// staticFallbackFonts is tried, in order, if fontconfig itself is
// unavailable or fails to resolve either query above.
var staticFallbackFonts = []string{
	"/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
	"/usr/share/fonts/jetbrains-mono-nerd/JetBrainsMonoNerdFont-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
	"/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
}

// ---- layout ----
var (
	Padding      = 20 // outer margin on every side
	ColumnGap    = 28 // horizontal gap between the two columns
	KeyDescGap   = 16 // gap between a key label and its description
	LineHeight   = 15 // vertical space per line of body text
	EntrySpacing = 5  // extra vertical gap between keybind entries
	TitleGap     = 14 // space below the title, before the keybind grid
	FooterGap    = 12 // space above the footer hint
	BorderWidth  = 2
)

// ---- palette (Catppuccin Mocha, pink accent) ----
var (
	ColorBackground = rgb(0x1e1e2e) // Base
	ColorBorder     = rgb(0xf5c2e7) // Pink
	ColorTitle      = rgb(0xf5c2e7) // Pink
	ColorKey        = rgb(0xf5c2e7) // Pink
	ColorDesc       = rgb(0xcdd6f4) // Text
	ColorFooter     = rgb(0x7f849c) // Overlay1
)

// Title is the heading drawn at the top of the popup.
var Title = "jotawm keybinds"

// Footer is the hint drawn at the bottom of the popup.
var Footer = "press any key, or click outside, to close"

func rgb(hex uint32) color.RGBA {
	return color.RGBA{
		R: uint8(hex >> 16),
		G: uint8(hex >> 8),
		B: uint8(hex),
		A: 0xff,
	}
}
