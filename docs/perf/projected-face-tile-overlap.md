# Projected-face tile rejection: deferred experiment

**Do not enable this predicate by default on the present evidence.** The native
controls show no demonstrated frame-time benefit after cooperative indexing.
Production shaders remain unchanged. The [prototype patch](projected-face-tile-overlap/prototype.patch)
retains the rejected implementation and its independent clipping oracle so the
experiment can be resumed without reconstructing it.

## Geometry

A face's projected bounding rectangle includes tiles that cannot intersect its
finite parallelogram. After AABB selection, the two edge-normal separating axes
can reject those tiles. The face's radius on either unnormalized normal is
half the absolute edge determinant. The tile's support is the sum of its half
extents weighted by the normal's absolute components. Strict separation keeps
closed-edge contact; a scale-dependent float allowance retains uncertain cases.
This affects index candidates only, never projected geometry or shadow blur.

The prototype enables rejection only in the cooperative box specialization;
scalar voxel indexing is unchanged. Both shader adapters were compared with
independent double-precision polygon clipping across rotations, reflections,
shears, nearly parallel edges, fractional grid origins/cell sizes, and world
translations of −1,000,000, 0 and +1,000,000.

Each backend checked **784,209 candidate tiles**, with 251,338 true
intersections. The predicate rejected 485,459 candidates and conservatively
retained 47,412 extras, with **zero false negatives in these fixtures**.
That is a test result, not a proof for arbitrary floating-point inputs.
Missing an axis, shrinking the face, using open boundaries, or disabling
rejection is caught by mutation controls. The cooperative record/partition
suite also passed with the candidate enabled.

## Native measurements

Apple M4 Max, macOS 26.5.2, Metal Debug, AC, 2560×1440. Same binary and
runtime scripts; original, candidate, then restored-original matrices.
Each matrix uses two forward/reverse rounds of `box_shadow_controls.py`,
275 frames and two captures per run: 24 native runs in total.

| Case | Original frame / box stage ms | SAT frame / box stage ms | Restored frame / box stage ms |
|---|---:|---:|---:|
| span18-count1 | 9.530 / 0.155 | 9.500 / 0.179 | 9.405 / 0.212 |
| span128-count1 | 9.525 / 0.114 | 9.520 / 0.130 | 9.535 / 0.091 |
| span512-count1 | 9.640 / 0.431 | 9.530 / 0.397 | 9.625 / 0.431 |
| span18-count65 | 9.515 / 0.280 | 9.485 / 0.294 | 9.495 / 0.279 |

The large-box stage decreases by about 0.034 ms; the small/count controls do
not show a consistent stage benefit. Frame differences are too small to
justify the added shader work in this matrix. These capture-assisted Debug
means include startup/readback stalls. Stage means are sampled invocation
timings, not additive frame budgets. No native OpenGL result is available.

[Raw reports and manifests](projected-face-tile-overlap/) retain all arms.
All 16 candidate screenshots are RGB-identical to the corresponding original
captures. [Representative pairs](../pr-screenshots/codex/projected-face-tile-overlap/README.md)
show unchanged appearance, not a visual fix. Source and runtime staging were
restored to the parent implementation after the experiment.

## Reproduction

The patch targets source at `40b7d3febe56d6f2e15863ffeceee481a20e362d`;
`git apply --check` passed after restoration. Run the commands below from a
dedicated worktree of this evidence branch, whose engine source is unchanged
from that base. If checking out the pinned base instead, copy the patch first
and apply it by absolute path: the base predates this documentation artifact. It contains the
four shader edits, host vector adapter and full new test. Apply it only when
explicitly resuming this experiment; it is not part of the shipped renderer.

```bash
git apply docs/perf/projected-face-tile-overlap/prototype.patch
python3 scripts/tests/test_render_projected_face_tiles.py
python3 scripts/tests/test_render_cooperative_face_index.py
fleet-build --target IRCanvasStress -j3
python3 scripts/perf/box_shadow_controls.py --rounds 2 --output /tmp/tile-rejection-candidate
```

Capture a fresh original matrix before applying the patch. A restored-original
comparison must stage the original shaders too; changing source without
rebuilding/staging does not establish the runtime control.

## Next experiment

The existing coincident-caster arm stresses real overlap. It does not isolate
false-positive tile pressure. Add a native fixture of distinct oblique casters
whose bounding rectangles share receiver tiles but whose actual faces miss
those tiles. Measure relevant candidate counts, incomplete tiles and receiver
query time, and compare the exact shadow against an independent finite-face
oracle. That can establish whether rejection preserves exact shadows where
irrelevant candidates otherwise trigger sampled fallback.

Only then consider precomputing per-face support coefficients, targeting
large/slender faces, or enabling rejection more broadly. Saturated-list
atomics remain a separate experiment; a larger global record buffer does not
solve the tile limit. Retain the cooperative scheduling improvement already
measured in [the preceding optimization](cooperative-box-face-index.md).
