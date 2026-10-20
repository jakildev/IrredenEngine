# gui-verify — shared flow

Build a creation, run it headless with the P3 GUI-test shot table, parse
the per-assertion result log, and report a pass/fail table. Exits non-zero
on any failure so CI and fleet agents get a parseable signal.

Each repo's `.claude/skills/gui-verify/SKILL.md` is a thin wrapper that
points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)).

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **runner** | Path to the runner script. | `scripts/gui-verify.py` |
| **build tool** | Tool used to build the target. | `fleet-build` |
| **run tool** | Tool used to run the target. | `fleet-run` |
| **warmup default** | Default warmup frames passed to `--auto-screenshot`. | `10` |

---

## Creation requirement

The creation opts into the P3 assertion tables:

1. Include `engine/prefabs/irreden/render/gui_test_assertions.hpp`.
2. Populate `g_shotAssertions[i]` for the shots under test.
3. Call `IRVideo::createGuiTestSystem(cfg)` in `initSystems()`, guarded by
   `if (g_autoWarmupFrames > 0)` so the harness runs only in screenshot
   mode (`creations/editors/voxel_editor/main.cpp` is the worked example).

The binary then emits one line per assertion per assertion-shot:

```
GUI-ASSERT shot=<N> label=<lbl> kind=<KIND> target=<eid> name=<tag> result=PASS|FAIL actual=<observed>
```

Kinds: `HOVERS`, `CLICK_FIRES`, `SLIDER_VALUE`, `CHECKBOX`, `PICKS_VOXEL`,
`PICKS_ISO_COLUMN`, `PREDICATE`. `PREDICATE` takes a creation-supplied
`bool(context, actual)` for state the prefab layer cannot see; use it
rather than emitting a `GUI-ASSERT` line by hand — the runner parses one
format.

Any host that runs the creation headless works (Metal on macOS, OpenGL on
Linux); results come from CPU-side state, not pixels.

## Running

```
python3 <runner> <target>
python3 scripts/gui-verify.py IRVoxelEditor                              # engine example
python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session drag_probe  # forwarded args select an alternate shot table
```

| Flag | Default | Effect |
|---|---|---|
| `--warmup-frames N` | 10 | Warmup frames for `--auto-screenshot N`. |
| `--no-build` | off | Skip the build step. |
| `--timeout S` | 120 | Watchdog: kill the process after S seconds. |
| `--allow-layout-findings` | off | With `-- --gui-layout-check`, report layout findings without failing. |
| `-- ARG ...` | none | Everything after `--` is forwarded to the target. |

The runner builds (`<build tool> --target <target>` unless `--no-build`),
runs (`<run tool> --timeout <S> <target> --auto-screenshot <N> <forwarded
args>`) capturing combined output, parses `GUI-ASSERT … result=PASS|FAIL`
lines, prints a shot / label / kind / name / result / actual table with a
`[gui-verify] <passed>/<total> assertions passed` summary and a
`Failing assertions:` list, and exits non-zero on any FAIL or a non-zero
binary exit. A binary that exits 0 with no `GUI-ASSERT` lines is
mis-wired; the runner warns.

## Layout check

`-- --gui-layout-check` arms the GUI layout audit (`IRPrefab::GuiAudit`,
`engine/prefabs/irreden/render/gui_layout_audit.hpp`) on every capture frame:
widget rects and the glyph cells the renderers queue, decided without a human
reading a screenshot. A creation wires it by registering
`GuiAudit::kLayoutCheckFlag` on `IREngine::args()` before init and arming
`GuiTest::LatchState::layoutCheck_` from it; an unwired target rejects the
flag (exit 2). The voxel editor is wired. Each capture frame logs:

```
GUI-LAYOUT <kind> <idA> <idB> <x> <y> <w> <h> shot=<i> label=<shot>
GUI-LAYOUT-SUMMARY <n> shot=<i> label=<shot>
```

Kinds: `overlap` (two hitboxes), `text-over-widget` (a run crosses another
widget or run), `text-clipped` (a run passes its own widget or the canvas),
`outside-parent` (a widget leaves the panel holding its origin), `offscreen`.
The runner fails a shot with a non-zero summary, and a run where an announced
shot logs no summary, unless `--allow-layout-findings`.

A finding is **intent** only when the cover is the design — a deliberate
overlay sitting over what it hides. Declare it on the covering widget
(`IRPrefab::Widget::setCoversBelow`, `IRGui.setCoversBelow`); panels and open
dropdown strips declare theirs. Everything else is a **bug**: fix the layout
(a position or size constant). A text run's origin can shift right by one
trixel for lattice parity, so size a box for its text with a trixel to spare,
and give a right-aligned readout an odd box width. Never declare a cover to
silence a real collision.

## Adding a first GUI test to a creation

1. Add GUI-test and pick-test shot entries to the shot table in `main.cpp`
   (voxel editor shots 4–5 are the example).
2. Populate `g_shotAssertions[<shot_index>]` from the `IRPrefab::GuiTest`
   factory functions.
3. `python3 <runner> <target> --no-build`, then run again to confirm the
   result is stable.

`CLICK_FIRES` and `CHECKBOX` have a settle window (`settleFrames_` in
`GuiTestConfig`) between synthetic input and the capture frame; widen it
if they flake — never mask flakes with more warmup frames.

## Pixel coverage

`gui-verify` asserts behaviour; `render-verify` (`scripts/render-compare.py`)
asserts pixels against committed references. For both on a GUI region, add
a `crops` block to the creation's `test/references/manifest.json` and run
`render-verify` alongside.

## Anti-patterns

- Asserting world-space voxel positions before the camera settles — gate
  picks on a deterministic initial pose.
- Reading only the exit code and not the assertion table.

Types: `engine/video/include/irreden/video/auto_screenshot.hpp`
(`GuiTestConfig`, `GuiTestShot`);
`engine/prefabs/irreden/render/gui_test_assertions.hpp` (factory and
per-frame driver).
