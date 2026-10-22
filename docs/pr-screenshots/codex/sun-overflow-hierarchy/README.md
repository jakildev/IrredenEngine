# Finite-pool shadow overflow: native production controls

The production unsorted hierarchy preserves a lower blocker's floor shadow when
crowded tile lists lose candidates. It queries the complete retained geometry
pool with the existing finite-face/analytic predicates; it does not blur or
expand coverage. This closes the retained finite-pool reproduction on Metal,
not every shadow defect or the original Windows report.

| Before: 128 distant casters erase the center | After: the lower shadow survives |
|---|---|
| ![Baseline receiver](baseline/receiver-detail.png) | ![Production receiver](far-128/receiver-detail.png) |

These are unchanged 360×240 crops of native 2560×1440 frames, rectangle
`(1100,600)-(1460,840)`. Full frames remain beside each crop; the
[baseline full frame](baseline/screenshot_000001.png) and
[final full frame](far-128/screenshot_000001.png) include the distant caster.

## What was checked

All **11** captures exited CLEAN. The nine shared cases match the earlier
production capture pixel-for-pixel after replacing mutable per-dispatch uniforms
with immutable level parameters. Both probe-on/off pairs also match exactly.
[comparisons.json](comparisons.json) records RGB comparisons; the original
[capture manifest](captures.json) retains commands, binary/frame hashes and
record/tile counters.

| Final case | Records | Incomplete tile lists, near / far | Changed pixels versus old full frame |
|---|---:|---:|---:|
| lower | 1,708 | 0 / 0 | 0 |
| far-8 | 1,924 | 0 / 0 | 0 |
| far-16 | 2,140 | 2 / 2 | 200 |
| far-128 | 5,164 | 4 / 4 | 280 |
| near-128 | 5,164 | 4 / 4 | 88 |
| oblique-lower | 2,220 | 0 / 0 | 0 |
| oblique-far-128 | 5,676 | 3 / 4 | 0 |
| oblique-aligned-16 | 2,652 | 2 / 2 | No old capture |
| oblique-aligned-128 | 5,676 | 4 / 4 | No old capture |

The receiver crops for **far-8, far-16 and far-128 exactly equal lower**;
**oblique-aligned-16 and oblique-aligned-128 exactly equal oblique-lower**.
The aligned offset `(18,24,-60)` follows the oblique sun input `(0.3,0.4,-1)`;
the older `(0,0,-60)` oblique control does not place its distant caster on the
same sun ray. No equality is claimed between different suns or across full
frames that contain differently positioned visible casters. Near-128 is retained
as a nearby-caster control, not asserted equivalent to the lower-only scene.
The CSV's “incomplete tiles” still describes the bounded tile lists; a ready
hierarchy supplies the missing exact query without changing those counters.

Each case retains its full frame, crop, config and run log; nine also retain
compressed GPU record/tile CSVs. The two no-probe cases intentionally omit CSVs.
No golden reference was refreshed. The coordinating reviewer inspected the old
and new far-128 detail/full frames and the aligned oblique detail/full frame.

## Provenance and replay

- Apple M4 Max, macOS 26.5.2 arm64, Metal Debug. Logical window/game size
  1280×720, native framebuffer 2560×1440, offscreen.
- Source head `5fe9e07918efb6ed32d365f63bfdcb090b732f84` plus the capture-time
  [tracked engine patch](source-engine.patch) and six archived new shaders under
  `untracked-source/`. [source-manifest.json](source-manifest.json) hashes changed
  engine files after capture; subsequent tracked C++ differences are formatting
  and a positive-leaf-size static assertion. The archive includes untracked
  shaders because a normal `git diff` omits them.
- The binary SHA-256 is in `captures.json`. [manifest.json](manifest.json) hashes
  the retained evidence. `first-production-captures.json` records the nine-case
  predecessor used for the immutable-uniform pixel comparison.
- The copied [baseline manifest](baseline/captures.json) and
  [baseline patch](baseline/source.patch) identify the old runtime
  `e5fce0efa` plus its patch. The [original baseline report](../sun-geometry-capture/README.md)
  owns its capture and independent finite-quad geometry proof; its descriptions
  of the then-unfixed hole are historical.
