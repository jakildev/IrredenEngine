# Bounded local-light propagation controls

The dispatch skips cells outside the proven reachable seed domain without
changing radius, iteration count, occlusion or RGBA8 quantization. The
[domain proof](../../design/light-volume-propagation-domain.md) defines the
contract. This does not change sun-shadow geometry or smooth any image edges.

## Paired measurements

Native macOS Debug, Apple M4 Max, AC power, 262,144 voxel entities. Each batch
ran under `ir-acquire benchmark`, with two 120-frame runs per case. All runs
completed cleanly and all 120 GPU-frame samples per run were valid. The report's
`quiet=unknown` reflects the outer benchmark lock rather than an inner quiet
probe. Offscreen rendering retains display-rate pacing (120 Hz on this host).
These are neither Release nor million-entity acceptance measurements.

Order: bounded first, full-domain control, bounded return. The full-domain
control restores the original host system and removes the invocation offset
from both propagation shaders, retaining the same 48-byte parameter ABI in
both arms. The unrelated master merge between the first two batches only
changes fog documentation/comments. Per-run commands, hashes, reports and logs
are retained in each case directory (log trailing whitespace normalized); see also [provenance](provenance.json).
The [control patch](full-domain-control.patch) converts this candidate to the
measured full-domain control. Apply it with `git apply --unidiff-zero` only in a dedicated experiment checkout,
rebuild the demos, then reverse it and rebuild before testing the candidate.

| Case | Bounded first GPU frame ms | Full-domain GPU frame ms | Bounded return GPU frame ms |
|---|---:|---:|---:|
| Zoom 1, subdivision 1, yaw 0 | 4.625 | 8.286 | 6.261 |
| Zoom 4, base subdivision 4, yaw 45° | 12.509 | 12.575 | 10.550 |
| Zoom 4, base subdivision 4, yaw 0 | 79.234 | 74.353 | 73.242 |

These are mean completed command-buffer envelopes, not sums of stage rows or
GPU busy time. The final low-density pair improves by 2.025 ms (24.4%), and its
light-volume row falls from 5.958 to 3.592 ms including the additional clear.
The first bounded low run was faster still, showing substantial run-order/host
variation. Rotated and dense results do not establish a stable percentage gain
across both bounded batches. Dense rendering remains far over the 16.67 ms
target; no million-entity/60 FPS claim follows from this experiment.

The dense bounded `computeLightVolume` row reports 70–75 ms while the
full-domain control reports 5.7 ms, despite nearly unchanged whole-frame cost
and three separate voxel rows of roughly 23–24 ms each. Treat that stage
attribution as unresolved, not a 12× propagation regression or saving.
Investigate encoder timestamp boundaries and upstream dependencies with an
isolated timing control before using that row to rank the next optimization.

Reproduce a batch from the repository root after building its control:

```bash
ir-acquire benchmark -- python3 docs/perf/bounded-light-volume/controls.py run-name
```

The script preserves the exact common CLI. Cardinal effective subdivision and
per-axis capped subdivision differ at zoom 4; this is not a density-parity
comparison between yaw modes. No overflow records were dropped in the rotated
runs (745,398 retained entries per sampled frame).

## Correctness evidence

- 36 native tests pass, including full-volume color/ID equality after every
  propagation iteration. [Final XML](validation/final.xml) retains the result.
- Removing the Metal invocation offset fails the native GPU equality test;
  removing the iteration expansion fails six of nine focused tests, including
  the GPU test. Both mutations were restored. Their XML results are retained.
- The GPU test also deliberately omits the destination clear, observes unequal
  poisoned exteriors, then clears and verifies equality.
- `render-verify.py --target IRCanvasStress --no-build` passes all 30 checks:
  20 exact RGB matches and ten structural shadow/face checks. The run used the
  separate harness follow-up's strict `RESULT=CLEAN` completion gate.
- All **51 paired PNGs are byte-identical**: 36 emissive domain views, five
  emissive boundary views, five hover views and five occluded-boundary views.
  Both sides report `RESULT=CLEAN`. The occluded fixture exercises relocated
  and discounted boundary seeds. [Hashes and representative screenshots](../../pr-screenshots/codex/bounded-light-volume/README.md)
  and the capture logs are retained; `captures.py` reproduces these four passes.
- The existing lighting baseline run still fails 40 of 46 image comparisons.
  Paired controls establish those differences also occur with full-domain
  propagation. References and thresholds were not changed. Equality proves no
  new image difference in these views, not that every inherited artifact is
  visually correct.

Hardware OpenGL, native Release, widely separated many-light cost, and sustained
moving-camera/million-entity qualification remain pending. A full-volume union
still pays the added destination clear; this is a localized-light optimization.
