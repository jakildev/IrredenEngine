# Plan: GUI hover/click/picking assertions + hovered-widget export (P3)

- **Issue:** #1796
- **Model:** opus
- **Date:** 2026-06-13
- **Epic:** #1793 — see `.fleet/plans/issue-1793.md` for full context
- **Blocked by:** #1795

## Verified current state

Widget state is per-widget on `C_WidgetState` (`hovered_`/`pressed_`/`focused_`/
`fireAction_`) with readers `IRPrefab::Widget::isHovered/wasClicked/isPressed`
(`widgets.hpp`). The z-ordered topmost hovered widget is computed in
`System<WIDGET_INPUT>::topHoveredId_` but is **private** — no exported accessor.
Picking exists (`VOXEL_PICKING` / `IRPrefab::Picking::castVoxelRay`). This phase
assumes P2 (#1795) scripted shots can drive input.

## Affected files

- `engine/prefabs/irreden/render/components/component_widget.hpp` (or a new
  `component_gui_hover_state.hpp`) — `C_GuiHoverState` singleton, OR a
  `IRPrefab::Widget::hoveredWidget()` accessor in `widgets.hpp`.
- `engine/prefabs/irreden/render/systems/system_widget_input.hpp` — publish
  `topHoveredId_` into the chosen surface in `endTick`.
- `engine/video/` (or a small assertion module) — assertion kinds + the
  machine-readable result log writer.

## Approach

- Export the hovered widget: prefer a `C_GuiHoverState` singleton written by
  `WIDGET_INPUT::endTick` (ECS-idiomatic, matches how singleton render state is
  exposed); fall back to an accessor only if a singleton entity is awkward.
- Assertion kinds evaluated at a shot's capture frame: `HOVERS(widget)`,
  `CLICK_FIRES(widget)` (`wasClicked`), `SLIDER_VALUE(widget, v, tol)`,
  `CHECKBOX(widget, b)`, `PICKS_VOXEL(worldCoord)` (compare `VOXEL_PICKING`
  result).
- Result log: one line per assertion (shot / kind / target / pass|fail /
  actual), machine-parseable like the render-compare report.

## Acceptance criteria

- A scripted shot asserts hover + click-fires + a slider/checkbox value, emitting
  one pass/fail line each.
- `PICKS_VOXEL` resolves a click to the expected voxel (alignment regression net).
- `C_GuiHoverState`/accessor returns the z-ordered topmost hovered widget.

## Gotchas

- Read post-tick state on a deterministic frame after a fixed settle.
- Script in screen pixels; the picking assertion is where the
  screen→iso/GUI-trixel mapping (and any reported alignment offset) surfaces —
  this is the intended place to pin that bug.

## Verification

Build the editor with a scripted shot that hovers a panel + clicks a control +
clicks a known scene voxel; confirm the result log shows the expected
pass/fail and is stable across runs.
