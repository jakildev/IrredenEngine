# engine/prefabs/irreden/asset/ — asset I/O commands + component adapters

Prefab commands for saving and loading engine assets at runtime, plus
header-only adapters that bridge component types (`C_ShapeDescriptor`,
`C_VoxelSetNew`, ...) to the low-level binary formats in
`engine/asset/`. Adapters live here so `engine/asset/` itself stays
below the component layer.

## Adapters

- `voxel_set_io.hpp` — `IRAsset::saveVoxelSet(span<const
  C_ShapeDescriptor>, ...)` writes a SHAPES-mode `.vxs` shape-group
  asset. Parallel optional spans (`offsets`, `rotations`, `csgOps`,
  `boneIds`) carry the per-instance composition metadata that
  `C_ShapeDescriptor` itself doesn't store. Loader path: callers use
  `IRAsset::loadShapeGroup` (in `<irreden/asset/voxel_set_format.hpp>`)
  and reconstruct entities from the returned `ShapeRecord`s.

## Commands

- `commands/command_save_main_canvas_trixels.hpp` — `Command<SAVE_MAIN_CANVAS_TRIXELS>`.
  Grabs `C_TriangleCanvasTextures` off the entity named `"main"` via
  `IRRender::getCanvas("main")` and calls `saveToFile("main_canvas")`, writing
  `main_canvas.txl` to the current working directory.

  The `SAVE_MAIN_CANVAS_TRIXELS` enum entry lives in
  `engine/command/include/irreden/command/ir_command_types.hpp`.

## Typical usage

```cpp
#include <irreden/asset/commands/command_save_main_canvas_trixels.hpp>
IRCommand::Command<SAVE_MAIN_CANVAS_TRIXELS>::create();
```

Bind to an input key via `IRCommand::bindCommand<SAVE_MAIN_CANVAS_TRIXELS>(...)`
if you want interactive save-on-keypress.

## Gotchas

- **Canvas must be named `"main"`.** `IRRender::getCanvas("main")` panics if no
  canvas with that name exists. If your creation uses a differently-named canvas,
  copy the command and substitute the name.
- **Output path is cwd-relative.** The file is written to wherever the exe's
  working directory is. Use the `IR<Name>Run` CMake target (which sets
  `WORKING_DIRECTORY` correctly) to avoid writing files to unexpected places.
