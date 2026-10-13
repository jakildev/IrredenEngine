# Cylinder shadow receiver reconstruction

An analytical cylinder's shadow receiver now lies on the same expanded field
used by its finite caster. Its retained producer key selects the camera ray;
the existing bounded cylinder solver finds that ray's first surface. Rounded
placement, camera yaw, model rotation and effective density follow the producer.
This changes sun visibility, not elected coverage, face normals or materials.
There is no blur, increased bias or same-entity shadow rejection.

## Native Metal evidence

Both arms use the same Debug executable built from `7df2843fd` plus the probe
fixture. The after arm changes the receiver shaders. Each run archives its
source patch, binary hash, actual settled camera states, original capture hashes
and clean-exit log. `comparison.json` verifies all 240 before/after pairs:

- 30 cases × eight yaw angles; all 60 launches exit cleanly.
- All 216 expected-unchanged pairs are byte-identical: shadows disabled,
  sphere/cone/torus, cylinder density one and hollow representation controls.
- 23 of 24 eligible cylinder pairs change: self-only, external blocker, and
  pitched external blocker. Total changed pixels: 196,760.
- The primary 16 self/external pairs have no changes outside the blue receiver
  and no blue-material coverage changes (`material-attribution.json`). This is
  a fixture-specific check, not a semantic-ID oracle.

Selected original full frames are retained for all three changed cases and a
shadows-disabled control. Unselected control PNGs remain local; their hashes and
exact comparison results are archived. The external and pitched eight-yaw
sweeps also have permanent Metal references in IRCanvasStress. The existing
comparison thresholds are unchanged. Other backends still require native
captures; executing both shader math adapters does not establish native GL parity.

Example: [before](before/cylinder-external/yaw-045.png) and
[after](after/cylinder-external/yaw-045.png). [Visual review](visual-review.md)
records the inspected quadrants and limitations.

Reproduce the external sweep with the before or after shader tree:

```sh
fleet-run IRCanvasStress --only shadowbox,floor --probe-analytic-shape cylinder --probe-overlapping-source --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 6 --auto-screenshot 6 --sweep-yaw 0 5.49778714 8
```

Add `--analytic-shape-tilt .45` for the pitched case, `--no-shadows` for the
material/coverage control, or `--probe-analytic-hollow` for the hollow fallback.
`--zoom 1` exercises density one. The other accepted probe shapes are sphere,
cone and torus; the existing sphere flag remains an alias.

## Mathematical acceptance and deliberate exclusions

`test_render_curved_shadow_receiver.py` runs the actual GLSL and Metal helpers
against independent double-precision field brackets, cap rays, quaternion
transforms and retained owner keys. It includes 255 transformed cylinder rays
per backend, six cap rays, 17 unsupported-mode controls, and disconnected-field
counterexamples. Wrong density, rotation and placement mutations are rejected.
The sphere route and cardinal provenance suites also pass.

The expanded cylinder is convex, so the retained integer sample and first
continuous entry belong to the same connected occupied interval. That is not
true in general. A rotated torus can have a nearer occupied interval
`[-13.539775, -13.407965]` containing no integer sample, while a farther interval
retains depth 8. The positive-threshold cone field can also have disconnected
intervals where its linear side meets its spherical apex cap. Both stay on the
existing receiver path. A future extension must select the occupied interval
containing the retained sample; simply selecting the earliest root is wrong.

## Remaining work and cost limits

The dotted/comb band on the cylinder side remains visible with shadows disabled.
It needs a finite emitted-face ownership/parity investigation; these images do
not prove whether every small stair face is valid. Cone/torus receiving and
continuous-angle temporal coverage are also unfinished.

The solver executes per occupied canvas receiver texel, potentially repeating
the owner ray across six emitted face halves. Rounded caps can invoke bounded
torus root queries. A Release comparison was attempted through the corrected
launcher, which selected benchmark priority despite screenshot arguments. Host
admission was refused (exit 75); `cost-refusal/` preserves the evidence. There is
no new cost measurement, performance-neutrality claim, or million-entity
throughput claim for this change.
