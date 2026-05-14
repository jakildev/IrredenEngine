# engine/prefabs/irreden/asset/ — asset I/O commands + component adapters

Prefab commands for saving and loading engine assets at runtime, plus
header-only adapters that bridge component types (`C_ShapeDescriptor`,
`C_VoxelSetNew`, ...) to the low-level binary formats in
`engine/asset/`. Adapters live here so `engine/asset/` itself stays
below the component layer.

## Adapters

- `voxel_set_io.hpp` — `IRAsset::saveVoxelSet(span<const
  C_ShapeDescriptor>, ...)` writes a SHAPES-mode `.vxs` shape-group
  asset (plus a `.vxs.json` sidecar; Rule #6). Parallel optional
  spans (`offsets`, `rotations`, `csgOps`, `boneIds`) carry the
  per-instance composition metadata that `C_ShapeDescriptor` itself
  doesn't store. Loader path: callers use `IRAsset::loadShapeGroup`
  (in `<irreden/asset/voxel_set_format.hpp>`) and reconstruct
  entities from the returned `ShapeRecord`s.

## Typical usage

Adapters bridge component types to asset-level save/load. To save the current
shape group:

```cpp
#include <irreden/asset/voxel_set_io.hpp>
IRAsset::saveVoxelSet(shapes, path);
```
