package main

import (
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"os"
	"os/exec"
	"strings"

	"github.com/BurntSushi/freetype-go/freetype/truetype"
	"github.com/jezek/xgbutil"
	"github.com/jezek/xgbutil/xgraphics"
)

// loadFont resolves the font to draw with, trying (in order): an explicit
// FontPath override, fontconfig's match for FontQuery, fontconfig's match
// for FontFallbackQuery, and finally a short list of common system fonts.
func loadFont() (*truetype.Font, error) {
	if FontPath != "" {
		return loadFontFile(FontPath)
	}

	if path := fcMatch(FontQuery); path != "" {
		if f, err := loadFontFile(path); err == nil {
			return f, nil
		}
	}

	if path := fcMatch(FontFallbackQuery); path != "" {
		if f, err := loadFontFile(path); err == nil {
			return f, nil
		}
	}

	for _, path := range staticFallbackFonts {
		if f, err := loadFontFile(path); err == nil {
			return f, nil
		}
	}

	return nil, fmt.Errorf("could not locate a usable font (tried fontconfig for %q and %q, and %d common system paths)",
		FontQuery, FontFallbackQuery, len(staticFallbackFonts))
}

func loadFontFile(path string) (*truetype.Font, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return xgraphics.ParseFont(f)
}

func fcMatch(query string) string {
	out, err := exec.Command("fc-match", "--format=%{file}", query).Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

// wrapText greedily breaks text into lines that each fit within maxWidth
// pixels when rendered with font at the given size.
func wrapText(font *truetype.Font, size float64, text string, maxWidth int) []string {
	words := strings.Fields(text)
	if len(words) == 0 {
		return nil
	}

	var lines []string
	cur := words[0]
	for _, w := range words[1:] {
		candidate := cur + " " + w
		width, _ := xgraphics.Extents(font, size, candidate)
		if width > maxWidth {
			lines = append(lines, cur)
			cur = w
			continue
		}
		cur = candidate
	}
	lines = append(lines, cur)
	return lines
}

// renderEntry is a single keybind entry laid out and measured ahead of
// drawing, so the two columns can be balanced by actual height.
type renderEntry struct {
	key     string
	lines   []string
	stacked bool
	height  int
}

// renderImage draws the title, the keybind grid (in two columns, wrapping
// long descriptions as needed) and the footer hint onto a fixed-size image
// sized WindowWidth x WindowHeight.
func renderImage(X *xgbutil.XUtil, kb []Keybind, font *truetype.Font) *xgraphics.Image {
	img := xgraphics.New(X, image.Rect(0, 0, WindowWidth, WindowHeight))

	draw.Draw(img, img.Bounds(), &image.Uniform{C: ColorBackground}, image.Point{}, draw.Src)
	drawBorder(img, BorderWidth, ColorBorder)

	contentX := Padding
	contentY := Padding

	_, titleH, _ := img.Text(contentX, contentY, ColorTitle, TitleFontSize, font, Title)
	contentY += titleH + TitleGap

	footerH := int(FooterFontSize) + 4
	contentBottom := WindowHeight - Padding - FooterGap - footerH
	contentWidth := WindowWidth - 2*Padding

	colWidth := (contentWidth - ColumnGap) / 2
	colX := [2]int{contentX, contentX + colWidth + ColumnGap}

	// Most keybinds fit "key   description" on a single line, but a few
	// key labels (e.g. "XF86AudioRaiseVolume / LowerVolume") are too long
	// for that without leaving almost no room for the description. Size
	// the shared key gutter off of the "typical" keys (capped as a
	// fraction of the column), and lay any longer key out on its own line
	// with the (full-width) description wrapped underneath it instead.
	keyGutter := 0
	gutterCap := colWidth * 45 / 100
	for _, k := range kb {
		w, _ := xgraphics.Extents(font, BodyFontSize, k.Key)
		if w > keyGutter && w <= gutterCap {
			keyGutter = w
		}
	}
	keyGutter += KeyDescGap
	descWidth := colWidth - keyGutter

	// For each entry, lay it out both ways -- "key   desc" sharing one
	// line (aligned to the shared gutter above), and "key" on its own
	// line with desc wrapped underneath using the full column width --
	// and keep whichever is shorter. A key too wide for the shared gutter
	// always goes with the stacked layout, to avoid overlapping the
	// description column.
	entries := make([]renderEntry, len(kb))
	total := 0
	for i, k := range kb {
		keyWidth, _ := xgraphics.Extents(font, BodyFontSize, k.Key)
		keyFits := keyWidth <= keyGutter-KeyDescGap

		inlineLines := wrapText(font, BodyFontSize, k.Desc, descWidth)
		stackedLines := wrapText(font, BodyFontSize, k.Desc, colWidth)

		e := renderEntry{key: k.Key}
		if keyFits && len(inlineLines) <= len(stackedLines) {
			e.lines = inlineLines
			e.height = len(e.lines)*LineHeight + EntrySpacing
		} else {
			e.stacked = true
			e.lines = stackedLines
			e.height = LineHeight + len(e.lines)*LineHeight + EntrySpacing
		}
		entries[i] = e
		total += e.height
	}

	// Split the entries between the two columns wherever it balances their
	// total height the best, instead of an even item count (a handful of
	// wrapped/stacked entries can otherwise make one column much taller).
	splitAt, running := len(entries), 0
	for i, e := range entries {
		if running+e.height/2 >= total/2 {
			splitAt = i
			break
		}
		running += e.height
	}

	for col, colEntries := range [][]renderEntry{entries[:splitAt], entries[splitAt:]} {
		x, y := colX[col], contentY
		for _, e := range colEntries {
			img.Text(x, y, ColorKey, BodyFontSize, font, e.key)
			if e.stacked {
				for i, line := range e.lines {
					img.Text(x, y+LineHeight+i*LineHeight, ColorDesc, BodyFontSize, font, line)
				}
			} else {
				for i, line := range e.lines {
					img.Text(x+keyGutter, y+i*LineHeight, ColorDesc, BodyFontSize, font, line)
				}
			}
			y += e.height
		}
		if y > contentBottom {
			fmt.Fprintf(os.Stderr, "jotawm-keybinds: warning: column %d overflows the popup by %dpx; "+
				"shrink BodyFontSize/LineHeight or grow WindowHeight in config.go\n", col+1, y-contentBottom)
		}
	}

	img.Text(contentX, WindowHeight-Padding-footerH+4, ColorFooter, FooterFontSize, font, Footer)

	return img
}

func drawBorder(img *xgraphics.Image, width int, c color.Color) {
	b := img.Bounds()
	for w := 0; w < width; w++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			img.Set(x, b.Min.Y+w, c)
			img.Set(x, b.Max.Y-1-w, c)
		}
		for y := b.Min.Y; y < b.Max.Y; y++ {
			img.Set(b.Min.X+w, y, c)
			img.Set(b.Max.X-1-w, y, c)
		}
	}
}
