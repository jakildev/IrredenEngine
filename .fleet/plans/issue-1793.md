# Plan: deterministic GUI + mouse verification harness (epic)

- **Umbrella:** #1793 (`fleet:epic`)
- **Design:** `docs/design/gui-mouse-harness.md` (PR #1792) — authoritative cross-phase reference
- **Date:** 2026-06-13

## Context

Headless, deterministic driving + assertion of the trixel GUI (mouse moves,
hovers, clicks, widget state). The GUI sibling of `render-verify`. The render
side is already scriptable + deterministic (`--auto-screenshot`, fixed-step sim,
`render-compare.py`) and widget state is introspectable (`C_WidgetState`,
`IRPrefab::Widget::isHovered/wasClicked`). The missing primitive is synthetic
input — input is GLFW-only. This epic adds it and builds the harness + a
`gui-verify` skill on top.

## Phases / children

| Phase | Issue | Model | Title |
|---|---|---|---|
| P1 | #1794 | opus | synthetic mouse/click injection (`InputManager`/`IRInput`) |
| P2 | #1795 | sonnet | GUI-test shot tables (extend `AutoScreenshotShot`) |
| P3 | #1796 | opus | hover/click/picking assertions + `C_GuiHoverState` export |
| P4 | #1797 | sonnet | `gui-verify` skill |

## Dependency chain

```
#1794 (P1 injection)  ->  #1795 (P2 shot tables)  ->  #1796 (P3 assertions)  ->  #1797 (P4 skill)
```

Linear: each phase needs the previous primitive. P1 is the only unblocked head.

## Closing criteria

All four children verified-closed; `gui-verify` runs an editor GUI test table
headless with per-assertion pass/fail; the picking assertion (P3) provides a
regression net for the reported mouse-vs-render alignment offset.

## Steward ledger

reconciled-through: 2026-06-13
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #1794 | open | — | plan | 2026-06-13 |
| #1795 | open | — | plan | 2026-06-13 |
| #1796 | open | — | plan | 2026-06-13 |
| #1797 | open | — | plan | 2026-06-13 |

### Decisions

### Events
- 2026-06-13: filed via file-epic
