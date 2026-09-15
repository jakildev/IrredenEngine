# Finite shadow casting defaults

Voxel casters use finite authored faces by default. Detached resampling does
not change the source silhouette used for casting. Receiver positions retain
the [surface sampling contract](surface-shadow-sampling.md).

`BAKE_SUN_SHADOW_MAP::beginVoxelFaceCoverage` opens one shared cascade frame
from the first voxel or analytic producer. Subsequent producers reuse it;
the bake system closes it even when its entity query is empty. Shape-only
pipelines therefore do not depend on a voxel tick to initialize shadows.

Analytic boxes contribute finite faces. Other analytic shapes retain their
depth-based casting fallback, consumed immediately per eligible world canvas.
The reusable depth scratch grows to the maximum canvas dimensions and uses
the physical texture width for Metal atomic indexing. Its frame-data binding
is separate from the resident voxel frame buffer, which is restored after use.

## Compatibility

Registered GPU or stateless particle renderers select the legacy depth caster
for the whole frame: those producers do not yet emit finite faces. Registration
is deliberately conservative, including a registered but inactive producer.
Custom depth-only caster producers must set `voxelFaceCoverage_ = false` until
they join the finite producer lifecycle.

IRCanvasStress exposes `--legacy-depth-shadows` for comparison and
`--voxel-face-shadows` for resampled-face diagnostics. `--source-face-shadows`
selects the default explicitly. Raw trixel display remains debug-only.

## Evidence

Native macOS Metal Debug captures are retained in
`docs/pr-screenshots/codex/finite-shadow-defaults/`.

- Capture 791: the four rotated GRID solids at zoom 3, yaw 225 degrees,
  frozen pose 0.6. RGB-identical to the earlier explicit finite-caster control
  751; switching the default preserves its filled shadows.
- Captures 796–798: two independent shadow-only analytic canvases, dimensions
  128 and 256. The combined capture 796 is RGB-identical to the darker-pixel
  union of independent captures 797 and 798. The casters intentionally have
  no composite, isolating their shadows on the main floor.
- Captures 799–806: detached revoxelized and attached geometry at eight camera
  yaws. These are visual regression evidence, not a numerical shadow oracle.
- Captures 807–814: analytic box and floor with and without the voxel pass;
  each corresponding pair is RGB-identical. The shape-only probe explicitly
  clears the device's atomic depth scratch.
- IRStatelessParticles builds and completes its four-shot native smoke run
  through the automatic compatibility fallback. This is a lifecycle smoke,
  not a deterministic particle-shadow oracle.

OpenGL runtime validation remains for the other host. Existing geometric
staircase teeth and finite sun-map quantization are not blurred away.

## Performance follow-up

`voxelSunFaces` has a dedicated CPU/GPU scope; analytic casting remains in
the `shapePass1` bundle. See the [timing contract](gpu-stage-timing-cost-model.md)
before summing stage rows. Initial repeated Debug frame measurements at 64³
voxels were approximately 9 ms cardinal (NONE) and 22 ms at 45 degrees
(FULL, base density 1), both at zoom 1. These are
different rendering paths, not a before/after optimization comparison.

The next matrix must vary camera yaw, entity rotation, subdivision mode,
effective subdivision density and zoom, including matched screen coverage.
Track intermediate allocations and generated/visible work rather than only
final screen pixels. Cull off-screen camera work without dropping off-screen
casters that can shadow visible receivers. Preserve the geometric screenshots
when changing compaction, overflow or subdivision expansion.

A Metal startup timestamp with an unwritten start contaminated one rotated
stage's statistics. Repair timestamp validity and retain repeatable CPU/GPU
reports before using those stage numbers to choose an optimization. No GPU
speedup is claimed by this default switch.
