# Plan: synthetic mouse/click injection (P1)

- **Issue:** #1794
- **Model:** opus
- **Date:** 2026-06-13
- **Epic:** #1793 — see `.fleet/plans/issue-1793.md` for full context
- **Blocked by:** (none)

## Verified current state

`InputManager` (`engine/input/include/irreden/input/input_manager.hpp`) snapshots
GLFW callbacks per pipeline event. `IRInput::getMousePosition` /
`getMousePositionScreen` / `checkKeyMouseButton` are read-only; the button/scroll
queues (`processKeyMouseButtons` / `processScrolls`) are private with no public
inject entry point. So nothing outside GLFW can set the cursor or a button —
confirmed by reading the manager surface during the epic's investigation. The
fixed-step capture gate (`engine/world/src/world.cpp`,
`IRVideo::isAutoCaptureActive()` → `m_timeManager.enableFixedStep()`) is the
precedent for a run-scoped mode flip to mirror.

## Affected files

- `engine/input/include/irreden/input/input_manager.hpp` (+ `.cpp` impl) —
  injected-event queue + the public inject entry points + the active-mode flag.
- `engine/input/include/irreden/ir_input.hpp` (+ impl) — `IRInput::`
  `beginSyntheticInput` / `injectMouseMove` / `injectButton` / `injectScroll`.

## Approach

- Add a `m_syntheticInputActive` flag + an injected-event queue to `InputManager`.
  When active, the per-frame snapshot is built from the injected queue instead of
  the GLFW callbacks (cursor pos, button presses, scrolls).
- `injectMouseMove(screenPx)` sets the next frame's cursor; `injectButton` /
  `injectScroll` enqueue into the existing internal queues via new public
  forwarders.
- Apply one batch of injected events per frame, before the INPUT-pipeline
  snapshot, so `HITBOX_MOUSE_TEST_GUI` / picking / gizmo-hover read them
  unchanged.
- Inactive path is byte-identical to today (GLFW only) — gate every divergence on
  the flag.

## Acceptance criteria

- Synthetic active: `injectMouseMove` + `injectButton` drive `getMousePosition`
  and a widget's `wasClicked` headless.
- Synthetic inactive: GLFW path unchanged.
- Smoke system injects move+click and logs observed mouse pos + `wasClicked`.

## Gotchas

- Snapshot timing: inject before the INPUT-event snapshot, not after.
- Screen-pixel coords only (GLFW's space); downstream code converts.
- Don't store a manager pointer across frames (engine baseline).

## Verification

`fleet-build --target IRShapeDebug` (or a widget demo); run a temp smoke that
injects a move+click and asserts the logged mouse pos / `wasClicked`. Confirm a
plain `--auto-screenshot` run with synthetic input inactive is unchanged.
