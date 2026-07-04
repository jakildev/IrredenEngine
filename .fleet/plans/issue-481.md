## Plan: Render — add unit/baseline coverage for GPU light-volume system

- **Issue:** #481
- **Model:** sonnet — the scene, the `--auto-screenshot` shot table, and the
  CMake target all already exist on master; this is a manifest + captured-
  reference add wired into the existing render-verify harness — no new render
  code, no architectural decision. The one judgment call (confirm the lit scene
  renders correctly before blessing) is a standard render-verify step.
- **Date:** 2026-07-04

### Verified current state (checked against master)
- The GPU light-volume path (`System<COMPUTE_LIGHT_VOLUME>` +
  `c_seed_light_volume` / `c_propagate_light_volume` / `c_clear_light_volume`,
  both GLSL and Metal) has **zero** automated coverage — the only exercise is
  manual screenshot capture.
- The `lighting` demo already drives the light volume
  (`lighting_demo_scene.hpp` registers `COMPUTE_LIGHT_VOLUME`; `detail::kShots[]`
  gives a `--auto-screenshot` shot table). The demo had **no** `test/references/`
  dir — a clean add.
- `IRLightingSdfBlocker` (`main_sdf_blocker.cpp`) is a fully static single-
  point-light scene: a fixed point light at (34,-7,-1) with an SDF
  `C_LightBlocker(blocksLOS_=true)` wall between the light and the floor. One
  deterministic frame exercises seed + propagate (distance falloff) + LOS
  occlusion, and the single light means seed texels can't collide.
- render-verify (`scripts/render-verify.py`) is manifest-driven; per-backend
  reference PNGs under sibling `macos-debug/` & `linux-debug/` dirs. A backend
  without committed refs SKIPS (never fails). `fog_demo`'s manifest is the
  closest template.

### Why Option B (render-baseline), not Option A
Option A ("CPU-mockable propagate-kernel reference asserted against a GPU debug
readback") depends on a headless GPU readback / unit-test category not on master
(issue #1771, deferred). Option B rides entirely on shipped render-verify infra
and is the issue's own recommended lower-effort first step. Option A remains a
good strictly-additive follow-up once #1771 lands (file separately).

### Approach (Option B — one target, deterministic scene)
1. Add `creations/demos/lighting/test/references/manifest.json` modeled on
   fog_demo's: `target` IRLightingSdfBlocker, a curated shot subset, warmup 10,
   fog_demo thresholds, a `notes` field.
2. Build + run + visually confirm correctness FIRST (warm point-light gradient
   on the floor + cold shadow band behind the wall). Bless only once the scene
   reads correctly (verify-then-bless, not author-from-scratch). If wrong on
   master, STOP and escalate.
3. Bless the host backend: `render-verify.py … --update-references`, then re-run
   without it to confirm green.
4. Commit the manifest + that backend's `test/references/<backend>/*.png`.

### Affected files
- `creations/demos/lighting/test/references/manifest.json` — **new**.
- `creations/demos/lighting/test/references/macos-debug/*.png` — **new**;
  blessed Metal references.
- `creations/demos/lighting/common/lighting_demo_scene.hpp` — **modified** (see
  deviation 1 below).
- `.fleet/plans/issue-481.md` — **new**; this plan.

### Acceptance criteria
- `render-verify.py --target IRLightingSdfBlocker --demo lighting` passes green
  on the blessed backend.
- A deliberate regression in the seed/propagate/occlusion path fails the
  thresholds (sanity-checked by perturbing a light-volume input, confirming red,
  reverting).
- `IRShapeDebug --auto-screenshot` + the other lighting targets still build/run.

### Gotchas
- Per-backend refs (macos-debug/ vs linux-debug/); this task blesses macOS only,
  linux SKIPS until a Linux host blesses it (`platform-catchup` / sibling task).
- Single-light `IRLightingSdfBlocker` scene only (avoid seed-collision flakiness).
- Confirm bit-determinism (double-capture + `cmp`) before blessing.
- Don't resurrect the deleted CPU `fillPointLight`/`fillSpotLight` helpers.
- `--demo lighting` is mandatory (else render-verify can't find the manifest).

### Implementation deviations (as-built)
1. **Perf-stats overlay suppressed during `--auto-screenshot`.** The plan
   assumed the scene was directly blessable, but `lighting_demo_scene.hpp`
   unconditionally added `PERF_STATS_OVERLAY`, which burns live wall-clock
   FPS / GPU-stage timings into every frame — non-deterministic
   (`renderFps`/`renderFrameTimeMs` are wall-clock, GPU-stage timers are wall-
   clock), so it would flake any pixel reference. Gated the overlay behind
   `g_autoWarmupFrames == 0` so it is present for interactive + `--auto-profile`
   runs but suppressed only while capturing. This matches fog_demo / shape_debug
   (which omit the overlay from gated scenes) and unblocks render-verify for the
   whole lighting family. The scene pixels are unchanged; determinism was
   re-verified (all 6 kShots byte-identical across two clean runs).
2. **Shots are the kShots[] positional prefix, not the literal [zoom2,4,8]
   subset.** render-verify maps captured shots to reference labels by
   **position** (first-N `screenshot_<index>.png`), not by label — the demo
   captures all 6 kShots, so an arbitrary subset would mislabel captures.
   `kShots[]` is shared across all 15 lighting targets (editing it is out of
   scope), so the manifest gates the positional prefix through `zoom8_origin`:
   `[zoom1_origin, zoom2_origin, zoom4_origin, zoom4_offset_3_5, zoom8_origin]`.
   This gates the plan's targets (zoom2/4/8) and excludes only the over-
   magnified `zoom16_origin` (the plan's stated intent), which is bit-
   deterministic here but hypersensitive to benign sub-pixel change.
