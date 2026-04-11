# engine/common/ — constants and platform detection

Compile-time-only module. No managers, no state. Two headers every other
module includes.

## Headers

- `engine/common/include/irreden/ir_constants.hpp` — numeric constants
  that are global engine tuning knobs (frame rate, chunk sizes, zoom
  bounds, trixel distance sentinels, world extents).
- `engine/common/include/irreden/ir_platform.hpp` — `constexpr` platform
  detection (OS + graphics backend) and the `IRPlatform::kGfx` conventions
  struct.

There is no `ir_common.hpp` umbrella — include the specific header.

## `ir_constants.hpp` highlights

- `IRConstants::kTargetFps = 60` — main loop fixed-step rate.
- `IRConstants::kChunkSize = 32` — voxel chunk edge (32×32×32).
- `IRConstants::kTrixelDistanceMaxDistance` — sentinel distance used to
  clear the trixel depth texture. Read by GLSL as the "nothing here" depth.
- Zoom bounds, world extents, voxel pool size constants.

Treat this file as read-mostly. Changing a value here typically ripples
through shaders (which read it via `ir_constants.glsl`) and several
systems.

## `ir_platform.hpp` highlights

- `IRPlatform::kIsOpenGL`, `kIsMetal`, `kIsVulkan` (compile-time bools).
- `IRPlatform::kIsWindows`, `kIsMac`, `kIsLinux`.
- `IRPlatform::kGfx` — a struct of conventions that differ by backend:
  NDC Y direction, depth range convention (0..1 vs -1..1), mouse Y flip.
  Use this instead of `#ifdef`ing backends in call sites.

## Internal layout

```
engine/common/
└── include/irreden/
    ├── ir_constants.hpp
    └── ir_platform.hpp
```

No `src/` — everything is header-only.

## Gotchas

- **Changing a constant is an ABI event.** `ir_constants.hpp` is included
  by half the engine. Changing a value triggers a massive rebuild; just
  don't be surprised.
- **`kGfx` is `constexpr`.** Branching on it is optimized away per build.
  Don't try to flip backends at runtime — the decision is baked into the
  binary.
- **GLSL mirror.** Any constant that also lives in `ir_constants.glsl`
  must be kept in sync manually. Grep the constant name in
  `engine/render/src/shaders/` before changing it.
