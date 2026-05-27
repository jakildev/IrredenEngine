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

## What this directory deliberately does NOT own

- The residency manager (`IRWorld::ChunkResidencyManager`). It lives in
  `engine/world/` because it manages a singleton runtime resource (the
  resident-set map + entity manifests + the voxel sub-pool slices).
  Anything sized like a `World` manager belongs in `engine/world/`, not
  in the prefab layer.
- Async upload pipeline, eviction policy, prefetch ring, save/load.
  Those are E2/E3/E6 work — see the design doc for the boundary.
