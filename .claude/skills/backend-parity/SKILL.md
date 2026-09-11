---
name: backend-parity
description: >-
  Keeps the OpenGL (Linux/Windows) and Metal (macOS) render backends at
  feature parity: audits shaders, backend C++ sources, and render-impl wiring
  for one-sided work, ports the missing side (usually GLSL → MSL, sometimes
  the reverse), and builds and smoke-runs the lagging backend before opening
  a PR. Use when the user says "metal parity", "port to metal", "sync the
  backends", "audit render parity", or after a render PR lands that touched
  only one backend.
---

# backend-parity

Fleet rules for parity work — host must match the lagging side, build-only is
not done, one feature per PR, Opus for math / dispatch / buffer-lifetime ports —
live in [`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md)
§"Cross-platform parity (OpenGL ↔ Metal)" and §"Model split". This skill is
the port procedure.

## Preconditions

1. Host matches the lagging backend (`uname -s`): `Darwin` ports GLSL → MSL on
   `macos-debug`; `Linux` or MinGW/MSYS ports MSL → GLSL on `linux-debug` /
   `windows-debug`. Cross-compilation is not a substitute for the build + smoke.
2. The lagging preset is already configured; never run `cmake --preset`
   ([`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §"Hard
   rules for autonomous fleet roles").
3. `gh auth status` succeeds; the skill ends in a PR.
4. On a feature branch with a clean tree — a parity PR is pure parity work.

## Flow

### 1. Resolve the scope

Take the mode from the trigger phrase or ask:

- **Full audit** — walk the whole render tree.
- **PR-scoped** — mirror PR `<N>`; only the files it touched.
- **Commit-range** — mirror everything since `<ref>`.
- **Single feature** — port one named file; skip the audit.

The mode goes at the top of the PR body.

### 2. Enumerate the deviation set

Layout: `engine/render/src/opengl/opengl_*.cpp` ↔ `engine/render/src/metal/metal_*.cpp`
(headers mirror under `engine/render/include/irreden/render/{opengl,metal}/`);
shaders `engine/render/src/shaders/*.glsl` (`c_/v_/f_/g_` stage prefixes per
CLAUDE-BASELINE §Naming) ↔ `engine/render/src/shaders/metal/*.metal`. One
`.metal` file bundles the vertex + fragment pair — a GLSL v/f pair ports to
**one** `.metal` file (`framebuffer_to_screen.metal` is the pattern), and a
`.metal` file ports to **both** `v_<key>.glsl` and `f_<key>.glsl`.

Missing-counterpart audit, GLSL → MSL:

```bash
cd engine/render/src/shaders
for glsl in *.glsl; do
    base="${glsl%.glsl}"
    key="${base#c_}"; key="${key#v_}"; key="${key#f_}"; key="${key#g_}"
    if ! ls metal/${key}.metal >/dev/null 2>&1; then
        echo "MISSING METAL: $glsl  →  metal/${key}.metal"
    fi
done
```

Invert it for MSL → GLSL. For the C++ side, Glob both `opengl_*.cpp` and
`metal_*.cpp`, strip the prefixes, and compare stems — then read
`opengl_render_impl.cpp` against `metal_render_impl.cpp`, because real gaps are
functions, not files. `metal_runtime.cpp`, `metal_cocoa_bridge.mm`, and
`metal_cpp_impl.cpp` have no OpenGL counterpart by design.

### 3. Read the leading side end-to-end

For each gap read the shader or C++ source, its CPU-side feeder / uniform
struct (`engine/render/include/irreden/render/components/` or a `*_types.hpp`
there), the shared interface in `renderer_impl.hpp` and `render_manager.hpp`,
[`engine/render/CLAUDE.md`](../../../engine/render/CLAUDE.md) for the pipeline
position, and — for recent features — the introducing PR
(`gh pr list --state merged --search "<feature>"`, `gh pr view <N> --json body,commits`).

### 4. Write the port

- Target-backend naming: GLSL stage prefixes; MSL bare filename with the stage
  declared inside.
- Uniform layouts byte-identical to the backend-agnostic CPU feeder struct
  (`layout(std140)` order ↔ MSL `struct`, same `[[buffer(N)]]` index). A
  mismatch compiles and renders garbage.
- Same binding slot `N` on both sides: `layout(binding = N)` ↔ `[[buffer(N)]]`
  / `[[texture(N)]]` / `[[sampler(N)]]`.
- Compute dispatch: GLSL `layout(local_size_x = X, ...)` ↔ MSL threads per
  threadgroup set at the dispatch site in `metal_render_impl.cpp`. Grid sizes
  come from `voxelDispatchGridForCount()` in `engine/math/`, never hand-rolled.
  Metal kernels register their threadgroup size —
  `engine/render/CLAUDE.md` §"Metal compute kernel threadgroup registry".
- Iso-projection math: both sides call the same helpers (`ir_iso_common.glsl`;
  mirror as an MSL header under `shaders/metal/` once more than two shaders
  need it). IRMath substitutions: [`.claude/rules/cpp-math.md`](../../rules/cpp-math.md).
- C++ backend functions implement the `IRenderImpl` signature in
  `renderer_impl.hpp`; a new virtual on the leading side must be implemented
  on the lagging side or nothing links.
- A leading-side primitive with no direct equivalent (e.g. `glCopyImageSubData`)
  is replicated by effect (blit encoder, texture copy) or the port stops and
  files the abstraction gap. No `#ifdef METAL` / `#ifdef OPENGL` stub that
  silently renders wrong, no TODO in its place.
- A bug found on the leading side is filed separately; the port mirrors the
  correct behaviour, not the bug.

### 5. Build the lagging backend

```bash
fleet-build --target IRShapeDebug
```

Host/preset mapping and the build-hygiene canary (a `Built target` line with
no `Building ...` line compiled nothing — `touch` and rerun):
[`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md). Compile or link errors
are fixed before committing; a port that cannot be made to build is escalated,
not stubbed.

### 6. Smoke-run

```bash
fleet-run --timeout 15 IRShapeDebug
```

(or the demo that exercises the ported pipeline). Compare against the leading
backend; missing geometry, wrong colours, or flicker is a port failure to fix or
escalate. Screenshot-diff when the demo has a fixed camera.

### 7. Commit and open the PR via `commit-and-push`

Title `metal: <what you ported>` or `opengl: <what you ported>`. Body:

```
## Summary
- Backend-parity port: <brief description, e.g. "MSL version of c_shapes_to_trixel compute">.
- Leading backend: <OpenGL | Metal>. Lagging backend (this PR): <Metal | OpenGL>.
- Scope mode: <full-audit | PR <N> mirror | commit-range | single-feature>.

## Parity reference
- Leading impl: <path:lineN–lineM in the other backend>
- Ported from: <PR #N, commit <sha>, or "pre-existing parity gap found via audit">

## Test plan
- [x] Built target `IR<Demo>` on the lagging preset.
- [x] Ran `IR<Demo>` and confirmed it renders at functional parity
      with the leading backend (no crashes, no obvious visual drift).
- [ ] Reviewer: eyeball diff against leading backend screenshots if you
      have them.

## Notes for reviewer
- <Anything tricky about the translation — layout differences, dispatch
  size changes, sampler state conversion, buffer binding reordering.>
- <Anything that's *not* exact parity and why.>
```

After the PR opens, wait for a user cue before the next gap — no
`start-next-task` unprompted. Review feedback: stay on the branch, address each
comment in a new commit, rebuild and re-smoke (steps 5–6), push and reply.

## Cross-backend translation cheatsheet

| GLSL | MSL |
|---|---|
| `layout(binding = N) uniform` | `constant T& x [[buffer(N)]]` |
| `layout(binding = N) buffer` | `device T* x [[buffer(N)]]` (read/write) / `constant` RO |
| `layout(binding = N) uniform sampler2D` | `texture2d<T> x [[texture(N)]], sampler s [[sampler(N)]]` |
| `layout(local_size_x = X, _y = Y, _z = Z) in;` | `kernel void foo(..., uint3 gid [[thread_position_in_grid]])` + threads per threadgroup at the dispatch |
| `gl_GlobalInvocationID` | `[[thread_position_in_grid]]` |
| `gl_LocalInvocationID` | `[[thread_position_in_threadgroup]]` |
| `imageStore(tex, pos, color)` | `tex.write(color, pos)` on `texture2d<T, access::write>` |
| `imageLoad(tex, pos)` | `tex.read(pos)` |
| `memoryBarrierShared(); barrier();` | `threadgroup_barrier(mem_flags::mem_threadgroup)` |
| `memoryBarrier(); barrier();` | `threadgroup_barrier(mem_flags::mem_device)` |
| `atomicAdd(buf, v)` | `atomic_fetch_add_explicit(&buf, v, memory_order_relaxed)` |
| `vec4 / vec3 / vec2 / float` | `float4 / float3 / float2 / float` |
| `mat4 / mat3` | `float4x4 / float3x3` |
| `#define FOO 1` | same — MSL accepts the C preprocessor |
| `#include "ir_iso_common.glsl"` | `#include "ir_iso_common.metal"` (create it if missing) |

MSL → GLSL inverts each row; the one non-mechanical reversal is dispatch
sizing — read the threadgroup size at the Metal dispatch call and encode it as
`layout(local_size_x = ..., _y = ..., _z = ...)`. Metal also negates clip
`position.y`: `engine/render/CLAUDE.md` §"Metal negates clip `position.y`; GL
does not".

## Escalation

A Sonnet agent stops and files a `fleet:opus` issue stating the reason when the
port touches `engine/math/`, relies on a primitive with no obvious equivalent,
shows a visual difference the diff does not explain, or breaks on GPU-resource
lifetime (buffer creation, destruction, persistent mapping). Never ship a
half-port.
