# Cardinal voxel surface receivers

Issue #4231, native macOS Metal. The paired capture command is
`fleet-run --timeout 1800 IRShapeDebug --spin-yaw --zoom 16 --auto-screenshot 720`.
Resolution is 2560×1440. Both runs completed cleanly. The manifests retain
all 720 filenames and hashes; this directory retains each cardinal and its
neighbors, plus the 45° control and its neighbors. Filenames encode degrees.

The before state includes refreshed master `fe29b5df8` and the existing shadow
stack, with the shape-sample diagnostic adapted to the gather API. The after
state adds exact cardinal voxel receiver reconstruction and world-space cascade
selection. The scene, camera, density, light, and caster algorithms are unchanged.

| Yaw | Before jump ratio | After jump ratio |
|---|---:|---:|
| 0° | 1.265468 | 1.199806 |
| 90° | 1.480613 | 1.219031 |
| 180° | 1.694949 | 1.146631 |
| 270° | 1.350902 | 1.194919 |
| 45° | 1.016379 | 1.016379 |
| 135° | 1.019572 | 1.019572 |
| 225° | 0.980327 | 0.980327 |
| 315° | 0.735996 | 0.735996 |

The metric is `scripts/render-yaw-lone-frame-metric.py`, with unchanged tolerance
and 1.3 threshold. All 716 non-cardinal PNGs are byte-identical. The historical
45° value of 0.93 in the issue is not the current baseline; the same-session
control is unchanged. A diagnostic pass retaining packed-depth cascade selection
produced identical images in this scene, but that depth has the wrong units:
the executed geometry tests independently exercise split-boundary disagreements.

`shadow-edge-180-before.png` and `shadow-edge-180-after.png` are unscaled crops
of `(820,1060)–(1170,1370)` in the corresponding full frames. The cube-face shadow
changes from a displaced, filtered sawtooth to a straight geometric edge. No
caster inflation, blur, or bias adjustment is used.

## Earlier rejection controls

Each subdirectory includes the exact command and capture membership in its
manifest. All four runs exited cleanly.

| Control | Observed |
|---|---|
| Authored source box, four cardinals | IoU .739 / .916 / .925 / .881, all pass |
| Analytic roof + detached staircase at 180/202.5/225° | 180° blocked/outside maximum RGB errors both zero |
| Unblocked staircase, four cardinals | Every full-frame RGB image identical to shadows disabled |

Commands for scoring the retained controls:

```sh
python3 scripts/render-shadow-box-metric.py --source docs/pr-screenshots/codex/4231-cardinal-surface-receiver/box/0.png docs/pr-screenshots/codex/4231-cardinal-surface-receiver/box/1.png docs/pr-screenshots/codex/4231-cardinal-surface-receiver/box/2.png docs/pr-screenshots/codex/4231-cardinal-surface-receiver/box/3.png
python3 scripts/render-sun-occlusion-metric.py docs/pr-screenshots/codex/4231-cardinal-surface-receiver/roof/0.png --staircase
```

## Contract coverage and limits

The compiler-backed test executes both shader helpers and production cardinal
emission: 723,744 face-center checks per backend, both triangle halves, all
cardinals, six signed faces, NONE/subdivision modes, density 1–16, signed and
fractional positions, panning, and storage phase. Routing tests execute both
shape specializations, winner publication, carrier composition, finite/legacy
frame selection, per-axis early return, resource binding, and cascade depth.
Nine geometry mutations and nineteen integration mutations are rejected.

The surface point is exact for the emitted cardinal microface; this does not
prove continuous shadow coverage inside every display triangle, remove bounded
shadow-index overflow, or fix unrelated SDF raster/lighting artifacts. Native
OpenGL execution remains a cross-host check. This is a correctness change, not
a measured performance improvement.

## Whole-suite follow-up

The [reference attribution](reference-attribution/README.md) retains every refreshed
before/after pair, distinguishes new receiver changes from inherited drift, and
records the fog review. The [staircase witness](staircase-witness/README.md) explains
why a newly sharp band is real voxel occlusion rather than a parity defect.

Final native validation: `python3 scripts/render-verify.py --all --no-build`
passes **172 checks across nine demos, zero failures and zero skips**. The
[complete log](native-suite.txt) records every command and check. All nine
targets were rebuilt against integrated master `4ca544a45` immediately before
the final run. Runtime code and reference images match `87afa20e1`; the only
change during the run was the test-adapter correction in `4b02c4f27`.

The preceding full build/run passed 170 checks and rejected the two CanvasStress
comparison references, which had selected the wrong capture indices during
attribution. Their corrected references were already committed before this fresh
aggregate run; both now match at 100%. No threshold or shot selection changed.
The editor-parts prefab passes its existing tolerance at 99.9799%; the green suite
is not a claim that every image is byte-identical or every possible scene is correct.

[Linux render-harness CI at `4b02c4f27`](https://github.com/jakildev/IrredenEngine/actions/runs/38351626445)
passes all 88 suites. Its initial failure exposed an integer `abs` overload in the
new C++ shader adapter; importing `std::abs` and pinning its scalar return type
fixed the adapter without changing production geometry or weakening the coplanar
assertion. Native OpenGL visual smoke and receiver-stage profiling remain open.
The [reconciliation report](../4362-reconciliation/README.md) records the later
#4362 integration and merged FIELD fog policy; the #4246 hold is resolved.
