# Finite-face index coverage: validation and cost

The [producer bounds](../../design/sun-face-index-coverage.md) fix two executable
omissions: accepted closed-edge receivers in an unpublished neighboring tile,
and independently contracted determinants that let the producer drop a face
accepted by the receiver. Receiver coverage and depth expressions are unchanged.
The full-cascade fallback preserves candidate/spill/incomplete-list handling.

## Correctness evidence

- Both extracted shader backends: 3,662 faces, 288,083 accepted in-map queries,
  zero missing memberships. Replacing the new face envelope with the raw AABB
  produces 1,882 failures even with the conservative tile mapper retained.
- Additional controls cover mixed FMA/uncontracted determinants, 16 unsafe
  geometry/cascade fixtures, six accepted extreme receiver positions, full-map
  record exhaustion, primary spill and query overflow. Mutations fail.
- [Native Metal results](native-metal.json): Apple M4 Max, default compiler
  options matching the runtime, 600,244 cascade queries, 288,083 accepted;
  zero containment failures. The raw-AABB mutant has 1,882 failures. This proves
  native predicate/bound containment for the fixtures, not atomic publication
  or every possible compiler/hardware input. The host index tests execute
  publication separately, including 64-thread cooperative controls.
- [35 native image pairs](../../pr-screenshots/codex/sun-face-index-coverage/README.md)
  are RGB-identical. This is regression evidence; the mathematical boundary
  witnesses are not established causes of defects in those images.
- Native OpenGL remains unverified on this host.

Reproduce the native numerical check:

```bash
ir-acquire gpu -- python3 scripts/render-source-face-coverage-metal.py --output /tmp/source-face-metal
```

The [native source archive](native-programs.tar.gz) retains the exact generated
shader programs, fixture exporter and Objective-C++ launcher. Fixture bytes and
launcher binary hashes are in the result; generated binaries are not committed.

## Native timing limits

macOS 26.5.2, Apple M4 Max, Debug Metal, 2560x1440 output. IRShapeDebug baked
four-subject/blocker fixture, yaw 45 degrees, zoom/effective subdivision 4,
AO disabled, shadows enabled, `configs/perf/sdf-off.lua` (stage timers off).
Surface lighting and visibility prepass enabled; diagnostic counters disabled.
Each arm has three guarded, clean 600-frame runs with 600 valid GPU timestamps
and zero dropped overflow entries. Each run reports 403 peak overflow entries.
All arms use the same executable SHA; only shader assets were staged between
arms, never during a run. Source head was `9917cb533ec8ee517390ed716edfa4a200e7d242`;
working changes and staged shader/config fingerprints are in each manifest.

| Arm | Power | GPU envelope mean (range), ms | Steady wall mean (run range), ms |
|---|---|---:|---:|
| Original index | Battery | 5.405 (5.374–5.429) | 8.330 (8.330–8.330) |
| Corrected index | AC | 5.954 (5.811–6.226) | 13.583 (11.050–15.640) |
| Restored original index | AC | 5.268 (4.576–6.087) | 14.750 (10.380–19.690) |

The first comparison is power-confounded. The AC arms overlap in run spread,
but the corrected index has a higher mean GPU envelope. **These measurements
do not establish negligible overhead or a speedup.** GPU envelopes include
submission gaps and stalls; wall-frame timing varies substantially. The added
producer arithmetic and pathological full-cascade cost still require denser,
stage-specific profiling before a performance claim. This is a correctness fix,
not a qualified scalability optimization. The fixed shader sources were restored
and built after the restored-original control.

[Archives and hashes](manifest.json) retain every report, console log and
configuration manifest, including the battery arm. A separate sandboxed baseline
attempt failed to connect to macOS display services and is excluded from timing.

## Diagnostic work counts

Separate diagnostic captures sample fixed 45-degree poses at zoom 1. Counters
are not enabled in the timed arms. Reservation order can change visit counts.

| Scene | Original visits | Corrected visits | Original/corrected incomplete queries |
|---|---:|---:|---:|
| Baked shapes | 483429 | 483615 | 0 / 0 |
| Direct shapes | 533469 | 533585 | 0 / 0 |
| Mixed canvas | 501187 | 502393 | 0 / 0 |

The largest observed visit increase is 0.24%. These samples do not measure
index-build work, total spill allocation, or pathological grazing populations.
Next measure those costs before reducing tile size or adding finer candidate
masks; preserve the executable inclusion contract throughout.
