# Deterministic GUI + mouse verification harness

Design note for the GUI/mouse verification epic. It specs a deterministic,
headless way to drive and assert the trixel GUI — mouse moves, hovers, clicks,
and the resulting widget state — so an agent (or CI) can verify editor/GUI
behavior without a human watching the screen. It is the GUI sibling of the
existing render-regression harness (`render-verify` / `scripts/render-compare.py`)
and the [cull-validation harness](cull-validation-harness.md), and a step toward
the engine's broader goal: a **toolbox of deterministic verification skills**
covering rendering, GUI, and game logic.

This note is the authoritative cross-phase reference; the work lands as a fleet
epic (umbrella + one child task per phase below). The harness is **not** built
yet — this is the plan.

## TL;DR

- The render side is already deterministic and scriptable (`--auto-screenshot`
  shot tables + fixed-step sim + a stdlib image comparator + per-backend
  reference images). GUI widget **state** is already introspectable
  (`C_WidgetState` + `IRPrefab::Widget::isHovered/wasClicked/isPressed`).
- The **one missing primitive** is *synthetic input*: there is no way to inject
  a mouse position or a click. Input comes only from GLFW callbacks, so a
  headless run can never move the cursor or press a button.
- Add synthetic input injection (Phase 1), drive it from per-shot input scripts
  (Phase 2), assert hover/click outcomes against widget state (Phase 3), and
  package the whole thing as a `gui-verify` skill (Phase 4).
- Everything is frame-scheduled, not wall-clock — same determinism contract the
  screenshot harness already relies on.

## Why a GUI harness

