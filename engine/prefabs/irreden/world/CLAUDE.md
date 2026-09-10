# engine/prefabs/irreden/world/ — chunk identity + future chunk-aware prefabs

Domain for chunk-coord addressing, chunk-membership systems, and any other
prefab that has to be in the prefab layer because it touches an
`engine/world/` manager but is opted into per-creation rather than
unconditionally present.

The residency manager itself (`IRWorld::ChunkResidencyManager`) lives at
[`engine/world/include/irreden/world/chunk_residency.hpp`](../../../world/include/irreden/world/chunk_residency.hpp);
this directory holds the prefab-shaped utilities and components that
surround it.

## Files

| File | Role |
|---|---|
| [`chunk_coord.hpp`](chunk_coord.hpp) | `IRPrefab::Chunk::worldToChunk`, `chunkOriginVoxel`, `chunkCenterWorld`, `pack`, `unpack`, and the `ChunkKey` typedef. Pure header, constexpr. |
| [`systems/system_propagate_chunk_membership.hpp`](systems/system_propagate_chunk_membership.hpp) | `PROPAGATE_CHUNK_MEMBERSHIP` (Epic E E5). Detects when an entity's `C_WorldTransform.translation_` crosses a chunk boundary, updates `C_ChunkMembership.chunkCoord_`, and migrates ownership through the creation-supplied `IRWorld::ChunkResidencyManager`. EntityId is preserved across migration. Wire the manager pointer at init via `IRPrefab::Chunk::setMembershipMigrationManager(systemId, &manager)`. Register in UPDATE after `PROPAGATE_TRANSFORM` and before any chunk-aware downstream consumer. Null-manager mode (single-chunk creations, unit tests) still updates `C_ChunkMembership` so consumers stay consistent. |

`C_ChunkMembership` lives under `common/components/` next to the other
position-family components — see
[`../common/CLAUDE.md`](../common/CLAUDE.md) "Key components".

## Design

Full design contract for Epic E (world streaming) is at
[`docs/design/world-streaming.md`](../../../../docs/design/world-streaming.md).
Topic 1 covers chunk identity; Topic 2 covers the residency manager API.

### A "chunk" here is not the only chunk in tree

Three unrelated 32-or-256 groupings share the word, and mixing them up is the
cheapest bug to write in this directory:

| Name | Tiles | Owner |
|---|---|---|
| `IRConstants::kChunkSize` (32³) — **this directory's** | 3D **voxel** space | `ChunkResidencyManager` |
| `kFieldChunkEdge` (32²) — *field chunk* | 2D **cell** space | a `PlacementField` ([`docs/design/chunked-field-placement-kit.md`](../../../../docs/design/chunked-field-placement-kit.md)) |
| `IRRender::kVoxelChunkSize` (256) | nothing spatial — a GPU dispatch/pool bucket | the voxel pool |

The field kit deliberately does **not** hook the residency manager (there is no
resident/evict observer surface to hook, and the manager is creation-constructed
rather than `World`-owned). A creation that wants a field to follow streaming
polls `isResident` / `forEachChunk` itself. If a residency observer surface is
ever added here, that doc's "Relationship to residency chunks" section is the
first thing to revisit.

## What this directory deliberately does NOT own

- The residency manager (`IRWorld::ChunkResidencyManager`). It lives in
  `engine/world/` because it manages a singleton runtime resource (the
  resident-set map + entity manifests + the voxel sub-pool slices).
  Anything sized like a `World` manager belongs in `engine/world/`, not
  in the prefab layer.
- Async upload pipeline, eviction policy, prefetch ring, save/load.
  Those are E2/E3/E6 work — see the design doc for the boundary.
