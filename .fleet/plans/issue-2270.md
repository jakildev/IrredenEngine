# issue-2270 — moth-eaten cardinal sun-shadow cast (coverage splat)

**Model:** opus · **Area:** engine/render (sun-shadow bake) · **Blocked by:** (none)

The cardinal cast shadow on the floor shatters into a point-scatter of tiny
disconnected fragments (moth-eaten / dashed) at a near-overhead sun. Root cause,
the full refutation history (density ratio, per-pixel neighbour walk, down-ray
extrusion — all measured and refuted), and the settled fix are in the
engine-level design doc:

**`docs/design/sun-shadow-bake-coverage.md`** — read it before touching the bake.

## Fix (this PR)

`c_bake_sun_shadow_map.{glsl,metal}` `atomicMin`s each cardinal single-canvas
caster's depth into a bounded **(2·r+1)² uniform box** of sun texels
(`bakeCascadeBox`), reaching the 2-D-scattered coverage the directional
derivations provably cannot. `atomicMin` gives saturated-host byte-identity
(no-op where geometry is dense); `FrameDataSun.sunSplatMaxTexels_` is the radius
(default 6, measured minimum with margin) and the kill switch (0 → exact
single-write path). Gated to the cardinal branch, so per-axis / smooth-yaw stay
byte-identical.

## Acceptance

- `canvas_stress` shot `shadow_overlay_floor`, `render-shadow-metric.py --roi
  1010,540,450,250`: components ≤ 8, largest_frac ≥ 0.9 (measured 1 / 1.0).
- `IRVoxelYaw zoom4_yaw0` solid; per-axis `yaw30`/`yaw45` byte-identity.
- Owed `fleet:needs-linux-smoke` (authored on macOS/Metal; GL side unverified).
