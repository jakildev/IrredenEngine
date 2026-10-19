# EditorFontMaker

Draws and edits the GUI canvas's trixel-font glyphs on their lattice, then
writes an edited glyph back into its header's bitmap literal in place. The
display geometry and the authoring rules are in
[`docs/design/gui-trixel-display.md`](../../../docs/design/gui-trixel-display.md).

## Launch

```bash
fleet-build --target EditorFontMaker
fleet-run EditorFontMaker
fleet-run EditorFontMaker --font-header path/to/my_font.hpp
```

Without `--font-header` the editor opens the shipped LARGE
(`engine/render/include/irreden/render/trixel_font.hpp`) and SMALL
(`trixel_font_small.hpp`) sets and starts on LARGE. `--font-header` opens any
header that follows the shipped schema: `k<P>GlyphWidth` / `Height` /
`SpacingX` / `SpacingY` constants, `k<P>Glyph_<Name> = make<P>Glyph("row"
...)` literals, and a `get<P>Glyph(char c)` switch. The prefix picks the
slot and rule set: `Small` is the SMALL slot, any other prefix the LARGE slot
(a 4-wide cell with another prefix checks against the SMALL rules). The
header is labelled CUSTOM. A header the editor cannot parse shows its error
and cannot be saved. A new set opened this way is edited and saved like the
shipped ones; adding it to `IRRender::TextSize` is separate engine work.

## Screen

- **Character table.** One cell per character the engine draws with its own
  glyph (63: A-Z, 0-9, punctuation, space), each showing its working
  bitmap. Lowercase letters draw the uppercase glyph and are listed as ALSO
  on the info line, not as cells. Blue is an empty bitmap (Space is empty by
  design), red a character the header's switch has no case for, orange a
  literal with the wrong row count or width. A red bar under a cell means a
  rule finding; a green mark means unsaved edits.
- **Edit grid.** The selected glyph magnified with the lattice scaling text
  uses (`c_text_to_trixel`): each texel is its left- or right-pointing
  triangle, with unlit texels shaded by orientation so the parity is visible.
  Texels a rule flags are red, and the info line names the rule and cell:
  `lone triangle`, `wrong parity` (a notched stem end in LARGE, an odd-row
  outer texel in SMALL), and `SAME AS <char>` for a bitmap another character
  already uses.
- **Sample strip.** The text-input string drawn with the working bitmaps of
  both sets at font size 1 and 2, LARGE in white and SMALL in blue, so an edit
  shows in context before it is saved.

## Controls

| Input | Action |
|---|---|
| Click a table cell | Select the character |
| Click the edit grid | Toggle the texel whose triangle was clicked |
| LARGE / SMALL | Switch the active set |
| SAVE | Write the selected glyph into its header |
| REVERT | Discard unsaved edits to the selected glyph |
| RELOAD | Re-read the active header, discarding its unsaved edits |
| SCALE 1 / SCALE 2, Ctrl+= / Ctrl+- | Set `gui_scale` |
| SCREEN / PIXEL | Toggle the screen and pixelated (framebuffer) GUI composites |
| Text input | Sample text |
| `` ` `` | Command help overlay |
| F8 / F7 / F9 | Screenshot / canvas screenshot / record toggle |
| Esc | Quit |

## Saving

SAVE rewrites only the quoted rows of the selected glyph's literal, and only
the characters of texels whose value changed; comments, constants, the switch
and every other glyph keep their bytes, and the file's line endings and
permissions are kept. The write goes to a temporary file in the same
directory, which then replaces the header. Where the platform cannot rename
over an existing file, the header is first moved to
`<header>.font-maker-backup` and moved back if the replacement fails; if even
that fails, the refusal names the backup, which holds the original.

SAVE refuses, leaving the file untouched, when the header changed on disk
since it was loaded (RELOAD it), when the glyph's literal is malformed, or
when the character has no literal. The engine's own text keeps the compiled
glyphs until the next build; rebuild to see a saved glyph in the editor's
widgets and everywhere else.

## Tests

```bash
python3 scripts/gui-verify.py EditorFontMaker -- --font-maker-test table
python3 scripts/gui-verify.py EditorFontMaker -- --font-maker-test edit
python3 scripts/render-verify.py --target EditorFontMaker
fleet-run IrredenEngineTest --gtest_filter='TrixelFont.*:FontMakerSource.*'
```

- `table` checks the 63 cells and Space's empty state on both sets, then
  opens a copy of the LARGE header without the `K` case and checks that
  exactly `K` reads as missing.
- `edit` selects Space and toggles one texel through injected clicks, then
  checks that one source bit and the preview changed and that the texel is
  flagged as a lone triangle; checks the grid's triangle orientation against
  `(column + row)` parity; and checks the wrong-parity and duplicate-bitmap
  findings on synthetic edits.
- `sample` (the render-verify rows) edits E and captures the sample strip
  through both composites at `gui_scale` 1 and 2.
- `FontMakerSource.*` round-trips both shipped headers through a save on
  temporary copies, checks the refusals, forces each replacement failure
  to prove the original survives, and compiles the saved copies.

No session saves into the shipped headers.
