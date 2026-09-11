---
name: attach-screenshots
description: >-
  Captures before/after screenshots for an engine rendering PR and stages
  them under `docs/pr-screenshots/<branch>/` so the PR body can embed them
  via raw GitHub URLs — runs an auto-screenshot-capable demo (default
  `IRShapeDebug`) against origin/master and against the dirty tree, or in
  `--two-ref` mode between two committed refs for the feedback-AMEND path,
  pairs the shots by label, and prints the markdown snippet. Use when the
  user says "attach screenshots" or a PR touches `engine/render/`,
  `engine/prefabs/irreden/render/`, any `.glsl`/`.metal` shader, or
  `creations/demos/*/src/`.
---

# attach-screenshots (Irreden Engine)

**The flow lives in [`docs/agents/skills/attach-screenshots.md`](../../../docs/agents/skills/attach-screenshots.md).**
Read it first, then apply the deltas below.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` |
| **raw URL base** | `https://raw.githubusercontent.com/jakildev/IrredenEngine` |
| **default branch** | `master` (`origin/master`); also refuse to run on `main` |
| **default demo** | `IRShapeDebug` |
| **visual-file globs** | `engine/render/**`, `engine/prefabs/irreden/render/**`, `engine/render/src/shaders/**`, `creations/demos/*/src/**`, `creations/demos/*/main*.cpp`, any `*.glsl` / `*.metal` |
| **screenshot output root** | `docs/pr-screenshots` |
| **demo save path** | `build/creations/demos/<demo-dir>/save_files/screenshots` (`fleet-run` cd's into the exe's directory, so `save_files/` lands beside the binary; the rotated `.prev.*` sibling is gitignored) |
| **sha-pin token** | `@COMMIT_SHA@` — `commit-and-push` step 8 substitutes `git rev-parse HEAD`; the feedback-AMEND `gh pr edit --body` step substitutes the post-amend pushed HEAD |
| **build tool** | `fleet-build --target <demo-name>` |
| **run tool** | `fleet-run <demo-name> --auto-screenshot 10` — no `--timeout` ([BUILD.md §Timeout choices](../../../docs/agents/BUILD.md#timeout-choices)) |

## Engine notes

- Demo target from directory: `creations/demos/shape_debug/` →
  `IRShapeDebug`. Shot labels come from the demo's `g_shots[]` array in
  `creations/demos/<demo-dir>/main.cpp`.
- Effect routing (shared-flow step 2, case 2): a diff in the sun-shadow /
  lighting / AO shader family — `ir_sun_*`, `c_bake_sun_shadow_map`,
  `c_clear_sun_shadow_map`, `c_compute_sun_shadow`,
  `ir_sun_shadow_sample`, `c_compute_voxel_ao`, `c_lighting_to_trixel`,
  `c_*light_volume*` — renders nothing visible in `shape_debug`. Use
  `IRCanvasStress` for **both** passes with `--debug-overlay
  shadow|ao|light_level --no-auto-rotate --no-spin` (overlay matching the
  effect; the freeze flags keep the pose comparable).
- Two-ref mode's detached HEAD is the one `fleet-pr-claim-feedback` /
  `fleet-pr-checkout-detached` leave you on.
- The demo picker knows only `creations/demos/*/`; game creations under
  `creations/game/` are out of scope.
- For "show me where the drift is", pipe a pair through `tools/img_diff`
  (red-on-grey diff image); for aggregate pass/fail use
  `scripts/render-compare.py`. Both consume this skill's PNGs.

### Camera-yaw fixes need a non-cardinal shot

The default `g_shots` cardinal sequence is byte-identical before/after for
yaw-only render fixes; the delta only shows at a non-cardinal yaw
(45°/30°). For a PR in the camera-yaw family, capture with a demo yaw flag
or yaw-sweep shot — a cardinal-only suite reads as "no visual delta".
