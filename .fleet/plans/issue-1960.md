## Plan (REFRESHED 2026-06-24): render: per-trixel priority + depth-priority demos (entity vs trixel clip/interpenetrate) [D/E #1884]

- **Issue:** #1960
- **Model:** opus
- **Date:** 2026-06-24 (supersedes the 2026-06-22 plan, which was wrong-by-construction)
- **Blocked by:** #1958 — **SATISFIED.** PR #1974 merged 2026-06-24T03:47:41Z. #1960 is now a normal-tier, Metal-verifiable pick.

### Why this plan was re-derived

The prior `## Plan` (2026-06-22) packed per-trixel priority into the per-voxel
`entityIds` channel and folded `− priorityBand·BAND` into `enc` at the **voxel-gather
and per-axis-scatter encode sites**, extending #1958's *additive* priority band
`enc = priorityBand·BAND + cardinalIsoDepth·4 + face`. **That foundation no longer
exists.** During #1958's review the architect escalated the additive band
`fleet:design-blocked` (no fixed additive band can dominate unbounded world placement —
the radius-200 GRID orbit exhausts it) and **replaced it with a disjoint near-plane
partition** applied at the single depth-finalization chokepoint. The old §A/§B are
therefore refuted. This plan re-derives #1960 against the *shipped* model.
(Two prior opus passes — 2026-06-23 22:52 and 2026-06-24 01:19 — flagged this and
teed up "re-plan once PR #1974 merges"; that trigger is now met.)

### Verified shipped state (#1958, on master after PR #1974 — macOS/Metal + source)

- **Per-ENTITY priority is already DONE.** #1958 added `int depthPriority_ = 0` to
  `C_EntityCanvas` (+ Lua twin) and the composite sets, **per detached-entity draw**,
  `fd.depthPriorityMode_ = depthPriority_ != 0 ? 1 : 0` and
  `fd.distanceOffset_ = kDepthForegroundBandCenter`
  (`system_entity_canvas_to_framebuffer.hpp:107,244,262,282,286`; each canvas is its
  own `drawElements`, :336).
- **The partition is per-DRAW, at finalization.** `f_trixel_to_framebuffer.glsl:83-104`:
  `enc = round(rawDist·depthScale) + distanceOffset`, then
  `if (depthPriorityMode != 0) enc = clamp(enc, kMin, foregroundCeil)` (pin INTO the
  reserved near band) `else enc = max(enc, foregroundCeil + 1)` (clamp world OUT).
  `foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth`.
- **Reserved band constants** (`ir_render_types.hpp:219-244`):
  `kDepthForegroundBandWidth = 16384`; `kDepthForegroundCeil = kMin + 16384`;
  `kDepthForegroundBandCenter = kMin + 8192`. Partition-layout `static_assert`s only
  (`kDepthForegroundCeil < 0`; band ⊂ encodable range) — the old world-extent assert is gone.
- **The per-texel transport channel ALREADY reaches finalization.**
  `f_trixel_to_framebuffer.glsl:18` binds `usampler2D triangleEntityIds` (binding 2) and
  reads it for hover (:119). Written by `c_voxel_to_trixel_stage_2.glsl:83,95`
  (`rg32ui triangleCanvasEntityIds`, `imageStore(..., uvec4(entityIds[voxelIndex], 0,0))`).
- **`EntityId = std::uint64_t`** (`ir_entity_types.hpp:17`) → the `rg32ui` channel
  carries a full 64-bit id across BOTH words. **The prior plan's "high 32 bits are spare"
  premise is false.** The per-trixel-priority *carrier* is an open decision (below).
- **The #1959 per-axis collision largely dissolves.** The shipped partition lives at
  finalization, DOWNSTREAM of `scatterCompositeDepthKey`. Per-trixel tier *math* is at
  finalization, so #1960 need not touch the scatter key #1959 edits (only the per-axis
  entityId *write* must carry the priority bits, if detached units use the per-axis path).
- **Demo host:** `canvas_stress` already has the #1958 canary foreground-priority scaffold.

