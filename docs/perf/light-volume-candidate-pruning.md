# Prune losing light-volume neighbors before occlusion

The propagation stencil reads a neighbor's residual strength before querying
the voxel and SDF-blocker bitfields. A candidate that cannot exceed the current
winner skips both occlusion lookups. Potential winners still pass both checks;
neighbor order, equal-strength ties, colors and winning SPOT IDs are unchanged.
RGBA8 UNORM reads make the current winner finite and nonnegative, so the
stronger rejection also covers the previous nonpositive-residual rejection.

This trades texture reads in blocked regions for fewer bitfield lookups for
losing neighbors. It adds no cache, changes no volume bounds or representation,
and does not assume that empty light cells imply empty geometry. The GLSL and
Metal implementations use the same order.

## Validation

All 57 fresh full-frame RGB pairs match byte for byte on native Metal Debug:

| Demo / controls | Candidate capture IDs | Parent capture IDs |
|---|---|---|
| IRLightingSpot, four cone/yaw/zoom views | 1–4 | 58–61 |
| IRLightingPoint, six zoom/pan views | 5–10 | 62–67 |
| IRLightingSdfBlocker, six zoom/pan views | 11–16 | 68–73 |
| IRLightingEmissive, light-domain matrix | 17–52 | 74–109 |
| IRLightingOccludedBoundary, boundary sweep | 53–57 | 110–114 |

Each run used `--auto-screenshot 6`; the final two added
`--light-domain-matrix` and `--light-boundary-sweep`, respectively. Candidate
captures preceded the parent controls, made by restoring only the two shader
files from parent `28d4416e7` and rebuilding shared lighting assets. Candidate
sources and assets were restored afterwards. The pixel comparison and native
performance reports are retained in [light-volume-candidate-pruning/](light-volume-candidate-pruning/).
Representative paired images are in
`docs/pr-screenshots/codex/light-volume-candidate-pruning/`.

The focused reviewer found no correctness or backend-parity blocker. Native
demo builds pass. OpenGL runtime remains untested.

## Performance

Apple M4 Max, Metal Debug, frozen IRPerfGrid 64³, yaw 45°, zoom 4,
three 600-frame runs per version, GPU profiling enabled and CPU sampling off.
The parent is [overflow face deduplication](overflow-face-dedup.md).

| Measurement | Parent mean (range), ms | Candidate mean (range), ms |
|---|---:|---:|
| Frame | 18.923 (18.910–18.930) | 17.467 (17.440–17.490) |
| GPU computeLightVolume | 10.904 (10.899–10.914) | 9.426 (9.411–9.438) |

This grouped local comparison improves frame time by about 7.7% and the sampled
light-volume invocation by 13.6%. GPU rows are not additive frame totals.
The dense scene is not evidence of the same gain in every lighting workload;
blocked-neighbor density can change the texture-versus-bitfield tradeoff.
