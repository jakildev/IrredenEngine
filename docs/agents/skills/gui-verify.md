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
| `-- ARG ...` | none | Everything after `--` is forwarded to the target. |

The runner builds (`<build tool> --target <target>` unless `--no-build`),
runs (`<run tool> --timeout <S> <target> --auto-screenshot <N> <forwarded
args>`) capturing combined output, parses `GUI-ASSERT … result=PASS|FAIL`
lines, prints a shot / label / kind / name / result / actual table with a
`[gui-verify] <passed>/<total> assertions passed` summary and a
`Failing assertions:` list, and exits non-zero on any FAIL or a non-zero
binary exit. A binary that exits 0 with no `GUI-ASSERT` lines is
mis-wired; the runner warns.

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
