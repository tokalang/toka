# Toka grapheme editor demo

This macOS demo renders a custom single-line `TextEditor` document. Click the
window, then type; the `|` character is the self-drawn insertion caret. It
accepts committed platform text and applies left/right, Backspace, and Forward
Delete at extended-grapheme boundaries. Composition updates are deliberately
ignored; there is no native text-field widget, selection highlight, mouse
placement, IME editing state, shaping, bidi layout, or multiline layout.

Run from this directory after building Toka from this source tree:

```text
TOKA_LIB=../../lib TOKAC=../../build/bin/tokac ../../build/bin/toka run
```
