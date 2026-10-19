# Complete finite-shadow query probe

This is a specialized native Metal sampler probe, not an engine frame benchmark.
It calls the unchanged production `sampleCascadeShadow` and its geometry helpers
with counters disabled for timing and enabled in a separate diagnostic dispatch.
The [tool procedure](../../../scripts/perf/README.md#native-finite-shadow-query-probe)
defines the reservation and evidence requirements.

## Native correctness

Apple M4 Max, macOS 26.5.2. Both shader variants pass all ten cells with 65,536
receivers each: 64 inline candidates or 256 candidates (64 inline plus 192 linked
spill nodes), crossed with envelope miss, footprint miss, behind receiver, first
blocker and last blocker. Every resulting receiver shadow and all ten diagnostic
counters are checked. The explicit validation-only run records no timing samples.
[Native result](native-validation.json) and expanded sources are retained.

These fixtures check complete local lists, not concurrent index construction,
incomplete-list fallback, arbitrary scene geometry, receiver rasterization or
OpenGL execution. Portable tests reuse the existing scalar shader adapter;
corrupted spill records, counter slots and expected counters fail as intended.
Mocked routing controls check reservation/mode separation, not native execution.

## Initial timing run: reject as a stable baseline

The initial two-round warmup left a strong ordered drift. Every case ends
faster than it starts across seven measured rounds: the 256-candidate envelope-miss path falls
from 0.355 to 0.189 ms, and the footprint-miss path from 0.570 to 0.309 ms.
No hardware frequency counters were captured, so frequency ramping is a possible
explanation, not a diagnosed cause. The wrapper exited zero without an unguarded
or breach warning, but this attempt did not request a separate quiet report.
Post-run power metadata reports battery power; power was not sampled at startup.
Do not use these medians as a qualified baseline or as optimization evidence.

| Case | 64 candidates median (range), ms | 256 candidates median (range), ms |
|---|---:|---:|
| envelope_miss | 0.0656 (0.0504–0.0944) | 0.2599 (0.1894–0.3549) |
| footprint_miss | 0.1121 (0.0795–0.1492) | 0.4309 (0.3094–0.5695) |
| behind | 0.1043 (0.0730–0.1369) | 0.3927 (0.2728–0.5205) |
| first_blocker | 0.0065 (0.0048–0.0095) | 0.0062 (0.0048–0.0095) |
| last_blocker | 0.0709 (0.0460–0.0936) | 0.2662 (0.1873–0.3520) |

[All initial samples](initial-short-warmup.json) remain in acquisition order.
Each sample is the full eight-dispatch command-buffer GPU span divided by eight,
including dispatch ordering. Exhausted paths also execute empty-map fallback;
blockers return early. All receivers share one tile, fixed basis/normal/scale/
throw, and coherent points. Compiler specialization and hot record data make
these timings unsuitable for extrapolating to frame rate or entity count.

The probe now warms all fixtures for at least three wall-clock seconds and two
full rounds. Warmup duration includes CPU initialization, validation and waits;
it is not GPU-active time or a timing-stability criterion. Inspect individual
rounds and preserve every attempt before drawing performance conclusions.

Source archives omit executables. Their hashes are listed in
[archives.json](archives.json); each results file includes hashes of its expanded
shader/launcher sources and executable. The first run needed a missing standard
header fixed in the launcher. A subsequent sandboxed repeat could not obtain a
Metal device and produced no measurements.

## Warmed repeat: guarded diagnostic evidence

The repeat completed 186 warmup rounds over 3.017 wall-clock seconds, followed
by all seven measured rounds. The wrapper exited zero and its retained
[quiet report](warmed-repeat.quiet) is `GUARDED`. Both power endpoints report AC.
All receiver results and diagnostic counters pass on the final tool sources.

| Case | 64 candidates median (range), ms | 256 candidates median (range), ms |
|---|---:|---:|
| envelope_miss | 0.0795 (0.0696–0.0796) | 0.3028 (0.2777–0.3031) |
| footprint_miss | 0.1258 (0.1102–0.1260) | 0.4736 (0.4339–0.5406) |
| behind | 0.1151 (0.1011–0.1154) | 0.4461 (0.4105–0.4462) |
| first_blocker | 0.0063 (0.0060–0.0067) | 0.0068 (0.0060–0.0069) |
| last_blocker | 0.0774 (0.0679–0.0775) | 0.3003 (0.2634–0.3355) |

[All repeat samples](warmed-repeat.json) are retained; no rounds are discarded.
The initial strong downward trend is absent, but first-round transients and
outliers remain. These are diagnostic magnitudes from one guarded run, not a
statistically qualified baseline. The first and repeat runs also differ in power
state and warmup, so they are not a before/after optimization comparison.

The complete scans take roughly 3.8 times as long for four times as many candidates,
while first-blocker time stays near 0.006–0.007 ms. This supports testing an early,
conservative per-tile candidate mask before full geometry loads; it does not
prove that such a mask will improve the engine. Any prototype must preserve the
shared finite-envelope containment contract, inline/spill and fallback semantics,
and be measured for index-build cost, storage and actual frame cost. Keep
candidate count reductions separate from arithmetic micro-optimizations, whose
previous deferred-depth experiment did not demonstrate a GPU benefit.

Remaining qualification: diverse tiles/caches, crowded real scenes, moving
camera/light, subdivision scaling and Windows/OpenGL. No production rendering
behavior, shadow geometry, filtering, buffer layout or default mode changed.
