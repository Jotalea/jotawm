package main

import (
	_ "embed"
	"regexp"
	"strings"
)

// readme_embed.md is a copy of jotawm's top-level README.md, refreshed by
// `make` before every build (see the Makefile). This keeps the keybind list
// shown by this popup in sync with the README without hand-copying it.
//
//go:embed readme_embed.md
var readmeMD string

// Keybind is a single "key : description" entry parsed out of the
// README's "## keybinds" section.
type Keybind struct {
	Key  string
	Desc string
}

// matches lines shaped like "* **Mod + t** : spawn terminal"
var keybindLineRe = regexp.MustCompile(`^\*\s+\*\*(.+?)\*\*\s*:\s*(.+)$`)

// loadKeybinds parses every "* **key** : description" bullet found between
// the "## keybinds" heading and the next "## " heading in readmeMD.
func loadKeybinds() []Keybind {
	inSection := false
	var out []Keybind

	for _, line := range strings.Split(readmeMD, "\n") {
		trimmed := strings.TrimSpace(line)

		if strings.HasPrefix(trimmed, "## ") {
			inSection = strings.EqualFold(trimmed, "## keybinds")
			continue
		}
		if !inSection {
			continue
		}

		if m := keybindLineRe.FindStringSubmatch(trimmed); m != nil {
			out = append(out, Keybind{Key: m[1], Desc: m[2]})
		}
	}

	return out
}
