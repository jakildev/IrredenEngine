## Plan: render: unified quadrant-stable depth encoding + per-entity priority bands (revisit #1370, fix Bug A) [B/E #1884]

- **Issue:** #1958
- **Model:** opus
- **Date:** 2026-06-21
- **Blocked by:** #1957 (detached composite must *write* depth before a band on it means anything — Finding 1 of the spike)

### Scope

Land the two halves of the #1884 design that resolve **Bug A** (a detached solid floating *above* the floor clips *behind* it at high zoom):

1. **Quadrant-stable cardinal iso-depth** for the SDF/shapes smooth-yaw path — the depth metric becomes piecewise-constant per 90° bracket instead of drifting continuously with `visualYaw`. This **retires #1370's continuous-yawed smooth-depth** (whose near-±45° "low/back surface wins" artifact shares Bug A's iso-depth-ambiguity root).
2. **Per-entity priority band** in the shared encoding, `enc = priorityBand·BAND + cardinalIsoDepth·4 + face`, with a `depthPriority_` field on `C_EntityCanvas` the composite reads. A foreground band lets a floating solid override the raw `x+y+z` ordering and render fully in front of the floor → Bug A fixed at the root.

Explicitly **out of scope** (own children, both blocked by this one): the per-axis scatter key's quadrant-stabilization + the cardinal-180 `+slot` geometric tiebreak (**Bug B → #1959**); per-trixel priority + demos (**#1960**); rotation perf parity (**#1961**). See the sibling-reconciliation note below for why the per-axis key stays out of this PR.

### Verified current state (macOS/Metal, against the #1950 spike + source)

