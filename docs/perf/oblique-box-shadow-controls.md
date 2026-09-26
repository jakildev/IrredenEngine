# Distinct oblique box shadow controls

The native oblique-caster matrix does **not demonstrate a frame-time gain**
from the [deferred tile-rejection prototype](projected-face-tile-overlap.md).
Production shaders remain unchanged. The useful deliverable is a repeatable
fixture with independently translated and rotated analytical boxes.

## Controls and scope

`IRCanvasStress --probe-analytic-box` accepts `--analytic-box-step x y z`
and `--analytic-box-yaw-step radians`. Instance `i` adds `i * step` to its
world position and `i * yawStep` to its starting yaw. Both default to zero;
the existing coincident-caster controls and normal demo scene are unchanged.
The optional orange depth-tie probe retains the first box's pose.

`box_shadow_controls.py --suite oblique` runs one unrotated box, one box at
45 degrees, and 64/128 boxes starting at 45 degrees. Successive boxes move
0.03125 world units along X and rotate 0.001 radians. The overhead sun is
`(0, 0, -1)`; boxes retain their default 18×6×8 extent. The existing `span`
suite remains the default.

These are distinct casters, but they still overlap heavily: the 64-box
centers span 1.96875 units and 3.61 degrees; the 128-box centers span 3.96875
units and 7.28 degrees. This is **mixed genuine and false-positive candidate
pressure**, not an isolated false-positive workload. No native tile-count
readback was added, so saturation and sampled fallback cannot be inferred
from these timings alone.

## Measurements

macOS 26.5.2, Apple Silicon, Metal Debug, battery power. Each arm uses two
forward/reverse rounds, 275 frames and two captures per run (16 native runs
total). Baseline and candidate use the same binary and runtime scripts;
only the four prototype shader files change. Raw manifests include hashes,
commands, battery state and run metadata.

| Case | Baseline frame ms | Candidate frame ms | Baseline box stage ms | Candidate box stage ms |
|---|---:|---:|---:|---:|
| count1-yaw0 | 9.545 | 9.460 | 0.223 | 0.304 |
| count1-yaw45 | 9.415 | 9.670 | 0.251 | 0.249 |
| count64-yaw45 | 9.465 | 9.505 | 0.347 | 0.281 |
| count128-yaw45 | 9.435 | 9.460 | 0.504 | 0.520 |

These are small capture-assisted Debug samples, not a throughput ceiling or
an equivalence test. The candidate's second count1-yaw45 run reached 9.93 ms;
it is retained. GPU rows are sampled invocation means, not additive frame
budgets. The inconsistent box-stage changes do not justify enabling SAT.
Native Windows/OpenGL is unmeasured.

[Raw reports](oblique-box-shadow-controls/) retain all valid runs. An earlier
candidate attempt changed staged shaders during execution; the existing
manifest-identity guard rejected it. That attempt is excluded from this
report, its aggregates and its screenshots.

All 16 paired zoom-1 captures are RGB-identical. Four additional zoom-8
cardinal camera pairs are also identical, but the floor is absent and the
object is clipped, making those unsuitable shadow-quality evidence. After
restoring production shaders, four zoom-3 captures show the floor and retain
visible serration around the dense group's shadow. The [screenshots](../pr-screenshots/codex/oblique-box-shadow-controls/README.md)
are evidence of the fixture and remaining limitations, not a visual fix.
The final binary also hoists an initialization-time yaw argument read;
performance matrices precede that equivalent cold-path cleanup.

## Reproduce

From a configured dedicated worktree, finish each build before starting a
matrix; never change runtime staging while a matrix is running.

```bash
fleet-build --target IRCanvasStress
python3 scripts/perf/box_shadow_controls.py --suite oblique --rounds 2 --output /tmp/oblique-baseline
```

To repeat the candidate, apply the retained patch from the linked experiment,
build/stage it, then run the same matrix into a fresh directory. Restore the
four source shaders and rebuild/stage before resuming production work.

The closer production view uses:

```bash
fleet-run IRCanvasStress --only shadowbox,floor --probe-analytic-box --analytic-box-count 128 --analytic-box-yaw 0.785398163 --analytic-box-step 0.03125 0 0 --analytic-box-yaw-step 0.001 --sun-direction 0 0 -1 --no-spin --no-auto-rotate --pivot-origin --zoom 3 --subdivisions 1 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4
```

## Pending work

1. [Native tile readback](sun-face-index-diagnostics.md) confirms incomplete
   lists in the dense fixture. Correlate individual boundary pixels with
   exact geometry using a same-geometry overflow control.
2. Isolate outer receiver tiles covered by caster AABBs but missed by their
   actual faces; compare exact shadows with the finite-face oracle before
   revisiting tile rejection or saturated-list atomics.
3. Diagnose the remaining dense-group shadow serration at zoom 3. Distinguish
   true overlap silhouettes, exact-list loss, projection parity and sampled
   fallback; preserve sharp geometry rather than blur the symptom.
4. Investigate why the zoom-8/no-AO close-up loses the floor before using it
   as evidence. Reproduce with each option varied independently.
5. Complete native Windows/OpenGL validation of the cooperative index stack.
