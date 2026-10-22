# Early-depth hypothesis: fixture-only, timing rejected

The experimental source-face shader removes fragment depth output and computes
depth in the vertex stage for faces wholly within one affine depth-clamp interval.
It tests whether hidden coincident fragments are doing expensive shadow queries.
**This shader is not production-safe across depth clamp/foreground-band boundaries
and is not the production implementation.** The next implementation investigates
a conservative whole-canvas GPU routing decision with the original fallback.

All eleven native fixture captures exit CLEAN and are pixel-identical to the
production overflow fix. [Frame links](captures/frame-links.json) reference the
already-retained full-frame PNGs by hash: their bytes also match, so no eleven
image copies are added. The [capture manifest](captures/captures.json),
[comparisons](captures/pixel-comparisons.json), per-case configs, compressed logs
and geometry CSVs are retained. This establishes parity only for these fixtures,
not arbitrary canvas depths, priority tiers, clipping, transparency or rotation.

## Timing rejection and diagnostic observations

The outer benchmark exits **1**: AC power before the sequence becomes Battery
Power afterward, failing the driver's final assertion. The retained
[quiet report](quiet.txt) says **GUARDED**, but a quiet host does not repair the
power mismatch. The parent manifest records AC at launch; both head and
experimental manifests already record Battery at launch. These timings are
**rejected for performance qualification**; do not present them as an accepted
speedup or evidence that the production cost is fixed.

| Variant | Wall mean ms | Steady wall mean ms | GPU frame envelope ms | GPU entityCanvasToFb ms |
|---|---:|---:|---:|---:|
| Parent | 64.250 | 63.750 | 39.557 | 3.645 |
| Hierarchy head | 192.210 | 192.090 | 164.017 | 126.598 |
| Fixture-only early depth | 67.450 | 67.010 | 41.150 | 5.240 |

These values motivate testing hidden-fragment work; they do not establish its
causal contribution under matched power. One 360-frame run per variant, ordered
parent/head/experiment; steady wall values exclude the first 90 frames. GPU
columns retain aggregate samples, with the same envelope/partial-stage caveats
as the [initial matrix](../README.md). The fixture has 128 coincident distant
casters, camera yaw 0, zoom/subdivision 1, vertical sun, 577 entities and many
private canvases. No captures or geometry readbacks occur inside timing runs.
No repeat spread, confidence interval, FPS or general throughput claim follows.

## Provenance

The [exact driver](driver.py.txt) asserts power before writing `matrix.json`, so
that file was never produced. [inventory.json](inventory.json) is explicitly a
**post-run reconstruction**, including the coordinator-reported failure text,
commands, source artifacts and qualification failure; it is not fabricated runner
output. Raw outer traceback text was not retained. Per-run manifests still
identify the integration checkout `d832ed2ec5de4813e77a309bed0e1ab43719a346`, not
the frozen binary source. Use the archived [parent](../artifacts/parent.json)
and [head](../artifacts/head.json) artifact manifests for those identities.

The experimental binary is the head binary. Its
[overlay manifest](experiment/manifest.json), [patch](experiment/source.patch),
[original shader](experiment/source_face_scatter.original.metal), and
[experimental shader](experiment/source_face_scatter.experimental.metal) identify
the changed runtime shader bytes. The overlay author's original note that
validation was pending is preserved verbatim; the capture/timing outcomes above
were produced later by the coordinating session. Reports/logs are gzip-compressed
losslessly and all evidence is covered by the parent directory's manifest.

Keep the production change **not merge-ready** until a general implementation
passes relevant boundary/visual controls and a new matched-power benchmark with
retained quiet evidence resolves the crowded cost.
