# Plan — Epic #667: persist — ECS world snapshot (generic archetype/component/relation serializer)

- **Umbrella:** #667 (refines closed #199)
- **Filed:** children filed 2026-07-04 from the 2026-07-03 architect triage session (human-ratified decomposition)
- **Blocked by:** (none — #663 binary-I/O primitives landed)

## Summary

Generic ECS world snapshot: every archetype, every opted-in component column, every
entity ID, every relation. Serializer lives in `engine/world/` (layering: asset is
below entity/world); consumes the #663 `engine/asset/` primitives (`BinaryWriter`/
`Reader`, `chunk_header`, `name_table`, `json_sidecar`). Format: `IRWS` magic +
uint32 version + chunk table + JSON sidecar, obeying the #662 Save Format
Extensibility Rules. Game-side save-slot metadata wraps this primitive in the game
repo — never here.

## Decomposition (7 chained single-PR children)

| Phase | Issue | Model | Blocked by | Scope |
|---|---|---|---|---|
| P1 | #2212 | sonnet | — | `SaveTrait<C>` + `kSaveVersion` + 169-component audited inventory + compile-time completeness gate (W-1/W-2) |
| P2 | #2213 | opus | #2212 | IRWS container + deterministic archetype walker + singleton chunk + type-erased save registry (W-3/W-6) |
| P3 | #2214 | opus | #2213 | `RELN` relation chunk — CHILD_OF triples + name table (W-5) |
| P4 | #2215 | sonnet | #2214 | round-trip + determinism test suite (W-7/W-8) |
| P5 | #2216 | opus | #2215 | migration registry — `(ComponentId, oldVersion) → reader`, four-case dispatch (W-4) |
| P6 | #2217 | opus | #2216 | GPU-resident state regeneration on load — the canvas-attach pass + `persist_roundtrip` demo (W-10) |
| P7 | #2218 | sonnet | #2217 | Lua bindings (`IRPersist.saveWorld/loadWorld`) + `.json.txt` debug dump (W-9/W-11) |

Chain is strictly linear — every child touches the same `engine/world/` surface.
Each child's canonical plan is its issue's `## Plan` comment (file-with-plan path);
the implementer commits it as `.fleet/plans/issue-<N>.md` in its own impl PR.

## Cross-child decisions locked at planning

- **Restore-exact entity IDs** — never remapped. Components embed raw `EntityId`
  fields; IDs are monotonic/never recycled, so restore inserts saved IDs and
  advances the watermark. Save-side exclusion set mirrors `resetGameplay`'s
  preserve set (singletons, `C_Persistent`, component-backing entities).
- **Relation state is owned solely by P3's chunk.** P2's walker skips
  `kEntityFlagIsRelation` entities and never emits relation ids as columns; load
  replays `setParent` as the final restore phase. Only `CHILD_OF` is materialized
  today (`PARENT_TO`/`SIBLING_OF` verified unimplemented — `setRelation` asserts).
- **`SaveTrait<C_VoxelSetNew>` round-trips into staged mode** (`pendingVoxels_` +
  `pendingBoundsMin_`, `numVoxels_ == 0`); P6 owns the staged→pool canvas-attach
  seed pass. Pool spans and `rotationSourceVoxels_` are never serialized.
- **The live render context is never serialized or reconstructed** — GPU-handle
  components are opt-out by default; loads reuse the live `C_Persistent` canvas
  bundle exactly as `resetGameplay` does (scene_reset #1857 is the proven mirror).
- **Component identity on disk is the stable stringized name** from P1's inventory
  (CMPN name table, Rule #2) — never `typeid` (mangled) or raw ComponentIds
  (session-local).
- `kSaveVersion` is trait-carried `uint32_t` (epic-locked), distinct from the asset
  records' `uint16_t` convention; a simplify-check follow-up for trait-version bump
  detection is flagged in P1.

## Acceptance (inherited from #199, preserved)

Round-trip parity (100+ entities, multiple archetypes, relations, byte-for-byte);
schema versioning with clear mismatch diagnostics; per-component opt-in/out;
deterministic serialization (same world → same bytes); binary + readable debug
dump; clean on `linux-debug` + `macos-debug`; Lua `saveWorld`/`loadWorld`.

## Steward ledger

reconciled-through: 2026-07-04
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2212 | open | — | plan | 2026-07-04 |
| #2213 | open | — | plan | 2026-07-04 |
| #2214 | open | — | plan | 2026-07-04 |
| #2215 | open | — | plan | 2026-07-04 |
| #2216 | open | — | plan | 2026-07-04 |
| #2217 | open | — | plan | 2026-07-04 |
| #2218 | open | — | plan | 2026-07-04 |

### Decisions
D1 (2026-07-03): 7 chained single-PR children, mixed model routing, queue-now — human-ratified in the triage session.
D2 (2026-07-03): restore-exact entity IDs (never remap) — see P2 plan rationale.
D3 (2026-07-03): CHILD_OF-only relation serialization; synthetic relation-entities excluded from the snapshot.
D4 (2026-07-03): C_VoxelSetNew serializes into staged mode; P6 owns the canvas-attach pass; render context preserved, never serialized.

### Events
- 2026-07-04: children #2212–#2218 filed with plans via the adapted file-epic flow; `fleet-validate-stack 667` PASS (7/7); all children human:approved.