### Approach (committed against the partition; one carrier decision flagged for plan-review)

Architect generalization (design doc §Resolution, "Per-trixel generalization (#1960)"):
*"The partition generalizes to N disjoint sub-ranges; `priority = max(entity, trixel)`
selects the tier. #1958 ships the two-tier split only; per-trixel tiers are D (#1960)."*

**A. Generalize the partition to N tiers** (`ir_render_types.hpp` + finalization shader).
Subdivide the reserved near band `[kMin, kDepthForegroundCeil]` into N disjoint
sub-ranges (tier 0 = world, OUT of the band; tiers 1..N-1 = foreground, each a sub-slice
with its own `[tierLo(t), tierHi(t)]` + `tierCenter(t)`, MORE-negative = higher priority).
Replace the two-way `if (depthPriorityMode != 0)` at `f_trixel_to_framebuffer.glsl:99-104`
with per-tier clamp; add `constexpr int kDepthForegroundTierCount` + per-tier layout
helpers + partition-layout `static_assert`s (each tier wide enough for a unit's
subdivided local iso-depth spread; tiers disjoint; all ⊂ `[kMin, kDepthForegroundCeil]`).
**Sizing is the load-bearing plan-review item** — see Open decisions.

**B. Per-fragment tier selection at finalization** (`f_trixel_to_framebuffer.glsl` + metal twin).
Unpack `perTrixelPriority` from the `triangleEntityIds` sample; compute
`tier = max(depthPriorityMode /*per-draw entity tier*/, perTrixelPriority)`; pin
`enc = clamp(round(rawDist·depthScale) + distanceOffset', tierLo(tier), tierHi(tier))`
for `tier > 0`, else `enc = max(enc, foregroundCeil + 1)`. (The per-draw `distanceOffset`
already re-centers a priority entity's rawDist into the band; per-trixel re-centering uses
`tierCenter(tier)`.) Default everywhere 0 ⇒ byte-identical to #1958 master.

**C. Per-trixel priority source + CARRIER** — *the plan-review decision.*
Source = authored per-voxel `priority_` (small uint, 0..`kDepthForegroundTierCount-1`),
default 0. It must travel voxel → trixel raster → the entity's trixel canvas → finalization.
The transport texture (`triangleEntityIds`) already arrives at finalization, but its two
words hold a 64-bit id. Carrier options (recommend the architect pick):
  1. **Steal high bits of the 64-bit id** in the `entityIds` channel (entity ids are
     allocated small in practice; reserve the top K bits for priority). Cheapest — no new
     attachment — but makes the masking audit load-bearing (every id reader must mask).
  2. **Spare high bits of the R32I `triangleDistances`** (iso-depth uses ~±65535 ≈ 17 of
     32 bits) — but rawDist is signed and feeds the partition arithmetic; bit-packing a
     signed depth is error-prone.
  3. **A dedicated per-texel priority attachment** on the trixel canvas (R8UI) — cleanest
     semantically, costs one render-target + a binding (budget is tight; check `ir_render_types.hpp:635-722`).
Recommendation: **(1)** if an id-bit reservation is acceptable (smallest diff, transport
already wired); fall back to **(3)** if the architect wants id-space untouched.

**D. Entity-id masking audit (the trap).** Whichever carrier rides the id channel, every
`triangleEntityIds`/`HoveredEntityId` reader must mask off the priority bits or a non-zero
priority corrupts a picked id. Surface is SMALL — confirmed only `f_trixel_to_framebuffer.glsl`
+ `metal/trixel_to_framebuffer.metal` read `triangleEntityIds` (the hover write to
`HoveredEntityIdBuffer`, :119-121). Mask there; default priority 0 keeps it a no-op.