The IRVoxel editor (and any future widget-driven creation) has logic that only
fires through mouse interaction: hover highlights, click-to-select, drag
sliders, the hover help-text panel (#1790), gizmo drag, voxel picking. None of
it is exercised by `--auto-screenshot` today, because that path renders frames
but never touches the cursor. So:

- An agent iterating on editor GUI cannot verify "clicking the BAKE button bakes
  the shape" or "hovering the FPS slider shows its help" without a human.
- Mouse-vs-rendered-object alignment bugs (the picking/selection offset the user
  reported) have no regression net.
- "Deterministic verification of game logic and rendering" is a stated engine
  design goal; the GUI is the current blind spot.

## What already exists (the foundation)

| Capability | Where | Notes |
|---|---|---|
| Headless shot tables | `engine/video/include/irreden/video/auto_screenshot.hpp` (`AutoScreenshotShot`, `parseAutoScreenshotArgv`); `engine/video/src/auto_screenshot.cpp` | per-shot zoom / camera / yaw / ROI-crop; cycles shots then closes the window |
| Fixed-step determinism | `engine/world/src/world.cpp` (`IRVideo::isAutoCaptureActive()` → `enableFixedStep()`) | one UPDATE tick per render frame during capture; per-tick animation is reproducible |
| Image comparator | `scripts/render-compare.py` (+ `scripts/render-verify.py`) | stdlib-only; match% / max-delta / PSNR vs per-backend reference PNGs |
| Reference images | `creations/demos/<demo>/test/references/<preset>/` + `manifest.json` | per-backend (`linux-debug` / `macos-debug` / `windows-debug`) |
| Widget state introspection | `C_WidgetState` (`component_widget.hpp`); `IRPrefab::Widget::isHovered / wasClicked / isPressed` (`widgets.hpp`) | per-widget hovered / pressed / focused / fireAction |
| Resolved hovered widget | `System<WIDGET_INPUT>::topHoveredId_` | the z-ordered topmost hovered hitbox (currently private) |

## The gap: no synthetic input

`InputManager` (`engine/input/include/irreden/input/input_manager.hpp`) snapshots
GLFW callbacks each frame. `IRInput::getMousePosition` / `getMousePositionScreen`
/ `checkKeyMouseButton` are **read-only**, and the button/scroll queues
(`processKeyMouseButtons` / `processScrolls`) are private with no public inject
path. There is no replay, no test-input mode, no way to set the cursor. So a
headless run cannot hover or click anything — the harness's load-bearing missing
piece.

## Proposed design

### Phase 1 — synthetic input injection (engine)

Add a deterministic input-injection layer the harness owns and GLFW does not
touch when active:

- `IRInput::beginSyntheticInput()` — switch `InputManager` to consume an
  injected event queue instead of (or in addition to) GLFW snapshots for this
  run. Mirrors the `isAutoCaptureActive()` gate that already flips the sim into
  fixed-step.
- `IRInput::injectMouseMove(ivec2 screenPx)` — set the cursor for the next
  frame; feeds the same `mousePosition` snapshot the GLFW path writes, so every
  downstream consumer (`HITBOX_MOUSE_TEST_GUI`, picking, gizmo hover) sees it
  with no per-consumer change.
- `IRInput::injectButton(button, ButtonStatus)` — enqueue a press/release into
  the existing private button queue via a new public entry point.
- `IRInput::injectScroll(dx, dy)` — same for scroll.

Injected events are applied at frame boundaries (one batch per render frame), so
ordering is reproducible. When synthetic input is inactive, the path is
byte-identical to today (GLFW only).

### Phase 2 — GUI-test shot tables (engine/video)

Extend the shot-table mechanism with scripted input per shot:

```cpp
struct GuiInputEvent {
    int frameOffset_;            // frames after the shot's settle to fire
    enum Type { MOVE, PRESS, RELEASE, SCROLL } type_;
    IRMath::ivec2 screenPx_;     // for MOVE / PRESS / RELEASE
    IRMath::vec2  scroll_;       // for SCROLL
};
struct GuiTestShot {
    IRVideo::AutoScreenshotShot render_;     // existing camera/zoom/crop config
    const GuiInputEvent *inputs_; int numInputs_;
    // optional: assertions to evaluate at capture time (Phase 3)
};
```

During capture the auto-screenshot system fires each shot's input events on the
scheduled frames (via Phase 1), lets the GUI settle, then captures + (Phase 3)
evaluates assertions. Reuses the existing settle-frame and screenshot plumbing.

### Phase 3 — hover/click assertions (engine + harness)

Assert GUI outcomes deterministically against the introspectable state:

- Expose the resolved hovered widget: write `WIDGET_INPUT::topHoveredId_` into a
  `C_GuiHoverState` singleton (or an `IRPrefab::Widget::hoveredWidget()`
  accessor) so a harness can read "which widget is hovered" without scanning.
- Assertion kinds, evaluated at a shot's capture frame:
  - `HOVERS(widgetName)` — after a `MOVE` to a point, the named widget is the
    topmost hovered.
  - `CLICK_FIRES(widgetName)` — after `PRESS`+`RELEASE` over it, `wasClicked` is
    true.
  - `SLIDER_VALUE(widgetName, expected, tol)`, `CHECKBOX(widgetName, bool)`, etc.
  - `PICKS_VOXEL(worldCoord)` — a click resolves to the expected picked voxel
    (the harness for the mouse-vs-render alignment bug the user flagged).
- Results stream to a machine-readable log (one line per assertion:
  shot / kind / target / pass|fail / actual), so an agent or CI parses pass/fail
  the way `render-verify` parses image diffs.

### Phase 4 — `gui-verify` skill + the toolbox

Package build → run-with-input-script → assert → report as a `gui-verify` skill,
the GUI sibling of `render-verify`:

- A creation declares a `GuiTestShot[]` table (camera + input script +
  assertions), the skill runs it headless, and reports per-assertion pass/fail
  plus optional GUI-canvas-region image diffs (reusing `render-compare.py` on a
  cropped GUI region for visual regressions like the help panel).
- Lands in the same verification toolbox as `render-verify` and the
  cull-validation harness, advancing the "deterministic verification of
  rendering and game logic" engine goal. A later `logic-verify` sibling can
  reuse Phase 1's injection + the assertion-log format for non-GUI game logic.

## Determinism contract

- Input is **frame-scheduled** (events fire on integer frame offsets), never
  wall-clock — matches the fixed-step capture mode.
- Synthetic-input mode disables GLFW input for the run, so no stray real cursor
  events perturb the script.
- Assertions read post-tick widget state on a deterministic frame, after a fixed
  settle count — the same reproducibility the screenshot harness already gives.
- RNG seeding is the caller's responsibility (set before capture), as today.

## Phased epic breakdown

1. **Phase 1 — synthetic input injection.** `InputManager` inject queue +
   `IRInput::beginSyntheticInput / injectMouseMove / injectButton / injectScroll`;
   GLFW-gated; no behavior change when inactive. Smoke: a temp system that injects
   a move+click and logs `getMousePosition` / a widget's `wasClicked`.
2. **Phase 2 — GUI-test shot tables.** `GuiTestShot` + `GuiInputEvent`; auto-
   screenshot fires scripted input per shot. Demonstrate on the voxel editor
   (move to a swatch, click, capture).
3. **Phase 3 — assertions + hovered-widget export.** `C_GuiHoverState`,
   assertion kinds, machine-readable result log.
4. **Phase 4 — `gui-verify` skill.** Wraps the flow; per-creation test tables;
   region image-diff via `render-compare.py`. Add a first editor GUI test.

## Open questions / risks

- **Backend parity of input timing.** Injection is CPU-side and pre-GLFW, so it
  should be backend-agnostic; confirm on Metal.
- **Hovered-widget export shape.** `C_GuiHoverState` singleton vs accessor —
  pick the one consistent with how other singleton render state is exposed.
- **Screen vs iso coordinates.** Tests should script in **screen pixels**
  (stable, what a user clicks); the harness converts via the existing
  screen→iso/GUI-trixel mappings, which is also where the reported
  mouse-vs-render alignment bug would surface and be pinned.
- **Scope of v1 assertions.** Start with hover + click-fires + slider/checkbox
  value; defer drag-path and gizmo-drag assertions to a follow-up once the
  primitive is proven.
