# Cooperative tile carry stepping: deferred experiment

**Do not enable this iterator on the present evidence.** Removing per-tile
integer division did not improve the targeted large-box workload. The
[prototype patch](shadow-tile-stride/prototype.patch) preserves both shader
edits and the exhaustive partition test; production shaders retain their
existing iterator. The added irregular-width concurrent fixtures remain in
`test_render_cooperative_face_index.py` as correctness coverage.

## Candidate and contract

The cooperative box indexer visits flattened rectangle entries `lane + 64k`.
The candidate computes each lane's initial quotient/remainder and the stride's
quotient/remainder once, then advances column and row with a carry. This
preserves each lane's tile ownership and visit order. Scalar voxel indexing,
record allocation, tile reservations, raw requested-reservation counters,
capacities and finite-face geometry are unchanged. Small rectangles pay extra
setup and carry work; fewer arithmetic operations in a long loop do not alone
establish a faster GPU pass.

## Native result

Apple M4 Max, macOS 26.5.2, Metal Debug, battery power, 2560×1440. Each suite
uses two rounds, forward then reverse case order, with 275 frames and two
captures per run. Baseline span and oblique matrices precede candidate span
and oblique matrices. All **32 runs** are retained. Both arms use the same
binary and runtime scripts at `2183e854875dfa003f35fc03d30f198d1cf624bb`;
only runtime shader staging changes.

| Case | Baseline frame ms | Candidate frame ms | Baseline box stage ms | Candidate box stage ms |
|---|---:|---:|---:|---:|
| span18-count1 | 11.350 | 9.790 | 0.152 | 0.087 |
| span128-count1 | 9.890 | 9.525 | 0.099 | 0.120 |
| span512-count1 | 9.680 | 9.735 | 0.425 | 0.424 |
| span18-count65 | 9.450 | 9.555 | 0.322 | 0.296 |
| count1-yaw0 | 9.480 | 9.630 | 0.104 | 0.137 |
| count1-yaw45 | 9.505 | 9.560 | 0.167 | 0.151 |
| count64-yaw45 | 9.415 | 9.435 | 0.260 | 0.242 |
| count128-yaw45 | 9.490 | 9.580 | 0.443 | 0.474 |

The primary `span512-count1` box-stage ranges overlap almost completely:
0.422–0.429 ms baseline and 0.420–0.428 ms candidate. The dense oblique
128-box stage increases from 0.443 to 0.474 ms. Smaller cases are mixed.
There is no supported optimization to retain.

The first `span18-count1` baseline run has a 13.21 ms all-frame mean and a
1008.86 ms maximum, with a 9.44 ms steady mean. This startup outlier remains
in the table and raw reports; that case's two-run baseline steady mean is
9.565 ms, versus 9.970 ms candidate. The all-frame decrease is not a claimed
speedup. These are small capture-assisted Debug samples: stage rows are
sampled invocation means, not additive frame budgets, and GPU envelope is
not busy time. Native Windows/OpenGL remains unmeasured.

[Span baseline](shadow-tile-stride/span/baseline/summary.md),
[span candidate](shadow-tile-stride/span/candidate/summary.md),
[oblique baseline](shadow-tile-stride/oblique/baseline/summary.md) and
[oblique candidate](shadow-tile-stride/oblique/candidate/summary.md) retain
case definitions, GPU summaries, and every run's manifest and profile report.
Logs and the complete screenshot set are omitted from this compact archive.

## Correctness evidence

All 32 corresponding capture pairs are RGB-identical. The
[representative pairs](../pr-screenshots/codex/shadow-tile-stride/README.md)
show unchanged appearance, not a shadow-quality fix.

Both backend adapters of the candidate pass every width and height from 1 through
128 at low and high grid origins: 32,768 rectangles and 136,323,072 tile
visits per backend preserve lane ownership and order. Five carry/initial-state
mutations fail. The existing concurrent adapter also passes with narrow
width-3, width-65 and exhausted-record width-65 rectangles, including their
far-cascade coverage. Scalar complete-buffer and atomic-workload controls
remain unchanged and pass. Host adapters do not prove GPU memory ordering.

## Reproduce

Use a configured dedicated worktree at the baseline above, or the evidence
branch with unchanged production index shaders. Copy the patch to an external
path first when checking out the pinned baseline, which predates this artifact.
Finish each build before starting a matrix; never change staging during a run.

```bash
fleet-build --target IRCanvasStress -j3
python3 scripts/perf/box_shadow_controls.py --suite span --rounds 2 --output /tmp/shadow-tile-stride-span-baseline
python3 scripts/perf/box_shadow_controls.py --suite oblique --rounds 2 --output /tmp/shadow-tile-stride-oblique-baseline
git apply --check docs/perf/shadow-tile-stride/prototype.patch
git apply docs/perf/shadow-tile-stride/prototype.patch
python3 scripts/tests/test_render_face_tile_partition.py
python3 scripts/tests/test_render_cooperative_face_index.py
python3 scripts/tests/test_render_source_face_index.py
fleet-build --target IRCanvasStress -j3
python3 scripts/perf/box_shadow_controls.py --suite span --rounds 2 --output /tmp/shadow-tile-stride-span-candidate
python3 scripts/perf/box_shadow_controls.py --suite oblique --rounds 2 --output /tmp/shadow-tile-stride-oblique-candidate
git apply --reverse docs/perf/shadow-tile-stride/prototype.patch
fleet-build --target IRCanvasStress -j3
```

Use the copied absolute patch path for the three `git apply` operations when
running at the pinned baseline. The final rebuild restores production runtime
staging as well as source. This experiment does not supersede the measured
[cooperative indexing improvement](cooperative-box-face-index.md).
