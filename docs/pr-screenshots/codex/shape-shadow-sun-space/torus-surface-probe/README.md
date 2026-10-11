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

The next correctness control should reconstruct the producer's accepted occupied
samples independently, project their finite exposed faces, and compare depth,
half-face ownership and normals at interior pixels. Compare continuous torus
ray intersections separately. That distinguishes an incorrect winning triangle
from intentional steps before changing normals, geometry or receiver lighting.
No normal averaging or rendering change is included in this diagnostic.
