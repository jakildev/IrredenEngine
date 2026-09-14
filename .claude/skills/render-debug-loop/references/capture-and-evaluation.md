# Capture and evaluation

## Reproducible evidence

Use the configured build and native-run wrappers from
[BUILD.md](../../../../docs/agents/BUILD.md). Verify the demo’s supported arguments
and screenshot contract rather than assuming a particular zoom, subdivision or
shot sequence. If it lacks the required capture controls, extend it using the
[video module’s auto-screenshot helper](../../../../engine/video/CLAUDE.md).

Record the source revision (and patch for uncommitted experiments), backend,
resolution, command, scene state and shot labels. Match camera, light, geometry,
render mode and timing between paired captures unless that input is the variable.
Build/stage assets before launching; do not replace them during a run.

Preserve prior screenshots. Prefer a separate output directory when supported;
otherwise record the pre-run file list and identify the new files. Do not infer
run membership solely from the latest mtime or delete the baseline to reset a
counter. Retain the full captures and logs behind published crops.

A successful capture requires the wrapper’s clean-exit result. A crash, including
teardown, remains a failed run even when it saved images; those images may still
support diagnosis. See [clean-exit policy](../../../../docs/agents/FLEET.md).

## What counts as correctness

Inspect full frames for context and native-resolution crops for edge defects.
Use nearest-neighbor magnification for pixel inspection and label it. Contact
sheets help compare sweeps, but inspect suspicious frames individually.

An existing screenshot proves previous behavior, not intended geometry. Establish
an independent expectation where feasible: analytical bounds/projections, a known
primitive, a verified paired path, or the user’s explicit visual requirement.
Do not bless references or loosen a metric merely to make a change pass.
A presence test cannot prove silhouette, depth, normals or shadow correctness.

Use [VALIDATION.md](../../../../docs/agents/VALIDATION.md) to choose existing
validators. `scripts/render-compare.py` and `tools/img_diff` quantify image drift;
interpret their results against the intended change. When creating a new oracle,
exercise it against the known defect and a positive control. For RGB comparisons
of RGBA images, inspect RGB differences explicitly; unchanged alpha must not hide
changed color channels in a bounding-box calculation.

## Coverage proportional to the change

Select cases that exercise the changed contract, rather than requiring every
combination for every edit:

- Geometry/depth: known primitives and concavities, attached/detached paths,
  world placement and screen locking, occluding receivers and face boundaries.
- Coordinate transforms: cardinal and intermediate rotations, odd/fractional
  offsets, pan and relevant zoom/subdivision modes. Check the actual effective
  density; zoom does not universally imply a particular subdivision scale.
- Lighting: isolate relevant contributions and check caster shape, receiver depth,
  normal direction and contact. Record any geometry mismatch before attributing
  the result to filtering or bias.
- Temporal behavior: use a fine motion sweep when changing camera decomposition,
  scatter placement or sampling. Stills cannot establish stability. The renderer’s
  [temporal validation recipe](../../../../engine/render/CLAUDE.md) describes
  `tools/jitter_probe`; use the demo’s actual supported sweep flags.
- Backend changes: maintain GLSL/Metal contracts and run both when available.
  Shared wrong output is not correctness; an unavailable backend remains unverified.
- Scale: measure representative populations and asset density when claiming
  throughput. Separate CPU submission from GPU execution, report hardware and
  workload, and count allocations, uploads, dispatches and synchronization.
  One small scene with similar frame times does not establish scalability.

For publication, use the existing attach-screenshots and commit-and-push workflows
rather than duplicating their PR procedures. Clearly label diagnostic experiments
versus shipped behavior, and baseline-regression checks versus correctness oracles.
