# Rendering stack visual evidence

These are the retained Metal captures from integrated head
`75a4729d69437b1eee7e2aa23c1c553bfb5f3d40`, collected on an
Apple M4 Max Mac (`macOS-26.5.2-arm64-arm-64bit-Mach-O`). All 19 runs used the
same binary, shader, and runtime-script hashes recorded in `capture-witness.json`.
The primary matrix holds 91 original 2560 × 1440 PNGs, 19 run manifests, 19 screenshot
maps, and compressed raw logs. Each run logged a clean exit and the expected
number of saved screenshots. `command.txt` contains the exact capture script.

`metric-results.json` records 20 passing source-face oracle views: four
identity-frame sun-beauty views, plus four cardinal-angle views each for frame
shadow, frame normals, octahedron shadow, and octahedron normals. Every tested
interior reported zero missing/extra pixels, wrong normals, false/missed shadows,
and invalid overlay pixels; the shadow views exercised real occlusion.
`paired-comparisons.json` records 31 exact RGB matches across mixed lighting,
no-shadows, sky-only, subdivision 8, parked visibility, and released visibility
cases, plus four exact frame occupancy-mask matches. The stack's default-off
captures have the per-axis lighting and visibility env switches unset. The
fresh comparison against master `d29d1cce` is retained under
`master-comparison/`; see the acceptance discussion below.

The source-face oracle deliberately excludes a one-pixel silhouette/face-edge
band and a one-pixel shadow-transition band. Its pass result certifies tested
interiors, not every edge pixel. Some orbit geometry shows visible banding;
this package does not accept that banding as correct or claim it is fixed.
The exact RGB control pairs establish feature-toggle parity only for their
captured scenes and views. No performance conclusion is drawn from these
single-run visual captures.

The per-case `manifest.json` gives command, environment, and host details;
`screenshots.json` maps each numbered image to the original screenshot filename.
`run-1.log.gz` preserves the raw camera/screenshot/clean-exit witness. See
`visual-acceptance-report.md` for the inspected views and oracle boundary counts.

## Integration validation

- The renderer CPU/shader harness passes all 57 suites. The restored query-gate
  mutations pass 20,736 GLSL and 20,736 Metal cases and reject each invalid gate.
- Native Metal builds of IRCanvasStress and IrredenEngineTest pass. All 69
  selected sun, per-axis and capture-witness native tests pass, including actual
  GPU clear/readback.
- PR #3850 Linux Engine Tests run `36663599372` passes, including the new hidden
  OpenGL 4.5 GPU readback fixture. This does not establish native OpenGL visual
  parity for the complete stack. Windows presentation is unmeasured here.
- The first broad `render-verify --all` run reports 106 checks, 23 failures and
  no skips. It is not a green suite: eight CanvasStress RGB references change;
  twelve PerfGrid and three Fog checks also fail.

## Master comparison and receiver attribution

Fresh master and integrated captures use the same machine, commands and
resolution. `master-comparison/comparison.json` records every frame comparison,
commands and binary/image hashes; selected complete frames and compressed logs
are retained alongside it. The 27 no-shadow CanvasStress frames match exactly,
and all seven PerfGrid frames match exactly. Thus the PerfGrid reference drift
is inherited. The detached-fog control also matches exactly. The other two fog
cases retain a large mismatch against their references on master; the stack's
additional local deltas are approximately 4,900 pixels with maximum RGB-channel
change 32. Their references have not been changed.

The CanvasStress reference deltas are isolated with staged-shader controls:
replacing the regular and overflow receiver shaders with master's restores all
12 default frames and both gated compare frames exactly to master. Replacing
only the caster shader changes no default-frame pixels. The staged shaders were
restored and checked against the integrated sources after the experiment.
`master-comparison/shader-ab-comparison.json` records all comparisons. These
controls attribute the change to receiver sampling; image equality alone does
not prove correct shadows.

The default screenshot table logs the requested yaw before settling. Automatic
camera rotation remains enabled during the 60 settling frames, so a requested
cardinal yaw does not certify a cardinal capture. Frozen controls explicitly use
`--no-auto-rotate` and a fixed entity pose.

A digitized rotating cube has stepped geometry even when its source shape is
convex. An independent finite-voxel ray calculation finds no self-occluded
sun-facing face centers at 0 or 90 degrees, but at 30 degrees finds 60/192 -X
and 66/192 -Y centers occluded by other occupied cells, with 0/144 top centers
occluded. This supports legitimate patterned self-shadow on GRID geometry; it
is not a pixel-by-pixel certification of the complete scene or approximate
incomplete-index fallback. The default path still samples once per face.

The initial frozen shadow controls also expose a diagnostic limitation: overflow
faces retain albedo instead of shadow colors. Those initial captures are retained
as pre-fix evidence, not accepted shadow-oracle results.

## Final diagnostic correction and acceptance

Regular and overflow receivers now share `surfaceShadowDebugColor`. Overflow
shadow mode queries the same receiver and preserves alpha before returning;
normal beauty, other diagnostic routes and engine defaults are unchanged. The
executable GLSL/Metal adapters cover query and write branches, disabled shadows,
normals, disabled lighting and empty dispatches. Restoring the old early-return
condition fails the test on both backends.

[Independent physical review](grid-ray-after/physical-review.md) records the
post-fix controls. All 27 beauty and two normal captures are RGB-identical before
and after this diagnostic correction. Both shadow captures contain only black
and magenta. The upright red cube has no shadow pixels in the conservative ROI;
its rotated counterpart has side shadows and zero top shadow pixels, consistent
with the independent ray expectation. All occupied source-index tiles are
complete (maximum 36 candidates, capacity 64), so this control does not rely on
the incomplete-tile fallback.
Each frozen run captures one shot and writes probe zero; only that run's
`sun-face-index-0.csv.gz` is retained. Unused probe files left by earlier demo
runs are excluded from this package and its acceptance analysis.

Only the eight Metal CanvasStress RGB references are refreshed, with unchanged
thresholds. The [acceptance report](master-comparison/acceptance-report.md)
documents this as the intended face-center receiver behavior, not a universal
edge-correctness claim. Fog and PerfGrid references remain untouched. The
post-fix capture manifests retain the six changed shader files and all staged
shader hashes; the executable is unchanged from the integrated runtime head.

The final `render-verify --target IRCanvasStress` run passes all 11 checks:
eight exact RGB references and all three structural shadow gates. The broader
suite's remaining 15 fog/performance reference failures remain explicitly
unaccepted above; the full suite is not represented as green.
