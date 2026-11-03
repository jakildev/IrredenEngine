# Detached triangle receiver check

The complete index changes sparse object pixels in eight CanvasStress captures.
A correct candidate query alone cannot certify those changes: the receiver must
also lie on the geometry displayed by its rendering route. This check covers the
`LOCAL_TRIANGLES` detached revoxelized route, including the comparison scene's
SO(3) cube. It does not approve reference updates by appearance.

## Surface construction

The private revoxelized raster uses cardinal-zero projection, identity face
deformation and depth axis `(1,1,1)`, as set by `buildVoxelFrameData`. The raster
selects exposed faces from its camera-aligned occupancy, without opposite-polarity
riser substitution or the legacy dilation. An integer micro-origin `m` emits two
stored pixels per visible face. Both retain integer depth `d=m.x+m.y+m.z`; there
is no fractional depth lost in `isoDepthAlongAxis` on this route.

Let `I(i,j,d)=((2d-3i-j)/6,(2d+3i-j)/6,(d+j)/3)` be the inverse projection.
For a stored pixel relative to the raster origin, the receiver adds
`(1-f/2,f/2,0)`, with `f=1/3` for odd `i+j` and `f=2/3` for even `i+j`.
Integer micro-origins have even projected parity. Substituting the six emission
offsets produces the following points relative to the micro-origin:

| Face | First half | Second half |
|---|---|---|
| X | `(0,2/3,1/3)` | `(0,1/3,2/3)` |
| Y | `(2/3,0,1/3)` | `(1/3,0,2/3)` |
| Z | `(2/3,1/3,0)` | `(1/3,2/3,0)` |

These are the centroids of the finite half-face triangles, not arbitrary offsets
from the surface. The receiver divides by effective subdivision, subtracts the
voxel half-cell anchor, rotates by the camera inverse and adds the world origin.
The finite caster uses the same rounded cell position, half-cell anchor, rotation
and world origin. Both world origins include the rotated rendered-cell phase.
Changing camera quadrant does not change that equality.

## Executable evidence and limits

Run `python3 scripts/tests/test_render_detached_triangle_receiver.py`.
The scalar C++ adapter executes production expressions from both shader backends:
voxel publication and depth encoding, receiver reconstruction, finite caster
quantization/corner construction, sun projection and finite-face ray separation.
Expected surface points come from literal triangle vertices and an independent
quaternion matrix, rather than the receiver inverse.

Each backend passes 35,640 centroid controls and 142,560 finite-ray controls.
The cases include negative and fractional positions, subdivision modes and
scales, both storage parities, nonzero canvas offsets and rendered-cell phase,
four yaw quadrants and two arbitrary-axis rotations. Each own-face ray has
separation within the existing quantization tolerance; a finite face placed a
quarter unit toward the sun is accepted and one placed behind is rejected. A
face translated laterally off the ray is rejected, and nonfinite results fail.
Stored raw depth is also checked against the independent integer micro-origin.
Parity reversal, a missing half-cell anchor and an incorrect subdivision factor
are independently rejected by the centroid oracle.

This establishes the local receiver/caster mathematical contract under those
frame inputs. It does not read a native elected owner, certify GPU uploads,
validate candidate-list completeness or test the legacy dilated raster. It also
does not require a stepped object to have uniformly lit faces: a different
occupied cell may legitimately block the sun. The matched [native scene check](scene-check/README.md) now supplies the remaining
index evidence: all eleven candidate captures match a complete-pool diagnostic
byte for byte, with the replacement restricted to complete spilled tiles.
Incomplete production tiles retain their normal fallback behavior. A repeated
candidate run also matches all eleven, while the parent reproduces all eleven
previous references; eight images change between parent and candidate.

Together, the native comparison and this mathematical contract support the eight
reference updates: candidate traversal agrees with all published geometry at the
native receiver positions, and the detached triangle positions match their
finite caster surfaces. This does not make other rendering representations
interchangeable or certify untested grazing angles and overflow densities.