**E. Demo 1 — clip-vs-swap** (`canvas_stress --demo priority-swap` + `kShots[]`).
N detached units orbiting while camera yaw-ramps so depths cross; **per-entity priority
only** (uses #1958's `depthPriority_` — buildable today). Acceptance: units swap
in-front/behind by true depth, NO flicker within a 90° bracket (proves #1958 quadrant
stability under the priority encode). Commit render-verify refs at bracket midpoints + ±45°.

**F. Demo 2 — intentional interpenetration** (`--demo priority-interpenetrate`).
Two units overlapping in world space: (a) both per-entity priority → stay solid, nearer
occludes (no clip); (b) one unit's overlap region carries high **per-trixel** priority →
those trixels render in front regardless of depth (deliberate "inside each other"). (a)/(b)
visibly differ; both get committed render-verify baselines. Animation frame-deterministic.

**G. Probe interpretability** (`depth_probe.hpp`). #1958 split the `[depth-probe]` log
into band/iso/face; extend to print the resolved `tier` so Demo 2's per-trixel `max` is
ground-truthable headless at an interpenetration pixel.

### Affected files

- `engine/render/include/irreden/render/ir_render_types.hpp` — N-tier band layout
  constants + helpers + partition-layout `static_assert`s; carrier bit-layout doc.
- `engine/render/src/shaders/f_trixel_to_framebuffer.glsl` (+ `metal/trixel_to_framebuffer.metal`)
  — per-fragment `tier = max(entity, trixel)`, per-tier clamp; mask id bits in the hover read.
- per-voxel authoring + `c_voxel_to_trixel_stage_2.glsl` (+ metal twin) — author `priority_`,
  pack into the chosen carrier at the entityId/canvas write.
- per-axis scatter entityId write (`v_/f_peraxis_scatter` + metal) — carry priority bits IF
  detached units render through the per-axis path (no scatter-KEY edit → no #1959 collision).
- `engine/prefabs/irreden/render/depth_probe.hpp` — tier split in the log (G).
- voxel-set authoring surface + Lua twin (if `priority_` is Lua-exposed).
- `creations/demos/canvas_stress/main.cpp` (+ `kShots[]`) — the two demos.
- `creations/demos/canvas_stress/` render-verify references — new committed baselines.

### Acceptance criteria

- Per-entity ("don't clip through walls") AND per-trixel ("animate inside each other")
  both demonstrated; committed render-verify baselines for both demos (they ARE the harness).
- Default `priority_ = 0` everywhere ⇒ cardinal + non-priority frames **byte-identical** to
  #1958 master: `shape_debug` 28-shot + `canvas_stress` cardinal shots unchanged, both backends.
- `max(perEntity, perTrixel)` verified at a probed interpenetration pixel via the tier split (G).
- Both backends build + run (GL `linux-debug`, Metal `macos-debug`); `attach-screenshots`
  + `render-debug-loop` evidence on the PR.
- N-tier partition-layout `static_assert`s pass (tiers disjoint, sized for subdivided local
  iso-depth, ⊂ reserved band).

### Open decisions for plan-review (architect sign-off before execution)

1. **Per-trixel carrier (C):** id-bit reservation (option 1) vs dedicated R8UI attachment
   (option 3). Recommend option 1; option 3 if id space must stay clean.
2. **Tier count + band layout (A):** how many foreground tiers, and how to subdivide the
   16384-code reserved band so each tier keeps enough headroom for a unit's subdivided
   local iso-depth spread (effSub≤16). Demos need ≥1 trixel tier above the entity tier;
   N=3 total (world / entity-fg / trixel-override) likely suffices — confirm.

### Sibling + in-flight reconciliation

- **#1958 (blocker)** MERGED — reuse `kDepthForeground*` constants, the partition chokepoint,
  `depthPriority_`/`depthPriorityMode_`, and the probe split verbatim; inherit its asserts.
- **#1959 (sibling)** edits `scatterCompositeDepthKey`. #1960's tier math is at finalization,
  downstream — **no scatter-key edit**, so the prior plan's line-for-line collision is gone.
  Only the per-axis entityId *write* may need the priority bits; coordinate lightly, no shared
  key region.
- **#1961 (perf parity)** independent; no overlap.

### One task or subtasks

**One task** (#1960) — the per-trixel feature + its two validation demos travel together
(the demos ARE the acceptance). Already a carve-off child of #1884.

— worker (opus), refreshed against shipped #1958
