# Torus surface classification probe

Native Metal at `3fe9f6cd6477976473858bda30bd0d9d2b8c73c7`, effective
subdivision eight. Eight matched beauty/normal frames disable both sun shadows
and AO, with no floor. Both runs exit cleanly. Commands, source revision, RGB
hashes and full-frame color-pair counts are in [capture-pairs.json](capture-pairs.json).
The example PNGs are unmodified native captures.

| Yaw | Beauty, no shadows or AO | Normal diagnostic |
|---|---|---|
| 0° | ![](beauty-0.png) | ![](normals-0.png) |
| 45° | ![](beauty-1.png) | ![](normals-1.png) |

At yaw zero, every non-background pixel belongs to one of three normal classes:
63,376 pixels of `(128,128,0)` have beauty color `(64,115,141)`; 59,824 pixels
of `(0,128,128)` and 28,176 of `(128,0,128)` have beauty color `(40,72,88)`.
The bands therefore coincide with face-normal selection even with shadows and
AO disabled. This localizes their immediate shading input; it does not prove
that the normals or represented geometry are wrong.

`shapeBoxReceiver` excludes curved shapes, so the torus does not use finite
fragment reconstruction. Its general depth search and three emitted diamonds
retain their sample/face-slot convention, and lighting decodes those slots into
axis normals. A continuous torus gradient is a useful representation comparison,
but it is not an oracle for stepped voxel faces. Legitimate depth steps can have
different exposed normals and triangular risers.

## Executable classification and sampling correction

`scripts/render_torus_surface_probe.py` independently enumerates accepted
fractional samples. For integer iso coordinates `(i,j)` and depth `d`, the sample
center in subdivision-scaled view space is
`((2d-3i-j)/6, (2d+3i-j)/6, (d+j)/3)`. These centers do not form a single integer
voxel lattice; rounding them would test a different representation.

The [original report](classification.json) reproduces all 151,376 foreground
pixels and the full background exactly: 18,406 accepted samples produce 18,922
stored texels. This is a publisher/image agreement check, not a visual approval.
The companion test executes extracted GLSL and Metal depth search, face offsets,
depth and owner election, publication guards and normal decoding. Both backends
reject six mutations; shuffled submission order preserves the elected output.
Independent small controls test occlusion, edge ties and missing/extra pixels.

Separately, the original report intersects screenshot-pixel rays with the union of finite unit
cubes centered at those fractional samples. It examines all equal encoded-depth
owners rather than inferring a native owner from normal color. Its classifications
of published pixels are:

| Finite-face model classification | Pixels |
|---|---:|
| Nearest exposed face for every possible owner | 40,056 |
| Selected face does not intersect the ray | 37,504 |
| Selected face is occluded | 26,390 |
| Classification depends on the unknown owner | 46,906 |
| Outside the finite union | 520 |

The model also covers 808 pixels absent from publication. Normal classes differ
from the nearest model face at 49,124 published pixels; the 520 pixels without
a finite hit are counted separately. The report retains
examples, depth intersections, input hashes and the fixed Z1 registration.

Those rays sample more finely than the native gather. The 2560×1440 screenshot
is a 2× nearest presentation of a 1280×720 game view within a 1284×722 padded
framebuffer. Each 2×2 screenshot block shares one rendered sample center. The
[native-gather report](native-gather-classification.json) uses those centers and
keeps counts in screenshot pixels so the reports are comparable:

| Finite-face model classification at native gather centers | Screenshot pixels |
|---|---:|
| Nearest exposed face for every possible owner | 27,412 |
| Selected face does not intersect the ray | 30,992 |
| Selected face is occluded | 40,220 |
| Classification depends on the unknown owner | 52,376 |
| Outside the finite union | 376 |

At this sampling, 67,524 published pixels differ from the nearest finite-model
normal and 952 model pixels are unpublished. Publisher/image agreement is
unchanged. Neither report establishes the native owner where encoded depths tie.

This correction matters for individual examples: screenshot `(1253,569)` maps
to padded framebuffer `(628,285)` and iso `(-6.75,-75.5)`, where the published Z
normal matches the nearest model face. The finer screenshot ray previously
classified that pixel as Z-versus-X. Keep it as a sampling regression control.
Screenshot `(1250,570)` instead maps to iso `(-7.25,-74.5)` and retains a unique
Z-versus-X difference at sample `(-8,-74,25)`. Its finite-model X entry depth is
24.375.

## Native sample ownership

ShapeDebug's opt-in `--shape-sample-probe X Y` reads the actual retained owner,
tile, descriptor, encoded depth and projection on the first capture frame. A
publication tick rejects stale samples; bounds and entity/face checks reject
foreign or invalid records. The probe synchronizes the GPU once and is unsuitable
for timing runs. It names the main-canvas sample, not the final winner after GUI,
sprites or detached canvases. Its depth reconstruction is restricted to the
origin-zero cardinal torus; unsupported geometry has an explicit status.

The independent Python checker additionally requires the isolated radius-4/2,
subdivision-eight fixture and validates the full encoded depth before classifying
the actual owner. The native readbacks resolve both examples:

| Screenshot pixel | Native owner | Sample `(i,j,d)` | Finite-model result |
|---|---:|---|---|
| `(1253,569)` | 116711 | `(-7,-75,26)` | Published Z is the nearest exposed face. |
| `(1250,570)` | 127505 | `(-8,-74,25)` | Published Z misses the ray; its nearest face is X. |

Raw records, capture commands, source hashes and comparisons are retained in
[native-owner-captures.json](native-owner-captures.json),
[owner-1253-569.json](owner-1253-569.json) and
[owner-1250-570.json](owner-1250-570.json). Both runs exit cleanly. The native
readback confirms the second representation difference independently of inferred
tile ordering; it does not make the finite-cube union the required SDF geometry.
All four final frames (yaw zero and 180° in each run) are RGB-identical to the
retained normal controls, so the existing screenshots also show this diagnostic
build. Five focused native tests, three retained-geometry adapter tests and seven
publisher/classifier tests pass, including rejected inconsistent-depth and
wrong-fixture records. Independent review is clean.

These counts measure a representation difference, not certified renderer errors:
the current smooth SDF path uses rectangular gather, which does not promise
finite unit-cube coverage. The next rendering change must establish the intended
curved-surface geometry, using the native owner/depth evidence to resolve ties.
Compare an explicit finite-face route against these controls before changing
normals or extending curved receivers. Continuous torus gradients remain a
separate comparison; no normal averaging or renderer change is included here.

Run from the repository root:

```bash
python3 scripts/tests/test_render_torus_surface_probe.py
python3 scripts/render_torus_surface_probe.py --normals docs/pr-screenshots/codex/shape-shadow-sun-space/torus-surface-probe/normals-0.png --output /tmp/torus-surface-classification.json
fleet-run IRShapeDebug --spin-yaw --auto-screenshot 1 --zoom 8 --no-ao --pivot-origin --spin-shape torus --no-shadows --debug-overlay normals --shape-sample-probe 1250 570 --shape-sample-probe-output /tmp/shape-owner.json
python3 scripts/render_torus_surface_probe.py --native-probe /tmp/shape-owner.json --pixel 1250 570 --output /tmp/torus-native-owner.json
```

The CLI defaults to native gather centers; `--sampling screenshot-ray` reproduces
the original finer-ray diagnostic. The CLI's exit status grades publisher/image
agreement only. Finite-model
differences are reported without turning the existing artifact into an accepted
geometry golden. `--pixel X Y` explains a particular screenshot location.
