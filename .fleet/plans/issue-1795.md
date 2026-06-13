# Plan: GUI-test shot tables with scripted input (P2)

- **Issue:** #1795
- **Model:** sonnet
- **Date:** 2026-06-13
- **Epic:** #1793 — see `.fleet/plans/issue-1793.md` for full context
- **Blocked by:** #1794

## Verified current state

`AutoScreenshotShot` (`engine/video/include/irreden/video/auto_screenshot.hpp`)
carries per-shot zoom / camera / yaw / ROI-crop; `createAutoScreenshotSystem`
(`engine/video/src/auto_screenshot.cpp`) cycles shots with warmup + settle frames
then closes the window. There is no input scripting today. This phase assumes P1
(#1794) has landed the `IRInput::inject*` API.

## Affected files

- `engine/video/include/irreden/video/auto_screenshot.hpp` — add `GuiInputEvent`
  + `GuiTestShot`.
- `engine/video/src/auto_screenshot.cpp` — fire a shot's scripted input events on
  scheduled frames via `IRInput::inject*` before capture.
- A creation (voxel editor) shot table as the demonstration.

## Approach

- `GuiInputEvent { int frameOffset_; enum {MOVE,PRESS,RELEASE,SCROLL} type_;
  ivec2 screenPx_; vec2 scroll_; }`; `GuiTestShot { AutoScreenshotShot render_;
  const GuiInputEvent* inputs_; int numInputs_; }` (superset of the existing
  shot so camera/crop config carries over).
- In the capture system, after a shot's camera is applied, fire each input event
  on `settleStart + frameOffset_` via P1's inject API; ensure remaining settle
  frames elapse after the last event before the screenshot.
- Frame-scheduled only — no wall-clock.

## Acceptance criteria

- A `GuiTestShot[]` table moves the cursor to a scripted point, clicks, and
  captures deterministically (identical across runs).
- Demonstrated on the voxel editor (move to a palette swatch, click, capture).

## Gotchas

- Keep `GuiTestShot` a strict superset of `AutoScreenshotShot`.
- Last-event-to-capture gap must be ≥ settle frames so the GUI reflects the
  interaction.

## Verification

Build + run the editor (or a widget demo) with a `GuiTestShot[]` table via the
auto-screenshot path twice; confirm the captured frames are byte-stable and the
scripted click visibly affected the widget.
