# gui-verify — shared flow

The canonical `gui-verify` flow: build a creation, run it headless with a
scripted-input shot table, parse the per-assertion result log emitted by the
P3 GUI-test harness, and report a pass/fail table. Exits non-zero when any
assertion fails so CI and fleet agents get a parseable signal.

Every repo that runs a fleet keeps its
`.claude/skills/gui-verify/SKILL.md` as a thin wrapper that points here and
supplies only its **deltas** (below) plus any repo-specific procedure files.
See [`docs/design/skill-sharing.md`](../../design/skill-sharing.md) for the
mechanism. Wherever a step needs a repo-specific value it names a **delta
key** in bold.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **runner** | Path to the runner script. | `scripts/gui-verify.py` |
| **build tool** | Tool used to build the target. | `fleet-build` |
| **run tool** | Tool used to run the target. | `fleet-run` |
| **warmup default** | Default warmup frames passed to `--auto-screenshot`. | `10` |

---

## Prerequisites

### Creation requirement: P3 GUI-test harness

The creation must opt into the P3 assertion tables:

1. Include `engine/prefabs/irreden/render/gui_test_assertions.hpp`.
2. Populate `g_shotAssertions[i]` arrays for the shots under test.
3. Call `IRVideo::createGuiTestSystem(cfg)` in `initSystems()`, guarded by
   `if (g_autoWarmupFrames > 0)` so the harness only runs in screenshot mode
   (follow `creations/editors/voxel_editor/main.cpp` as the worked example).

When this is in place the binary emits one `GUI-ASSERT` log line per
assertion per assertion-shot, in the format:

```
GUI-ASSERT shot=<N> label=<lbl> kind=<KIND> target=<eid> name=<tag> result=PASS|FAIL actual=<observed>
```

Assertion kinds: `HOVERS`, `CLICK_FIRES`, `SLIDER_VALUE`, `CHECKBOX`,
`PICKS_VOXEL`, `PICKS_ISO_COLUMN`, `PREDICATE`. `PREDICATE` takes a
creation-supplied `bool(context, actual)` — the escape hatch for state the
prefab layer can't see (an editor mode flag, a voxel-set cell). Use it rather
than emitting a `GUI-ASSERT` line by hand: the runner parses one format, and a
second emitter is one drift away from being unparseable.

### Platform

Works on any host that can run the creation headless. On macOS the Metal
backend is used; on Linux the OpenGL backend. The assertion-log path is
backend-agnostic (results come from CPU-side state, not pixel comparison).

---

## Running the harness

```
python3 <runner> <target>
```

Concrete engine example (voxel editor, all six shots including the two
assertion shots):

```
python3 scripts/gui-verify.py IRVoxelEditor
```

Options:

| Flag | Default | Effect |
|---|---|---|
| `--warmup-frames N` | 10 | Warmup frames for `--auto-screenshot N`. |
| `--no-build` | off | Skip the build step (assume binary is current). |
| `--timeout S` | 120 | Watchdog: kill the process after S seconds. |
| `-- ARG ...` | none | Everything after a literal `--` is forwarded to the target, so a run can select a mode the shot table depends on. |

Forwarded args are how a creation's alternate shot tables are reached — e.g.
the voxel editor's authoring sessions (#766), which replace the standing shot
table with a recipe of scripted editor gestures:

```
python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session drag_probe
```

---

## What the runner does

1. **Build** — `fleet-build --target <target>` (unless `--no-build`).
2. **Run** — `fleet-run --timeout <S> <target> --auto-screenshot <N>
   <forwarded args>`, capturing combined stdout+stderr while streaming it
   to the terminal.
3. **Parse** — scan the captured output for lines matching
   `GUI-ASSERT ... result=PASS|FAIL ...`.
4. **Report** — print a per-assertion table (shot / label / kind / name /
   result / actual value).
5. **Exit** — non-zero if any assertion is `FAIL` or if the binary itself
   exits non-zero.

### Typical pass output

```
shot   label                          kind           name                   result   actual
-----------------------------------------------------------------------------------------------
4      editor_gui_assert              HOVERS         layer_list_hover       PASS     widget=1234
4      editor_gui_assert              CLICK_FIRES    layer_list_click       PASS     fired=true
4      editor_gui_assert              CHECKBOX       layer_visible          PASS     checked=true
4      editor_gui_assert              SLIDER_VALUE   fps_value              PASS     fps=24.0
5      editor_pick_voxel              PICKS_VOXEL    scene_pick             PASS     voxel=(-1,-1,-1)

[gui-verify] 5/5 assertions passed
```

### Typical fail output

```
shot   label                          kind           name                   result   actual
-----------------------------------------------------------------------------------------------
4      editor_gui_assert              HOVERS         layer_list_hover       FAIL     widget=none
...

[gui-verify] 0/5 assertions passed

Failing assertions:
  shot=4 name=layer_list_hover kind=HOVERS actual=widget=none
```

---

## Adding a first GUI test to a creation

1. In `main.cpp`, add shot entries to the shot table for the GUI-test and
   pick-test scenarios (follow `creations/editors/voxel_editor/main.cpp`
   shots 4–5 as a worked example).
2. Populate `g_shotAssertions[<shot_index>]` with assertions from the
   `IRPrefab::GuiTest` factory functions.
3. Run once with the **runner** to confirm assertions pass:
   `python3 <runner> <target> --no-build`
4. Re-run a second time to confirm stable (no flake from timing/settle
   windows).

### Assertion stability

`CLICK_FIRES` and `CHECKBOX` assertions have a settle window (`settleFrames_`
in `GuiTestConfig`) after synthetic input before the capture frame. Increase
the settle window if assertions flake (widen the gap between input injection
and the assertion frame).

---

## Image-comparison integration

For visual regressions on GUI regions (panel layout, text rendering), the
`render-verify` harness handles pixel comparison via `scripts/render-compare.py`.
The two harnesses are complementary:

- **`gui-verify`** — behavioral assertions: "did the hover state update?
  did the click fire the action?" No image reference needed.
- **`render-verify`** — pixel assertions: "does the panel look the same as
  the committed reference?" Requires committed PNGs.

Use `render-verify` with a `crops` block in the creation's
`test/references/manifest.json` when you want both behavioral and visual
coverage. The behavioral assertions in `gui-verify` are the faster, backend-
agnostic gate; pixel comparison is an opt-in overlay.

---

## Anti-patterns

- Running on a non-headless host without a display. The binary needs a Metal
  or OpenGL context even in `--auto-screenshot` mode.
- Asserting world-space voxel positions that depend on camera state before
  the camera settles. Gate picks on a deterministic initial camera pose.
- Increasing `warmup-frames` indefinitely to mask flaky settle windows.
  Fix the root cause (increase `settleFrames_` in the shot config) instead.
- Checking PASS/FAIL only from the process exit code without reading the
  assertion table. A binary that exits 0 but emits no `GUI-ASSERT` lines is
  silently mis-wired; the runner warns on this case.

---

## Related

- `.claude/skills/render-verify/SKILL.md` — pixel-level regression harness
  (use alongside gui-verify for visual coverage).
- `engine/prefabs/irreden/render/gui_test_assertions.hpp` — assertion
  factory and per-frame driver (P3 of epic #1793).
- `engine/video/include/irreden/video/auto_screenshot.hpp` — `GuiTestConfig`
  and `GuiTestShot` types that drive the harness.
