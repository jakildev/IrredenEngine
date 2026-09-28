# Finite shadow query pruning: deferred experiment

The conservative pre-division rejection did not demonstrate a useful GPU gain.
Production shader source and staged assets are restored to the parent. The
prototype and deterministic controls are retained in
[the experiment directory](finite-shadow-query-pruning/) for reproduction;
this PR does not add the rejection branch to the renderer.

## Geometry contract

For projected edges U and V, the existing face query solves coordinates with
numerators `Nu = dx*Vy-dy*Vx`, `Nv = Ux*dy-Uy*dx` and determinant D.
The prototype extracts that numerator calculation into a shared helper without
changing expression order. It sign-folds copies of the numerators and rejects
only values strictly outside `[-abs(D), 2*abs(D)]`. Surviving queries retain the
original division, closed `[0,1]` footprint test and depth expression.

The loose interval is intentional. A tight pre-division interval can disagree
with rounded division near a closed edge, including a tiny negative coordinate
that rounds to negative zero. The unit-wide slack keeps those cases in the
original exact test; it never expands accepted shadow geometry or adds blur.
Overflow of the upper extent disables that rejection rather than tightening it.
Degenerate bases retain the existing threshold. No records, tile membership,
capacity, allocations or buffer layout are changed.

## Deterministic controls

The retained test executes both backend expressions against fixed pre-change
scalar algebra and independent exact rectangles. Each backend returns identical
bits for 40,380 queries, including 19,686 finite accepted hits. It exercises
signed winding, rotated/scaled faces, large translations, adjacent representable
edge values, signed zero, the degeneracy threshold, finite determinant with
`2*abs(D)` overflow, and two nonfinite intermediate fallthrough cases.

Test-only counters observe 9,650 broad rejections and 24,764 entries into the
divided-coordinate test; these are fixture counts, not native workload counters.
Removing rejection or determinant sign folding fails a work-count witness.
Tightening to `[0,abs(D)]` fails the tiny-negative-coordinate control: the parent
accepts the rounded negative zero, while the tight predicate incorrectly rejects
it. Host arithmetic disables contraction and fast-math; native GPU checks remain
necessary. The archived patch was reapplied and its complete Python test passed
before source restoration, so the reproduction includes the test and both twins.

## Native result

Apple M4 Max, macOS 26.5.2, Metal Debug, battery power, 2560×1440.
`IR_PERAXIS_SURFACE_LIGHTING=1`, mixed scene, fixed yaw 73.125°, zoom 1,
subdivision 1, 363 frames/run. Three runs per retained arm, in order: parent,
candidate retry, restored parent. All retained runs have 1,533 peak overflow
entries, zero drops, fixed camera/zoom and valid GPU timestamps. The executable
fingerprint is identical; restored shaders match the initial parent fingerprint.

| Measurement | Parent mean (range), ms | Candidate mean (range), ms | Restored mean (range), ms |
|---|---:|---:|---:|
| GPU frame envelope | 10.134 (9.838–10.708) | 10.037 (9.974–10.104) | 10.248 (9.749–10.543) |
| Per-axis scatter | 5.205 (5.131–5.313) | 5.384 (5.369–5.400) | 5.296 (5.131–5.415) |
| Steady frame average | 13.730 (13.390–14.150) | 13.693 (13.640–13.780) | 13.910 (13.470–14.160) |

The target scatter scope does not improve; whole-frame ranges overlap.
Host load varied materially, so this establishes no demonstrated benefit,
not a reliable regression magnitude. Counters proving skipped arithmetic do
not establish GPU instruction savings. Metal scopes may overlap and must not
be summed or treated as additive busy time. This is not a population benchmark.

An initial candidate batch contains a 72,564.922 ms frame stall and severe
cross-stage timing variation. It is retained under `candidate-stalled/` and
excluded from the table. Its clean process exit is not timing acceptance.

All three candidate retry captures and all three restored captures are
RGB-identical to the corresponding parent captures. The four frame-oracle views
also remain RGB-identical to the parent evidence. The three stalled-batch images
match too, but provide no valid timing result. `comparison.json` records all
13 pairs. [Screenshots](../pr-screenshots/codex/finite-shadow-query-pruning/README.md)
show unchanged appearance, not a new visual fix. Native OpenGL remains untested.

## Reproduce the rejected prototype

The patch targets source at `f6f912b737f32e4263ad89e3f433499fcf686f68`.
From a dedicated worktree of this evidence branch:

```bash
git apply docs/perf/finite-shadow-query-pruning/prototype.patch
python3 scripts/tests/test_render_source_face_pruning.py
python3 scripts/tests/test_render_source_face_query.py
fleet-build -j3 --target IRCanvasStress
```

Set `IR_PERAXIS_SURFACE_LIGHTING=1` and run the exact command in each manifest.
The patch contains both shader twins and the test; it is not active by default.
The final source/runtime restoration was verified with matching hashes and
fresh native captures. The shader layout control and header checks passed with
the candidate. CPU scalar tests complement, rather than certify, GPU fast-math.

## Next direction

Measure repeated shadow work across covered fragments before attempting more
small arithmetic changes. The scatter fragment performs lighting before writing
its final depth, which includes margin-yield and tie arbitration. Test a visibility
pass using exactly those rules before expensive lighting, or a winner-only
screen-space resolve. This is a hypothesis, not an established early-depth GPU
bottleneck. Preserve SDF co-sorting, equal-depth ownership, conservative margins,
backend depth formats and material data before adopting a new pass. Moving the
existing depth assignment earlier or forcing early fragment tests is insufficient:
vertex depth is not the final fragment-computed depth, and Metal uses `depth(any)`.
A replay must explicitly compare against a separately stored winning depth/code
before lighting; reject only strictly farther candidates and preserve equal-depth
draw ordering. Start with per-axis/overflow visibility, recognizing that it cannot
eliminate shading hidden by later SDF draws. Avoid attachment feedback and account
for D24/D32 quantization; a common integer winner code may be preferable. Keep the
continuous attached lighting path default-off until the wider-view cost is solved.
