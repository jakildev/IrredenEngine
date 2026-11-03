# Grazing finite-face precision witness

This is a diagnostic follow-up, not a failing CI test or a production fix. It
characterizes a pre-existing numerical limitation of `sourceFaceRaySeparation`.
That function is unchanged by spill-list commit
`9256ac0473584699c98de0e1f470378224f7fff0`. This experiment does **not** attribute
any current screenshot difference to the limitation.

## Reproduce

```sh
python3 docs/pr-screenshots/codex/shadow-index-complete-tiles/grazing-follow-up/run_experiment.py --repo . --output /tmp/grazing-results.json
```

The script imports the repository's existing overflow-test host harness and
compiles the actual GLSL and Metal projected-face and finite-face query helpers
to two small CPU programs. It does not launch the engine or change any files in
the checkout. `results.json` records revision, helper hashes, compiler flags and
all 42 cases for each backend. No fast math or contraction is enabled.

## Observed result

Both backend translations agree on every case: 7 of 42 ideal self-surface
midpoints exceed the unchanged `4/1024` positive separation threshold after
absolute projected coordinates are rounded to float. Computing the projection
from a retained relative midpoint first produces zero false self hits in these
42 controls. This is evidence for a design direction, not a general proof.

For a unit face with projected edges `U=(d,0,sqrt(1-d*d))`, `V=(0,1,0)` and
corner `(origin,100,100)`, the intended receiver is its exact midpoint.

| UV origin | d | Actual separation | Double intended midpoint oracle |
| --- | --- | --- | --- |
| 0 | approximately 0.00005 | 0 | 0 |
| 1 | approximately 0.00005 | -0.0006790161 | 0 |
| 100 | approximately 0.00005 | 0.0422363281 | 0 |

At origin 100 the erroneous separation is more than ten times the current
threshold. The determinant remains above the current `1e-6` rejection threshold.
The receiver remains within the closed face footprint, so this is not an edge
ownership dispute. It is the same geometrical relation at different translations.

The independent oracle uses a double cross-product plane intersection, rather
than the shader's inverse 2D parameter solve. Two controls separate the issues:

- The oracle on the **intended** midpoint reports zero separation.
- The oracle on the **already rounded** receiver reports approximately
  `0.0422363166`; performing the intersection in double cannot recover discarded
  input precision. Subtracting corner from those rounded projected inputs also
  leaves the error (`post_projection_shift` in the results).

`world_relative_first` constructs the small relative midpoint before projection
and then calls the same actual shader helper. It is an idealized retained-local-
geometry control, not an implementation of the full engine transform chain.

## Proposed scoped TODO

**Characterize and remove translation-sensitive grazing finite-face self hits.**

1. Add a native diagnostic fixture with a known planar receiver/caster relation,
   translated together through the above ranges, and a sun basis sweep including
   moderate and grazing determinants. Record the world receiver, stored corner
   and edges, determinant, parameter coordinates, separation, cascade and record
   identity. Include near-surface genuine blockers to prevent fixing acne by
   suppressing real occlusion.
2. Compare actual native values against the independent double oracle and CPU
   replay. Capture relevant witnesses from changed scene pixels before assigning
   this issue responsibility for existing screenshots.
3. Prototype subtracting `receiverWorld - sourceCornerWorld` **before** sun-basis
   projection. Retain world/local source geometry and project the small relative
   vector; use projected edges for the finite footprint. Measure the added
   per-candidate arithmetic and storage against the existing once-per-receiver
   projected query. If replacing the record's projected corner, consider index
   construction and analytic record users explicitly.
4. Compare world-origin rebasing performed **before** projection. A shared local
   origin can retain once-per-receiver projection and reduce cancellation, but
   only bounds magnitudes near that origin. Source records shared by cascades
   need an explicit origin contract; simply shifting already projected values
   is ineffective. Rebasing is not by itself a guarantee for all distant/grazing
   geometry.
5. Promote the native/replay fixture to a regression only after choosing a
   supported conditioning/precision contract and checking both backends,
   translations, face orientations, true blockers and performance.

Do not change shadow bias, expand footprints, blur shadows or exclude every
same-entity blocker. Those changes can conceal this issue while removing valid
self-occlusion. Robust predicates or Pluecker intersections applied only to the
already quantized projected inputs cannot reconstruct the missing receiver
position either.

## Limits

- CPU translations do not prove native GPU operation ordering, contraction or
  fast-math behavior.
- This fixture covers finite quads, not reconstructed trixel receivers, analytic
  shape intersection, spill allocation or current scene screenshots.
- Full world coordinates may themselves have lost local displacement before
  subtraction. Local geometry/origin retention must address that upstream case.
- The grazing face's direct sun contribution may be small; image severity and
  prioritization need a native witness. No bias recommendation follows here.
- The script intentionally reports the known issue instead of requiring it to
  stay broken. A future corrected implementation can reduce the reported count.
