# Rotate gizmo rings and the x-ray blend's one writer

Captured on `macos-debug` (Metal) from the voxel editor's own sessions, with
`python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session <name>`. A
session writes one frame per segment, in order, to
`build/creations/editors/voxel_editor/save_files/screenshots/`.

| File | Session and segment | Tree |
|---|---|---|
| `part_gizmo_reach_bake_center-before.png` | `part_gizmo_reach`, `bake_center` | master `8a1bc317a` |
| `part_gizmo_reach_bake_center-after.png` | same | this branch |
| `part_gizmo_reach_array-before.png` | `part_gizmo_reach`, `array` | master `8a1bc317a` |
| `part_gizmo_reach_array-after.png` | same | this branch |
| `gizmo_over_blob-before-crop.png` | the `bake_center` frame, 320x300 px around the part, 3x nearest | master `8a1bc317a` |
| `gizmo_over_blob-after-crop.png` | same | this branch |
| `part_ring_turn_turn_{x,y,z}-after-crop.png` | `part_ring_turn`, one segment per ring, same crop | this branch |

`part_ring_turn` does not exist on master, so its frames have no before.

On master the three rings are one torus in one place, and the handles' x-ray
blends race the pixel's owner, so the gizmo over the baked blob is a small
patch of mixed colour that changes between runs. On this branch each ring is
a torus about its own axis at radius 9, and every pixel has one writer.