- IRCanvasStress uses a rigid 18×6×8 source-face box, a 40×40 GRID floor,
  and 0/8/16/128 coincident rotated 3³ rigid casters. One sun and ambient sky,
  shadows enabled, AO disabled, no authored local lights. Yaw 0, zoom 1,
  subdivision 1, origin pivot, no spin/auto-rotation. Commands retain the exact
  sun direction and caster offsets. ECS total was not instrumented.

The [standalone replay driver](capture.py) expands the original wrapper's eleven
cases without depending on a `/tmp` script. Run from a checkout containing the
production change and a matching build:

```bash
ir-acquire gpu -- python3 docs/pr-screenshots/codex/sun-overflow-hierarchy/capture.py --output /tmp/sun-overflow-replay
```

Capture/readback synchronizes the GPU; these runs are not timing evidence. The
[separate guarded timings](../../../perf/sun-hierarchy-throughput/README.md)
measure a standalone synthetic receiver plane, not this production integration.

## Validation limits and remaining work

The CPU production-shader adapters exercise hierarchy construction/traversal,
independent triangle-ray comparisons, readiness/clear, finite depth windows,
record-pool overflow, and preservation of the independent raster layer. Analytic
records use a sentinel predicate in those controls; they do not establish native
analytic intersection correctness. The coordinated 111-test CPU run had 110
passes and one stale allocation-size expectation; after correction to 30,670,864
bytes, the affected seven-test suite passed. This is not a claim of a fresh
111-test all-green rerun.

**The full CanvasStress regression run passed 41 of 46 checks; five failed.**
The retained [render-verify log](render-verify.log) reports failures for
`modeswitch_grid`, `modeswitch_grid_return`, `detach_grid`, `impulse_midspin`
and `impulse_settled`. No failing reference was refreshed.

A [controlled native comparison](transitions/results.json) then ran the three
affected scenes twice: production and a variant with only hierarchy querying
disabled. All six runs exited CLEAN and all **nine paired full frames match
exactly**. Against the existing golden images, both variants retain the same
740, 740, 536, 2,024 and 1,696 changed RGB pixels, respectively. These are raw
changed-pixel counts, not the regression harness's threshold/maximum-delta metric.
The archive retains all 18 full frames, details, configs and logs, the
[exact driver text](transitions/driver.py.txt),
[original shader](transitions/original-sample.metal),
[disabled-query shader](transitions/query-disabled-sample.metal), and
[reference hashes](transitions/reference-manifest.json).

That switch-only control left construction and layout changes active. A subsequent
[actual parent-artifact comparison](actual-parent/results.json) now captures the
same three scenes from clean parent `ffbcf2161820906760fce7299c035a141fe523b4`:
three CLEAN runs, nine full frames, all pixel-identical to production, with the
same five golden differences. Its binary/shader artifact hashes, commands,
configs and logs are retained beside the frames. This establishes that these
specific differences also occur without the whole hierarchy patch; it does not
explain their underlying cause or excuse refreshing the goldens. The full suite
remains **41/46**, with those five existing mismatches still needing attribution
or repair.

The [initial engine profile matrix](../../../perf/sun-overflow-production/README.md)
exposed a major crowded-cardinal cost increase. The subsequent
[guarded early-depth implementation](../../../perf/sun-overflow-production/guarded-depth/README.md)
preserves these eleven images and eight additional touching/intersection pairs.
Its retained GUARDED parent/optimized/parent timing comparison is provisional on
battery power: crowded cardinal means are 70.60 ms versus 66.61/67.23 ms on the
parent. The original large overdraw amplification is absent, but remaining cost
and native Windows parity still require qualification.

Pending: native Windows/OpenGL reproduction and parity, actual analytic/mixed
render-mode controls, motion and stale-publication transitions, exhausted global
record pools, raster-only depth loss, and guarded production build/query plus
full-frame cost. A complete hierarchy cannot recover geometry absent from the
pool or raster layers. The retained finite-geometry proof and native Metal
fixture do not establish closure of the original moving-block Windows report,
general shadow correctness, FPS gains, or million-entity throughput.
