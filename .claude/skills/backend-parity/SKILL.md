---
name: backend-parity
description: >-
  Keep the OpenGL (Linux/Windows) and Metal (macOS) render backends
  functionally in sync. Scans for graphics-backend deviations — shaders,
  backend C++ sources, render-impl wiring — and ports the missing side so
  both backends stay at feature parity. Primary direction is OpenGL → Metal
  (most new work lands as GLSL first), but it also handles Metal → OpenGL
  when Metal is the leading side. Builds and tests the lagging backend
  before opening a PR. Use when the user says "metal parity", "port to
  metal", "sync the backends", "audit render parity", or after a render
  PR lands that touched only one backend.
---

# backend-parity

Running it requires a build host that matches the **lagging** backend:

- Porting GLSL/OpenGL → MSL/Metal? You must be on **macOS** with the
  `macos-debug` preset configured.
- Porting MSL/Metal → GLSL/OpenGL? You must be on **Linux/WSL** with
  the `linux-debug` preset, or **Windows** with the `windows-debug`
  preset.

You cannot port in a direction you can't build, because the skill's
core rule is "no port lands without a clean build + smoke run on the
target backend". Cross-compilation is not a substitute.

## Model expectations

This is **Opus** work by default. The render backends and shader
translation touch core engine invariants — GPU buffer lifetime, compute
dispatch sizing, iso-projection math, uniform/binding layout. A Sonnet
agent that picks up a backend-parity task should escalate unless the
gap is purely mechanical (e.g. a trailing debug-overlay vertex shader
with no state changes). See the "Model split" section in
[`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md).

## Preconditions

1. **You are on the right host for the lagging backend.** Check with
   `uname -s`:
   - `Darwin` → you can port GLSL → MSL.
   - `Linux` → you can port MSL → GLSL.
   - Anything else (MinGW/MSYS) → you can port MSL → GLSL via the
     Windows OpenGL path.
2. **The repo is configured** for the lagging preset. If not, stop and
   ask the user before reconfiguring (agents never run `cmake --preset`
   unprompted — see top-level `CLAUDE.md` "What not to automate").
3. **`gh` is authenticated** (`gh auth status`). The skill ends in a
   PR.
4. **You are not on `master`.** Branch first, per `commit-and-push`
   preconditions.
5. **Working tree is clean.** If there are unrelated uncommitted
   changes, stop and warn — a parity PR should be pure parity work.

## Flow

### 1. Resolve the scope

Ask the user (or accept from the trigger phrase) which of these
modes you're running:

- **Full audit.** "Find all render drift from scratch." Expensive but
  thorough — walk the whole render tree.
- **PR-scoped.** "Mirror PR <N> to the other backend." Cheap — only
  look at files the PR touched.
- **Commit-range-scoped.** "Mirror everything since `<ref>`." Moderate
  — walks git log between refs.
- **Single-feature.** "Port `c_shapes_to_trixel` to metal." Narrowest
  — skip audit, go straight to step 3 for the one file.

Record the mode at the top of the PR body later.

### 2. Enumerate the deviation set

OpenGL sources live under `engine/render/src/opengl/` and Metal counterparts
under `engine/render/src/metal/` with matching names. Headers mirror under
`engine/render/include/irreden/render/opengl/` and `.../metal/`. Shaders:
`engine/render/src/shaders/*.glsl` (GLSL, prefixed per CLAUDE-BASELINE §Naming)
↔ `engine/render/src/shaders/metal/*.metal` (MSL). MSL bundles a vertex + fragment
pair into one `.metal` file — when porting GLSL v/f stages, produce **one** `.metal`
file (see `framebuffer_to_screen.metal` for the canonical pattern).

**Audit command** (to list missing counterparts when running on macOS
porting GLSL → MSL):

```bash
cd engine/render/src/shaders
for glsl in *.glsl; do
    base="${glsl%.glsl}"
    # strip c_/v_/f_/g_ prefix for the metal lookup
    key="${base#c_}"; key="${key#v_}"; key="${key#f_}"; key="${key#g_}"
    if ! ls metal/${key}.metal >/dev/null 2>&1; then
        echo "MISSING METAL: $glsl  →  metal/${key}.metal"
    fi
done
```

For the Metal → OpenGL direction, invert: for each `metal/*.metal`,
check whether any matching `<stage>_<key>.glsl` exists under the
flat `src/shaders/` dir. A single `.metal` file with both vertex
and fragment stages should produce **both** `v_<key>.glsl` and
`f_<key>.glsl` on the GLSL side.

**C++ backend audit:**

Use the Glob tool to list backend source files and compare stems:
- `engine/render/src/opengl/opengl_*.cpp`
- `engine/render/src/metal/metal_*.cpp`

Strip the `opengl_`/`metal_` prefix from each result and compare — any stem present on one side but absent on the other is a potential parity gap.

Read the diff carefully — Metal has runtime-bridge files
(`metal_runtime.cpp`, `metal_cocoa_bridge.mm`, `metal_cpp_impl.cpp`)
that have **no** OpenGL counterpart by design; those are not parity
gaps. Real gaps look like "one side has a function that renders
shape X, the other side doesn't", which you find by reading
`opengl_render_impl.cpp` vs `metal_render_impl.cpp` rather than
diffing file names.

### 3. For each deviation: read the leading side end-to-end

For each missing file (or missing function), read the leading-side
implementation in full. Include:

- The shader or C++ source itself.
- The CPU-side feeder struct / uniform struct. For a compute shader,
  that's usually in `engine/render/include/irreden/render/components/`
  or a `*_types.hpp` under `engine/render/include/irreden/render/`.
- The shared render interface in
  `engine/render/include/irreden/render/renderer_impl.hpp` and
  `render_manager.hpp` to understand how both backends expose the same
  operation.
- `engine/render/CLAUDE.md` for the pipeline overview.
- If the feature was added recently, the PR that introduced it —
  `gh pr list --state merged --search "<feature name>"` then
  `gh pr view <N> --json body,commits` for motivation.

Do not skim. A GLSL→MSL port that misses the uniform layout will build
but render garbage, and that's the category of bug this skill exists
to prevent.

### 4. Write the lagging-side port

General rules:

- **One PR per logical feature.** Porting `c_shapes_to_trixel` is one
  PR. Porting a C++ backend function is one PR. Don't bundle
  unrelated parity fixes into one monster PR — reviewer-agents can't
  sign off on a sprawling mixed-scope parity diff.
- **Match naming conventions** of the target backend. GLSL uses
  `c_/v_/f_/g_` prefixes; MSL uses a bare filename with the stage
  declared inside.
- **Preserve uniform layouts bit-exact.** The engine's CPU-side feeder
  struct is backend-agnostic; whatever packing order the GLSL
  `layout(std140)` block uses, the MSL `struct` must match byte-for-
  byte (respecting `[[buffer(N)]]` binding indices). Mismatches here
  are invisible at compile time and produce garbage at run time.
- **Match binding points.** OpenGL uses `layout(binding = N)`; Metal
  uses `[[buffer(N)]]`, `[[texture(N)]]`, `[[sampler(N)]]`. Keep the
  same N — the CPU-side code assumes a single slot number per resource
  across both backends.
- **Compute dispatch sizes.** GLSL `layout(local_size_x = X, ...)`
  becomes MSL `[[threads_per_threadgroup]]` on the dispatch side plus
  a matching `threadgroup_size` in `metal_render_impl.cpp`. The engine
  uses `voxelDispatchGridForCount()` in `engine/math/` to pick grid
  sizes — reuse it; don't hardcode. This is called out in
  `engine/render/CLAUDE.md`.
- **Iso-projection math.** Both backends must call the same
  iso-projection helpers; the canonical forms live in
  `ir_iso_common.glsl` (GLSL) and should be mirrored in MSL as an
  equivalent header under `shaders/metal/` if more than two shaders
  need them. See [`.claude/rules/cpp-math.md`](../../rules/cpp-math.md) for IRMath substitutions.
- **Trailing sanity.** Every ported shader needs at least one
  end-to-end manual test: build the lagging preset, run
  `IRShapeDebug` (or whatever demo exercises the pipeline the shader
  belongs to), and compare the rendered output against the leading
  backend side-by-side. Screenshot diffs if the demo has a fixed
  camera.

**C++ backend functions.** Port `opengl_render_impl.cpp` functions to
`metal_render_impl.cpp` (or vice-versa) following the same public
signature declared on the `IRenderImpl` interface in
`renderer_impl.hpp`. The interface is the contract — if the leading
side added a new virtual method, the lagging side **must** implement
it (or the whole thing won't link).

If the leading side took a shortcut with a backend-specific detail
(e.g. used a glCopyImageSubData with no direct Metal equivalent),
either:

1. Replicate the **effect** via the nearest Metal equivalent
   (blit encoder, texture copy), or
2. Stop and flag it as an issue that needs a broader abstraction
   change. **Do not** paper over it with a stub or a TODO comment
   that silently renders wrong.

### 5. Build the lagging backend

After every port, the skill must **build the lagging preset clean**.
Use `fleet-build` — it avoids the `$(nproc)` / `$(sysctl)` command-substitution
gate and auto-detects the worktree's build tree. See [`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md)
for the host/preset mapping (and the Windows-native PATH-fix wrapper):

```bash
fleet-build --target IRShapeDebug
```

**Watch for the build-hygiene canary** — a "Built target" line without
any "Building CXX object" / "Building C object" / "Building Metal
Library" line proves nothing compiled. If the build cache is cold on
your files, `touch` them and rerun.

Any compile or link error must be fixed **before** committing, not
worked around with a `#ifdef METAL` stub. If you cannot resolve it,
stop and escalate (`/model opus` if running as Sonnet) rather than
submitting a half-port.

### 6. Smoke-run the target executable

A clean build is necessary but not sufficient. Also launch the
demo that exercises the feature and confirm it doesn't crash on the
first frame:

```bash
fleet-run --timeout 15 IRShapeDebug
```

Let it render at least a few frames. If it's a deterministic scene,
eyeball-compare against the leading backend. If something looks
visibly wrong (missing geometry, wrong colors, flickering), **treat
that as a port failure** and fix it or escalate — don't ship.

The goal is functional parity, not "it builds".

### 7. Commit + open PR via `commit-and-push`

Hand off to the `commit-and-push` skill. The parity PR body template:

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
- <Anything that's *not* exact parity and why — e.g. "Metal has no
  direct equivalent of glCopyImageSubData, used a blit encoder which
  is functionally equivalent for our usage.">
```

Title format: `metal: <what you ported>` or `opengl: <what you ported>`
depending on the lagging side.

After the PR is open, `commit-and-push` hands control back. The
reviewer agent (`review-pr` skill) takes over. Wait for a user cue
before starting the next parity gap — do not invoke `start-next-task`
proactively.

## Cross-backend translation cheatsheet

Quick reference for the mechanical bits of porting shader code. Full
rules live in `engine/render/CLAUDE.md` — this table is just the
shortcuts that come up most.

| GLSL                                     | MSL                                                        |
|------------------------------------------|------------------------------------------------------------|
| `layout(binding = N) uniform`            | `constant T& x [[buffer(N)]]`                              |
| `layout(binding = N) buffer`             | `device T* x [[buffer(N)]]` (read/write) / `constant` RO   |
| `layout(binding = N) uniform sampler2D`  | `texture2d<T> x [[texture(N)]], sampler s [[sampler(N)]]`  |
| `layout(local_size_x = X, _y = Y, _z = Z) in;` | `kernel void foo(..., uint3 gid [[thread_position_in_grid]])` + dispatch specifies threads per threadgroup |
| `gl_GlobalInvocationID`                  | `thread_position_in_grid` (via `[[thread_position_in_grid]]`) |
| `gl_LocalInvocationID`                   | `thread_position_in_threadgroup`                            |
| `imageStore(tex, pos, color)`            | `tex.write(color, pos)` (on `texture2d<T, access::write>`) |
| `imageLoad(tex, pos)`                    | `tex.read(pos)`                                             |
| `memoryBarrierShared(); barrier();`      | `threadgroup_barrier(mem_flags::mem_threadgroup)`           |
| `memoryBarrier(); barrier();`            | `threadgroup_barrier(mem_flags::mem_device)`                |
| `atomicAdd(buf, v)`                      | `atomic_fetch_add_explicit(&buf, v, memory_order_relaxed)`  |
| `vec4 / vec3 / vec2 / float`             | `float4 / float3 / float2 / float` (same semantics)         |
| `mat4 / mat3`                            | `float4x4 / float3x3`                                       |
| `#define FOO 1`                          | Same — MSL accepts C preprocessor                           |
| `#include "ir_iso_common.glsl"`          | `#include "ir_iso_common.metal"` (create one if it doesn't exist) |

Going the other direction, invert each row. The only non-obvious
reversal is **dispatch sizing**: in Metal the threads-per-threadgroup
is set at the dispatch call site (`metal_render_impl.cpp`), not inside
the shader. When porting MSL→GLSL you must hunt down the dispatch
call, read its threadgroup size, and encode it as
`layout(local_size_x = ..., _y = ..., _z = ...)` in the GLSL.

## Anti-patterns

- Shipping a port without building the lagging preset. The whole
  point of the skill is that you *verified* it builds and runs.
- Shipping a port without smoke-running the demo. Build-clean ≠
  parity.
- `#ifdef METAL` / `#ifdef OPENGL` stubs that silently return
  wrong values. Either port properly or stop and flag.
- Bundling multiple unrelated parity fixes into one PR. One logical
  feature per PR; the reviewer can't usefully sign off on "ports
  three shaders and rewrites the framebuffer path".
- Changing the leading backend mid-port. If the leading side has a
  bug, file it separately and then port the correct behavior.
  This skill is parity, not drive-by cleanup.
- Hardcoding dispatch sizes instead of reusing
  `voxelDispatchGridForCount()` or the equivalent math helper.
- Inventing new uniform layouts "because MSL is different". Match
  the CPU-side feeder struct exactly; if the feeder struct is wrong
  for Metal, that's a separate refactor in a separate PR.
- Running this skill on a host whose backend matches the
  *leading* side. You cannot verify the port — stop.

## Re-port after review feedback

If the reviewer asks for changes:

1. Stay on the same branch (don't `start-next-task` yet).
2. Address each review comment with a new commit.
3. Rebuild and re-smoke-run — same as step 5/6 above.
4. Push and reply on the PR.
5. Only after merge, run `start-next-task` and pick the next gap.

## Escalation

A Sonnet agent running this skill should escalate to Opus when:

- The port requires touching `engine/math/` (iso-projection,
  dispatch-grid helpers). That's core engine math — Opus territory.
- The leading-side implementation relies on a backend primitive that
  has no obvious Metal (or OpenGL) equivalent and you're not sure
  what the right replacement is.
- Smoke-run reveals a visual difference you can't explain from the
  diff.
- The build break looks like it touches lifetime / ownership of a
  GPU resource (buffer creation, destruction, persistent mapping).

Escalate by stopping, updating `TASKS.md` to re-tag the gap as
`[opus]`, and posting a short note on why in the task body. Don't
ship a half-port.
