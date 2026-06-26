# Plan — #2009: smooth/sub-tile fog-of-war reveal (feathered edge)

**Task:** smooth / sub-tile / feathered fog-of-war reveal that tracks a
smoothly-moving observer without per-cell vibration, on GL + Metal.

## Verified current state (read the real code, not the issue's guess)

- `engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp` —
  `revealRadius(int cx, int cy, int radius)` writes a **hard**
  `kFogStateVisible (255)` per **integer** cell inside a **Euclidean** disc
  (`dx*dx+dy*dy <= radiusSq` — the post-#1994 circular reveal). Center and
  radius are **integers**. `.r`-only CPU mirror, RGBA8 GPU texture,
  `dirty_`-gated `subImage2D` (documented exception).
- `engine/render/src/shaders/c_fog_to_trixel.glsl` and
  `metal/c_fog_to_trixel.metal` — **both** bind the fog texture as an
  **unfiltered image** (`layout(rgba8,binding=2) readonly uniform image2D` /
  `texture2d<float, access::read>`) and read it with **`imageLoad` / `.read()`**
  at the **rounded integer** cell. Modulation is **3 hard buckets** (`>=0.75`
  visible/pass-through, `>=0.25` explored = luminance ×0.4, else black).
- `engine/prefabs/irreden/render/systems/system_fog_to_trixel.hpp` —
  once-per-frame `.r->RGBA8` expand + `subImage2D` upload; binds the texture
  `bindAsImage(2, READ_ONLY, RGBA8)`.

**Two correctness findings that change the issue's recommended approach:**
1. The issue's "switch the texture filter `NEAREST -> LINEAR`" is a **NO-OP** on
   both backends — neither shader samples through a sampler; both use unfiltered
   image reads (`imageLoad` / `access::read`), which ignore `TextureFilter`.
   Hardware bilinear is unreachable without switching to a sampled
   texture+sampler on both backends, which would break the deliberate `image2D`
   binding-layout sharing with AO/sun-shadow and the OOB-as-visible invariant
   (image reads have no wrap mode). **Drop the filter change; do bilinear
   manually (4-tap `imageLoad` lerp) keeping the image binding.**
2. Integer reveal center is the temporal-vibration root. A soft *spatial* edge
   (bilinear) alone still pops one whole cell each time a moving observer
   crosses an integer boundary. Sub-tile smoothness requires a **float-center,
   float-distance** gradient write so the field evolves continuously with
   sub-cell motion.

Callers of `revealRadius` (all init-time, integer center):
`creations/demos/{fog_demo,perf_grid,skeletal_demo,lua_perf_grid}`,
`creations/demos/lighting/common/lighting_demo_scene.hpp`, and game
`arcade_starter`/`scene_demo`. `IRMath::length` exists; `IRMath::smoothstep`
does NOT (only `mix`/`fract`/`clamp`). The Metal `c_fog_to_trixel`
threadgroup-size is already registered (#1986) — editing the existing kernel
needs no re-registration.

## Approach (single committed) — CPU-authored feather + manual bilinear + continuous lerp

**1. CPU — add a float-center feathered reveal (additive; keep the int one byte-identical).**
- Add `IRMath::smoothstep(edge0, edge1, x)` wrapper to
  `engine/math/include/irreden/ir_math.hpp`.
- Add `C_CanvasFogOfWar::revealRadius(float cx, float cy, float radius, float feather)`:
  for each integer cell in the bounding box,
  `d = IRMath::length(vec2(x-cx, y-cy))`,
  `v = IRMath::smoothstep(radius, radius - feather, d)` (1.0 inside, ramping to
  0 across the last `feather` units; clamp `feather` to `(0, radius]`), store
  `value = round(255*v)` via **max-combine** so overlapping reveals and the
  explored floor compose sanely; set `dirty_`/`allUnexplored_` like today. Keep
  the existing integer `revealRadius` unchanged so the existing init callers and
  their render-verify references stay **byte-identical**.
- `engine/prefabs/irreden/render/fog_of_war.hpp` — add the
  `IRPrefab::Fog::revealRadius(float,float,float,float)` overload; fix the stale
  doc (revealRadius doc still says "taxicab" — it's Euclidean since #1994).

**2. Shader (GLSL + Metal in lockstep) — manual bilinear + continuous modulation.**
- Replace the single rounded-cell `imageLoad` with **4-tap bilinear** over the
  **continuous** recovered coord: `vec2 fogCoord = pos3D.xy + halfExtent;
  vec2 base = floor(fogCoord); vec2 f = fogCoord - base;` load `(base)`,
  `(base+(1,0))`, `(base+(0,1))`, `(base+(1,1))`; each tap **OOB -> 1.0
  (visible)** to preserve the OOB-as-visible invariant;
  `state = mix(mix(t00,t10,f.x), mix(t01,t11,f.x), f.y)`.
- Replace the 3-bucket branch with a **continuous lerp that is byte-identical at
  the canonical 0 / 0.5 / 1.0 values**: `exploredColor = vec3(dot(src.rgb,LUMA))*0.4;`
  then `v>=0.5 -> mix(exploredColor, src.rgb, (v-0.5)*2)`,
  `v<0.5 -> mix(vec3(0), exploredColor, v*2)`; alpha = `src.a`.
  (At v=1 -> src exactly; v=0.5 -> exploredColor exactly; v=0 -> black exactly.)
- Keep `TextureFilter::NEAREST` (irrelevant to image reads); do not switch the
  binding to a sampler.

**3. Demo — give the smooth/temporal behavior a headless verification vehicle.**
- Add a moving-observer mode to `creations/demos/fog_demo/main.cpp` (e.g.
  `--moving-observer`): each frame `Fog::clear()` + `Fog::revealRadius(fx, fy,
  radius, feather)` with a smoothly-advancing float center. Keep the default
  static path unchanged.

## Byte-identity / drift analysis
- **Continuous lerp** is byte-identical for any hard `0/128/255` field -> demos
  with off-screen fog boundaries (perf_grid, skeletal_demo, lua_perf_grid) stay
  **byte-identical**.
- **Bilinear** softens fog only at a visible boundary -> the intended change.
  Only `fog_demo` and `lighting_demo` have on-screen boundaries; **refresh those
  two render-verify references** as intentional drift and call it out for
  reviewers.

## Files
- `engine/math/include/irreden/ir_math.hpp` — add `IRMath::smoothstep`.
- `engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp` —
  float feathered `revealRadius`; `std::->IRMath` cleanup.
- `engine/prefabs/irreden/render/fog_of_war.hpp` — float overload + taxicab->Euclidean doc fix.
- `engine/render/src/shaders/c_fog_to_trixel.glsl` + `metal/c_fog_to_trixel.metal` — bilinear + continuous lerp (lockstep).
- `creations/demos/fog_demo/main.cpp` — moving-observer mode.

## Acceptance / verification
- `render-debug-loop` on `fog_demo` (GL host + Metal host): before/after
  full-frame + ROI-crop pair showing the feathered edge; static yaw-0 capture
  stays byte-identical away from a fog boundary.
- Moving-observer capture sequence: a smoothly-advancing center produces a
  smooth, non-vibrating edge (no per-cell pop); feather width visibly tunable.
- Refresh `fog_demo`/`lighting_demo` render-verify references (intentional
  drift); confirm perf_grid/skeletal/lua_perf_grid references unchanged.
- GL + Metal parity (`backend-parity` if authored single-host); both shaders
  edited in lockstep.

## Sibling / in-flight reconciliation
- **#2008** (engine, fog cull): different files (visibility/cull path vs fog
  shader+reveal). Both edit `c_fog_to_trixel` (#2008 removes the dead hard-black
  store; this replaces the bucket branch with continuous lerp) and
  `component_canvas_fog_of_war.hpp` — whichever lands second rebases on the
  other. No semantic conflict; coordinate on shared fog semantics only.
- **#211** (game, player on a detached canvas): the downstream consumer; this
  ships the float `Fog::revealRadius` it will call. Separate repo, no conflict.
- Builds on **#1994** (Euclidean reveal) and **#1986** (Metal `c_fog_to_trixel`
  threadgroup registration).

**Class:** opus. No epic — single PR.
