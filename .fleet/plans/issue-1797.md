# Plan: gui-verify skill (P4)

- **Issue:** #1797
- **Model:** sonnet
- **Date:** 2026-06-13
- **Epic:** #1793 — see `.fleet/plans/issue-1793.md` for full context
- **Blocked by:** #1796

## Verified current state

`render-verify` (`.claude/skills/render-verify/SKILL.md` + the shared flow +
`scripts/render-verify.py` / `scripts/render-compare.py`) is the build → run →
capture → compare → report pattern to mirror. After P1–P3 land, a creation can
script GUI input and emit a per-assertion result log; this phase wraps that into
a reusable skill.

## Affected files

- `.claude/skills/gui-verify/SKILL.md` (+ a shared flow under
  `docs/agents/skills/` if mirroring the skill-sharing mechanism).
- `scripts/` — a thin runner if needed (prefer reusing `render-compare.py` for
  region image diffs; the assertion pass/fail comes from P3's result log).
- A first editor GUI test table (worked example).

## Approach

- Skill flow: build the target, run it headless with the creation's
  `GuiTestShot[]` table, parse P3's result log for per-assertion pass/fail, and
  (optionally) region-diff a GUI canvas crop via `render-compare.py` for visual
  regressions (e.g. the help panel).
- Author `SKILL.md` as a thin wrapper over a shared flow (mirror `render-verify`
  + the skill-sharing mechanism) so the game repo can adopt it without drift.
- Add a first editor GUI test (hover a panel → assert help text; click a swatch →
  assert selection).

## Acceptance criteria

- `gui-verify` runs a creation's GUI test table headless, prints per-assertion
  pass/fail, exits non-zero on failure (agent/CI parseable).
- At least one editor GUI test passes deterministically across runs.
- Region image-diff path works on a GUI canvas crop.

## Gotchas

- Reuse `render-compare.py`; don't add a second comparator.
- Keep the skill thin-wrapper + shared-flow per the skill-sharing mechanism.

## Verification

Run `gui-verify` on the editor test table twice; confirm stable pass and a
deliberately-broken assertion produces a non-zero exit + a clear failing line.
