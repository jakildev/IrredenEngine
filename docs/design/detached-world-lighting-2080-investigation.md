# #2080 — detached canvases & world lighting: verify-first investigation

**Status:** verify-first complete; root cause routes into a known design-blocked
backend gap (#1640) + architect-scoping questions. Escalated `fleet:design-blocked`.

**Host:** macOS / Metal (the only host this worker can run; OpenGL side
unverified — see Q3).

**Issue premise (as filed):** detached canvases in `canvas_stress` read as flat
/ disconnected from world lighting — cast no floor shadow, receive no
shadow-driven face darkening. The plan's leading hypothesis was a **gating gap**
(the floaters not opting into `worldPlaced_` / `reVoxelize_`), fixable by making
world-lighting participation the default for detached solids and reusing the
existing #1375 / #1576 paths.

## What verification found

### 1. It is NOT a gating gap — participation is already the default (#1624)

`canvas_stress` detached re-voxelize canvases are spawned with
`screenLocked_ = false` (`spawnDetachedReVoxelizeSolid`, `main.cpp:533`), and
`PROPAGATE_CANVAS_ROTATION` derives `worldPlaced_ = !screenLocked_` +
`reVoxelize_` every tick. So they already satisfy the cast gate
(`gatherWorldPlacedCasters`, `system_bake_sun_shadow_map.hpp:444-475`:
`!screenLocked_ && visible_ && worldPlaced_ && reVoxelize_ && isDetached()`) and
the receive gate (`detachedWorldReceive.w != 0`, `c_lighting_to_trixel.glsl:139`).
**The plan's primary hypothesis (branch 2a) is refuted** — there is no demo flag
to set and no opt-in default to change.

### 2. The symptom reproduces cleanly, isolated against world-rendered twins

`canvas_stress --only compare,floor` spawns three labelled cubes side by side —
**GRID SPIN** (GRID re-raster into the world pool), **WORLD Z-YAW** (GRID), and
**DETACHED SO(3)** (`DETACHED_REVOXELIZE`, a detached canvas) — all on the same
SDF floor, same sun.

- Normal render (`compare_normal_grid-casts_detached-pasted-on.png`): the two
  GRID cubes each cast a distinct dark floor shadow and show shaded faces; the
  **DETACHED cube casts no floor shadow and reads as a uniform "pasted-on"
  silhouette.**
- Shadow debug overlay (`compare_shadow-overlay_detached-renders-raw-color.png`,
  black = lit / magenta = shadowed): the floor + GRID cubes false-colour
  correctly; the **DETACHED cube renders its RAW albedo** (green), i.e. its
  lighting pass is not participating in the same shadow visualisation as world
  geometry.

So the cast (and apparently the world-receive) **output** is missing for the
detached canvas, even though the gating is satisfied and the machinery is wired.

### 3. The cast machinery exists but produces no visible Metal output

`BAKE_SUN_SHADOW_MAP` gathers world-placed casters and resolves them into a
main-canvas-layout scratch before baking (`c_resolve_world_placed_depth`,
`system_bake_sun_shadow_map.hpp:~400-432`) — the resolve-then-bake pattern that
exists specifically to avoid the **#1640** Metal gap (binding a foreign
model-frame R32I distance texture as a compute read returns the clear value on
Metal). The receive path re-runs the cascade lookup at the recovered world pos
in `c_lighting_to_trixel.glsl:156-177`, gated on `detachedWorldReceive.w`.
Both are present in current `master`; neither produces a visible Metal result
for the detached cube. This is the same failure surface that block-listed
#1596 / #1626 / #1700 (all design-blocked on the #1640 R32I read + missing
GPU-test infra).

### 4. Confound: the SDF floor self-shadows with zero casters

`--only floor` (no casters at all) still paints a large diagonal magenta band
across the plate in the shadow overlay
(`floor-only_shadow-overlay_self-shadow-acne-no-casters.png`) — shadow acne /
self-occlusion on a flat top face. The band's presence is **scene-content
sensitive**: it is absent under `--only maingrid,floor`
(`maingrid-floor_shadow-overlay_grid-control.png`) and present under
`--only floor` / `--only revox,floor`. This suggests the sun-shadow bake's
depth range / bias shifts with what world-pool geometry is present, and it
muddies any "does the detached solid cast?" read taken from the floor alone.
It is a sun-shadow-bake issue distinct from the detached-canvas integration and
likely out of scope for #2080 — flagged for the architect.

## Reproduce

```
# build
fleet-build --target IRCanvasStress
cd build/creations/demos/canvas_stress
# smoking gun (GRID casts, DETACHED does not):
./IRCanvasStress --only compare,floor --auto-screenshot 10      # last shot = compare_yaw0
# same in shadow overlay (DETACHED renders raw colour):
./IRCanvasStress --only compare,floor --debug-overlay shadow --auto-screenshot 10
# floor self-shadow confound (no casters):
./IRCanvasStress --only floor --debug-overlay shadow --auto-screenshot 10
```
Auto-screenshot filenames use a monotonic counter that does **not** reset per
run — the per-run shot index N maps to `base + N + 1`, not a fixed file number.

## Open design questions (see PR NEEDS-DESIGN)

1. Is the detached-cast root the #1640 Metal R32I gap (then this is blocked on
   that + GPU-test infra, #1771), or is there a Metal-tractable path the resolve
   pass is missing?
2. Is the world-**receive** failure separable from cast (receive does not use the
   foreign R32I read — it re-runs the cascade in the lighting shader), i.e. is
   there a tractable receive-only fix even while cast stays #1640-blocked?
3. GL/OpenGL is unverified here — does the cast/receive already work on GL (making
   this a Metal-only backend gap), and how should that gate worker assignment
   (cf. #1998)?
4. Is the floor self-shadow acne (finding 4) in scope for #2080 or a separate
   issue?
5. Sequencing vs. the C-series (#2081–#2083) — the plan flags these as parallel;
   a newly-participating detached caster should cast a *clean* (C-series-aware)
   shadow, not a ragged one.