- Encoding is `encodeDepthWithFace(rawDepth, face) = rawDepth*4 + face` (`ir_iso_common.glsl:123`, `kDepthEncodeShift = 4` `ir_render_types.hpp:187`), normalized to `gl_FragDepth` by `normalizeDistance` over `±65535` (`ir_constants.hpp:47,51`). Depth test is **GL_LESS** → smaller `enc` = nearer.
- The SDF smooth-yaw `baseDepth = roundHalfUp(dvx+dvy+z)` is computed from **`visualYaw`** cos/sin (`c_shapes_to_trixel.glsl:937-942`). That shader already receives **`uniform float rasterYaw`** (cardinal-snapped, multiple of π/2) and **`uniform int smoothYawEnabled`** (`:33-43`), and the cardinal helpers `rasterYawCardinalIndex()` / `cardinalYawCosSin()` already exist in `ir_iso_common.glsl:401-416`. So the quadrant-stable swap needs **no new plumbing** — it reuses inputs already present.
- The **GRID voxel gather** already uses un-yawed `pos3DtoDistance` / model-axis `isoDepthAlongAxis` (`c_voxel_to_trixel_stage_1.glsl:285-289,322-324`) — i.e. already cardinal-stable, not `visualYaw`. So "the voxel path" needs no quadrant-stable change here; the only `visualYaw`-driven depth metrics are the SDF smooth path (this PR) and the per-axis scatter key (#1959).
- The detached composite folds `compositeDistanceOffset = worldDepth · effSub · kDepthEncodeShift` (`system_entity_canvas_to_framebuffer.hpp:228`) and rescales the texture rawDist by `effSub/cubeSub`. **It writes color only — not depth — on Metal** (spike Finding 1); #1957 makes it write depth, which is why this issue is blocked on #1957.
- The #1910 probe decodes `rawDist_ = windowDepth·(kMax−kMin)+kMin` (`depth_probe.hpp:92-98`) — the full encoded int. After banding it will include `priorityBand·BAND`, so its log line needs a band/iso/face split to stay interpretable.

Repro confirmed in the spike (`docs/design/depth-unification-1884-investigation.md`, on `claude/1884-depth-unification`): `--only canary,floor` breaks (clip line), `--only canary,maingrid` is clean.

### Approach (single, committed)

**A. Quadrant-stable SDF depth** (`c_shapes_to_trixel.glsl` + `metal/c_shapes_to_trixel.metal`):
In the smooth-yaw `baseDepth` branch only, derive the depth metric's cos/sin from the **cardinal bracket** (`cardinalYawCosSin(rasterYawCardinalIndex(rasterYaw))`) instead of `yawC/yawS` (= cos/sin `visualYaw`). Keep on-screen **pixel placement** at `visualYaw` (unchanged) — only the stored depth becomes quadrant-stable. Net effect: within a 90° bracket the floor's depth no longer drifts toward/under the geometry above it, killing #1370's near-±45° crossing. Cardinal frames are unaffected (cardinal cos/sin == visualYaw cos/sin at the bracket).

**B. Banded encoding** (`ir_render_types.hpp`):
Add `constexpr int kDepthPriorityBand` (= BAND) and `kDepthMaxPriorityBand` next to `kDepthEncodeShift`. Band 0 = world (floor/GRID/SDF/particles — encoding byte-identical, no shader edits). Foreground priority must make `enc` **smaller** (GL_LESS), so the composite **subtracts** `depthPriority_·kDepthPriorityBand`. Size BAND from the signed range: `kDepthMaxPriorityBand · BAND ≤ kTrixelDistanceMaxDistance` **and** `2·(4·maxSubdividedIso) + 3 < BAND` (bands must not overlap after the ×4 + face spread). Architect's target `BAND ≈ 21845 (= 65535/3)`. **`static_assert` both inequalities** in `ir_render_types.hpp`.

**C. Per-entity field + composite write** (`component_entity_canvas.hpp`, `component_entity_canvas_lua.hpp`, `system_entity_canvas_to_framebuffer.hpp`):
Add `int depthPriority_ = 0;` to `C_EntityCanvas` (+ constructor arg, default 0; mirror in the Lua component). The composite already iterates `C_EntityCanvas`, so it reads the field directly (no foreign per-tick `getComponent`) and folds `− depthPriority_ · kDepthPriorityBand` into `compositeDistanceOffset` (only when `!screenLocked_`, alongside the existing `worldDepth·effSub·4`). Default 0 → byte-identical to today.

**D. Probe interpretability** (`depth_probe.hpp`): extend the `[depth-probe]` log to also print `band = enc / BAND`, `iso = (enc mod BAND) / 4`, `face = enc mod 4` so a debugger reads the banded ordering directly.

**E. Demo opt-in**: the canary/showcase floating solids in `canvas_stress` set `depthPriority_` to a foreground band so they render fully above the floor (the Bug-A acceptance case). No change to GRID/world demos.

### Cross-system audit (shared depth encoding)

| Consumer (GL → Metal twin) | Role | #1958 change |
|---|---|---|
| `ir_iso_common.glsl:123` → `metal/ir_iso_common.metal:113` `encodeDepthWithFace` | shared encode fn | **unchanged** (band added by the one caller that needs it; default band 0) |
| `c_shapes_to_trixel.glsl:937-942,1016` → `metal:1099` SDF smooth baseDepth | floor/shapes | **visualYaw → cardinal-bracket depth metric** (A) |
| `c_voxel_to_trixel_stage_1.glsl:289,324` / `stage_2:245,275` → metal twins | GRID/detached voxel gather | **unchanged** (already cardinal/model-axis = quadrant-stable; band 0) |
| `c_render_stateless_particles…:142`, `c_render_gpu_particles…:64` → metal | particles | **unchanged** (band 0) |
| `c_resolve_per_axis_screen_depth.glsl:127`, `c_resolve_world_placed_depth.glsl:111` → metal | per-axis / world-placed resolve | **unchanged** (band 0); per-axis cardinal-stabilization is #1959 |
| `scatterCompositeDepthKey` (`ir_iso_common.glsl:575`, `v_peraxis_scatter.glsl:267-269`) → metal | per-axis scatter key | **deferred to #1959** — see reconciliation note |
| `system_entity_canvas_to_framebuffer.hpp:228` composite | detached composite | **+ `− depthPriority_·BAND`** (requires #1957's depth-write) |
| `f_trixel_to_framebuffer.glsl:43,66,95` `normalizeDistance` → metal | final FB depth write | **unchanged** (band is just part of `enc`) |
| `depth_probe.hpp:92-98` rawDist decode | #1910 probe | **add band/iso/face split to the log** (D) |
| `ir_render_types.hpp:187` `kDepthEncodeShift`; `ir_constants.hpp:47,51` range | constants | **add `kDepthPriorityBand` + `kDepthMaxPriorityBand` + static_assert** (B) |

### Sibling + in-flight reconciliation

- **#1957** (blocker) is the foundation: the composite must write depth before a band on it is observable. Build #1958 only against a master where #1957 has landed — else the band write is a Finding-1 no-op.
- **Per-axis scatter key stays out of #1958.** The architect's scope lists "the per-axis / voxel paths" under the quadrant-stable change, but the per-axis key (`scatterCompositeDepthKey`) is entangled with **Bug B** (its `+slot` tie at cardinal-180), which is **#1959's** explicit charter, and #1959 is `Blocked by: #1958`. Touching that key here would (a) collide line-for-line with #1959 and (b) drag Bug B's regression surface (#1883's just-stabilized per-axis path) into a PR whose validation is SDF/gather-focused. So #1958 stabilizes the SDF smooth path; #1959 (built on #1958) does the per-axis cardinal-stabilization **and** the geometric tiebreak together. *If the architect intended the per-axis metric to move in #1958, flag on review and I'll re-fold it.*
- No open PR touches the depth encoding except #1950 (the spike doc, docs-only, merging).

### Acceptance criteria

- **Bug A fixed:** `IRCanvasStress --only canary,floor --no-spin --no-auto-rotate --auto-screenshot --depth-probe 320,337` (and `313,330`) — the canary cube renders fully above the floor; the probe shows the cube's foreground band winning at the previously-clipped bottom pixels.
- **#1370 non-regression:** the rotated-solidity harness (#1882) + depth-probe (#1910) confirm the near-±45° "low/back surface wins" SDF crossing does **not** return through a full bracket sweep (incl. near-cardinal samples at the ±45° boundary).
- **Cardinal frames byte-identical** to master on both backends: `shape_debug` 28-shot render-verify + `canvas_stress` cardinal shots unchanged (band 0 = no-op for world geometry).
- **Both backends build + run** (GL via `linux-debug`/CI, Metal via `macos-debug`); visual parity via `attach-screenshots` + `render-debug-loop`.
- **`static_assert`s pass** for band headroom (`2·4·maxSubdividedIso + 3 < kDepthPriorityBand`, `kDepthMaxPriorityBand·kDepthPriorityBand ≤ kTrixelDistanceMaxDistance`).

### Gotchas

- **Band headroom × subdivisions (the trap).** The encoded iso-depth is **subdivided** (`worldDepth·effSub·4`, and texture rawDist `·cubeSub·4`), so it scales with zoom up to the #1570-D2 cap. Sizing BAND off un-subdivided world iso-depth **overflows at high zoom and silently wraps depth**. Measure the max subdivided iso-depth for the priority-using demos (`canvas_stress`, `shape_debug`) and `static_assert` it fits a band. If it can't fit ≥2 bands at the demos' effSub cap, **escalate `fleet:design-blocked`** — do not silently clamp.
- **Sign.** GL_LESS + `normalizeDistance` ⇒ nearer = smaller `enc`. Foreground priority **subtracts** `priorityBand·BAND`. Wrong sign sinks high-priority solids behind everything.
- **Bracket-boundary stability.** The cardinal metric is piecewise-constant per quadrant by design; verify the ±45° handoff doesn't introduce a 1-frame depth pop using #1882's near-cardinal sampling (this is exactly what the spike's "quadrant-stable" property guarantees — confirm it empirically).
- **Metal parity.** Every GLSL depth-metric/encoding edit has a `.metal` twin (audit table) — port identically. Metal sources are **not** in clang-format (`QUALITY_FILES`); never whole-file reformat them.
- **Lua component sync.** `component_entity_canvas_lua.hpp` mirrors `C_EntityCanvas` for Lua — add `depthPriority_` there too or Lua-spawned canvases drop the field.
- **Probe contract.** After banding, `rawDist_` includes `band·BAND`; without the (D) split a future Finding-1-style debug mis-reads it.

### One task or subtasks

**One task** (#1958). It is already a carve-off child of the #1884 sub-epic; the per-axis (#1959), per-trixel (#1960), and perf-parity (#1961) pieces are separate children, each `Blocked by` this one.

— worker (opus)

