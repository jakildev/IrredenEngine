# Plan — #2009: smooth/sub-tile fog-of-war reveal (analytic vision circle)

**Task:** a smooth / sub-tile fog-of-war reveal that tracks a smoothly-moving
observer without per-cell vibration, on GL + Metal — and an option to keep the
existing hard voxelized circle.

## Why not "feather the grid" (the original recommendation)

The first pass softened the *grid*: a CPU `smoothstep` feather band + a shader
4-tap bilinear. But the fog grid is at **voxel resolution**, so any softening of
it is just *blur* of voxel-resolution data — the circle's interior still snaps
to voxels and the edge reorganizes at cell granularity as the observer moves.
That is exactly the "blurs the edges but still snaps to voxels" failure. A
grid-resolution source cannot represent a crisp sub-voxel circle.

## Approach (shipped) — analytic vision circle, max-combined with the grid

The fog shader already recovers each pixel's **continuous** world column
(`pos3D.xy`) before it rounds to a cell. Evaluate the reveal disc analytically
from that, per pixel, instead of sampling a voxel grid:

1. **Two reveal sources, max-combined in `c_fog_to_trixel` (GLSL + Metal):**
   - **Grid** (the existing 256² texture): a single NEAREST read — coarse,
     voxel-quantized explored/visible *memory* and the hard **voxelized circle**
     (`revealRadius`, unchanged). The 4-tap bilinear is **removed**.
   - **Analytic vision circles** (new): up to `kMaxFogVisionCircles` world-space
     discs in a tiny UBO; `state = max(grid, max_i(1 - smoothstep(r-aa, r+aa,
     |pos3D.xy - center_i|)))`. Crisp at render resolution, slides smoothly with
     sub-voxel motion, reveals partial voxels at the boundary.
   - **Zoom-stable AA:** `aa = max(edgeSoftness, worldPerPixel)`, where
     `worldPerPixel` is recovered in-shader from the iso inverse-projection
     Jacobian (the `+x` neighbour at the same depth). No `fwidth` (compute
     shader), no AA uniform; the rim stays ~1px at every zoom. `edgeSoftness`
     (per-circle `.w`, default 0) adds an optional deliberately-soft falloff.

2. **CPU API (additive; the integer grid path is byte-identical):**
   - `C_CanvasFogOfWar`: `FrameDataFogObservers` UBO payload held on the
     component + `addVisionCircle` / `clearVisionCircles`. The float feather
     `revealRadius(float,float,float,float)` is **removed**.
   - `IRPrefab::Fog::setVisionCircle` / `addVisionCircle` / `clearVisionCircles`.
   - `System<FOG_TO_TRIXEL>`: a tiny per-frame UBO upload (small, unconditional —
     no dirty flag) + bind before dispatch.

3. **Metal buffer-table constraint:** the Metal 0-30 buffer table is full, so the
   observer UBO **aliases slot 27** (`kBufferIndex_FrameDataLightingToTrixel`).
   Fog runs immediately after lighting (it masks the *lit* canvas), lighting is
   done with slot 27 by then, nothing between reads it, and lighting rebinds
   before its own dispatch — same rebind-before-use discipline the engine
   already relies on. Documented in `ir_render_types.hpp` + both shaders.

4. **Demo:** `fog_demo --moving-observer` drives `setVisionCircle` at a smooth
   float center on an all-unexplored grid → the crisp smooth circle alone on
   black. The default static scene keeps the **voxelized** grid circle + explored
   band, so both reveal styles sit side by side in one demo.

## Detached-player payoff (#211)

The smooth-moving player just calls `Fog::setVisionCircle(x, y, r)` each frame —
the disc follows continuously with no grid quantization and no per-frame texture
upload. The grid stays available for static-terrain "explored memory" if wanted;
per-entity memory policy (enemies never remembered) stays game-side.

## Files
- `engine/render/include/irreden/render/ir_render_types.hpp` — `kBufferIndex_FogObservers` (alias of 27).
- `engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp` — `FrameDataFogObservers`, vision-circle API; drop float feather.
- `engine/prefabs/irreden/render/fog_of_war.hpp` — `setVisionCircle` / `addVisionCircle` / `clearVisionCircles`; drop float overload.
- `engine/prefabs/irreden/render/systems/system_fog_to_trixel.hpp` — UBO create + per-frame upload + bind.
- `engine/render/src/shaders/c_fog_to_trixel.glsl` + `metal/c_fog_to_trixel.metal` — single grid read + analytic circles + Jacobian AA (lockstep).
- `engine/math/include/irreden/ir_math.hpp` — drop the now-unused `smoothstep` wrapper.
- `creations/demos/fog_demo/main.cpp` — `--moving-observer` drives the analytic circle.

## Verification
- `IRFogDemo` builds + runs clean (macOS/Metal); `--moving-observer` shows a
  crisp, smoothly-sliding circle revealing partial voxels; static scene shows the
  hard voxelized circle + explored band.
- Both shaders edited in lockstep; GL/Linux cross-host smoke still owed (authored
  on Metal).

**Class:** opus. No epic — single PR.
