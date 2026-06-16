# Plan: render — occlusion-cull gating + verification (occlusion cull child 3/3)

- **Issue:** #1800 (child 3/3 of #1294)
- **Blocked by:** #1799 (needs the pre-pass to gate and verify)
- **Parent plan:** `.fleet/plans/issue-1294.md` — shared design, measurement gate, cross-system audit, constraints
- **Design:** `docs/design/voxel-occlusion-culling.md` § Implementation sketch steps 3–4 + § Latency/correctness budget
- **Model:** opus
- **Date:** 2026-06-13

## Verified current state

- Children #1798 (Hi-Z) + #1799 (chunk occlusion pre-pass) produce + apply the
  occlusion bit but it must ship **off by default** (design § Recommendation:
  sparse/solid scenes get zero payoff — `dense_set` measured 0.00 this session —
  so the default config must pay no cost).
- The cull is *approximate* (coarse, one-frame-lagged Hi-Z); on a camera cut the
  lag source is stale (design § 4). `C_RenderCamera` already tracks per-frame iso
  position, so a delta threshold gates a one-frame disable.

## Approach (single path)

1. `occlusionCullEnabled_` flag (default **false**) on `C_RenderCamera` or a
   render option. The pre-pass (#1799) early-returns when off → zero added cost
   in the default configuration.
2. One-frame disable after a camera-position delta over threshold (stale lag
   source on cut/teleport/first frame).
3. `render-verify` baselines proving **pixel-identical** output cull-on vs
   cull-off (fully-occluded voxels write nothing, so any diff is a cull bug — see
   parent plan / design § "load-bearing invariant").
4. **Measure + record** the realized `voxelStage1` ms reduction on `voxel_set`
   zoom8 (against the 0.97 ceiling) in the PR body.

## Affected files

- `engine/prefabs/irreden/render/components/component_camera_*.hpp` /
  `C_RenderCamera` — the `occlusionCullEnabled_` flag + camera-delta gate
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` (or the
  pre-pass system) — honor the flag (early-return)
- `render-verify` baseline fixtures
- `creations/demos/perf_grid/main.cpp` — optional toggle + intentional-drift note

## Acceptance criteria

- Cull-on vs cull-off **bit-identical** in `render-verify`.
- Realized `voxelStage1` reduction on `voxel_set` zoom8 recorded in the PR body.
  **If weak** (per-chunk granularity captures little of the 0.97 ceiling), file
  the **per-voxel** occlusion follow-on (design § 1) rather than forcing
  per-chunk — do not over-engineer chunk culling.
- Zero added cost when `occlusionCullEnabled_=false` (verify the default-config
  profile is unchanged from baseline).
- Builds clean; `render-debug-loop` + `attach-screenshots` on both hosts.

## Gotchas

- Document the one-frame-lag silhouette pop as **intentional drift** in the demo
  note so reviewers don't read it as a regression.
- The conservative-cull invariant (never cull a chunk whose shadow-feeder-widened
  footprint isn't fully Hi-Z-covered) carries through from #1799.
