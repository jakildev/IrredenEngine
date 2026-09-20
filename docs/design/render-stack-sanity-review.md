# Rendering stack sanity review — 2026-09-19

Reviewed the stack from PR 3519 through 3530, 3542, 3554, 3556 and 3559,
ending at `55d17503f60cb13bc988468b4ec017fd2ed9c7c7`, against master
`a1ebacc45`. Corrections live on `codex/render-stack-sanity`.

## Direction

The destination voxel lattice is the right geometry for revoxelized display,
receiver depth and shadow casting. Casting continuously rotated authored cells
against resampled receivers created false self-shadow. Removing that alternative
caster simplifies the pipeline without smoothing away real staircase geometry.
The independent face oracle separates connected face reconstruction from fidelity
to the authored solid; its passing result does not mean the resample is lossless.

Private-canvas producer ordering, shared depth clearing, rendered density and
owner-relative half-cell phase now have one documented contract. The owner scan
is batched rather than a lookup for each shape. Its per-frame hash allocation,
shape bucket reuse and redundant shape-only clears remain optimization candidates,
not evidence of million-entity throughput.

The neighboring hover correction (PR 3522) and culling identity/positive-fire gates
(PR 3547) complement this work. Their descriptions and scope were checked for
interaction; this is not a full implementation review of those independent PRs.
Recent merged AO, binding preservation, pivot and capture changes remain in the
stack's ancestry.

## Corrections

- The new yawed SDF lattice walk discarded `SHAPE_FLAG_HOLLOW`. Both backend
  implementations now reject interior samples below the shell bound. Larger,
  hollow and depth-colored marker controls support further investigation.
- The shadow metric accepted a blank image at yaw 22.5 because no expected
  interior was shadowed. It now requires an expected occluded interior as a
  positive control. The new regression test rejects this vacuous pass; executing
  the old comparator on the same fixture demonstrates that it passed before.
- Mixed-canvas documentation distinguishes measured unit-box behavior from
  untested rotated SDFs, density changes and the unresolved symmetric tie.
  Plain detached SDF textures still need face reconstruction.

## Validation

Host: Apple M4 Max, macOS, Metal, `macos-debug`, 2560x1440 captures.

- `fleet-build --target IRCanvasStress IrredenEngineTest` and `header-checks` pass.
- C++ test executable exits zero (1824 cases enumerated, 12 backend/GPU cases
  skipped); rendering Python suite: 153 tests pass. Ruff, comment-reference lint
  and changed-line formatting pass.
- Fresh asymmetric mixed-canvas unit markers at yaw 45: strict metric reports
  zero missing/extra/wrong-owner pixels, exact centroids, area ratio 1.0.
- Fresh cyan shadow at yaw 45: 0 missed pixels, 7448 false-shadow pixels, all
  classified grazing, maximum ray clearance 0.333 cells. The gate's 0.5-cell
  allowance is a fixture-level tolerance, not proof of perfect physical shadows
  or a bound valid for every sun-map resolution.
- Five-cell hollow boxes with depth colors at yaw 45 are byte-identical before
  and after the flag fix. Nearer shell cells hide the rejected interior samples;
  this is regression evidence, not a visible artifact fix or complete shell proof.
- Fresh normals capture is paired with the shadow capture: zero missing, extra
  or wrong-face pixels across 366592 foreground pixels.
- Every capture exited `RESULT=CLEAN`. PNGs are retained in
  `docs/pr-screenshots/codex/render-stack-sanity/`.
- `git merge-tree --write-tree origin/master HEAD` succeeds. This does not
  substitute for checking GitHub heads again before merging.

All captures use `--only revox --no-spin --no-auto-rotate --pivot-origin
--no-ao --subdivisions 1 --zoom 8 --auto-screenshot 10 --sweep-yaw
0.785398163 0.785398163 1`. Normals/shadow captures use `--focus-revox 1`
and their respective `--debug-overlay`; normals also uses `--no-shadows`.
Mixed captures use `--focus-revox 3 --focus-mixed-shape --mixed-shape-at
-8.5 -7.5 -8 --no-shadows --debug-overlay unlit`; the shell pair additionally
uses `--mixed-shape-size 5 --mixed-shape-hollow --mixed-shape-depth-color`.

## Merge and follow-up boundaries

The implementation direction is sound. Native OpenGL runtime remains untested
on this host; Linux build CI does not provide that evidence. Existing
instruction-size failures are in unchanged worker, fleet-label and video docs;
the separate PRs 3538 and 3543 address those files. Do not loosen their budgets
in this rendering stack.

The green CI perf result contains zero frame timings or a baseline-seeding
notice. It provides no GPU scaling conclusion. Before throughput claims, measure
CPU submission and GPU stages with rotation, subdivision and visible-population
sweeps, including shadow workload. Keep the following visual follow-ups explicit:

1. Plain detached SDF texture face reconstruction and non-unit/rotated SDF parity.
2. Resolve symmetric half-cell ties consistently in the renderer and oracle;
   the asymmetric passing control only isolates the problem.
3. Expand hollow-shape coverage, including the older cardinal lattice walk.
4. Derive shadow terminator tolerance from the actual sun-map footprint and test
   external receivers, different light directions and zooms.
5. Refresh stale visual references only after independent geometry checks; the
   existing CanvasStress reference mismatch is tracked by issue 3552.
