#ifndef WORLD_SNAPSHOT_INTERNAL_H
#define WORLD_SNAPSHOT_INTERNAL_H

/// Src-private seams the monolithic `world_snapshot.cpp` save/load calls
/// into for its per-chunk phase implementations. NOT part of the public
/// `world_snapshot.hpp` surface — creations never see these. Kept out of
/// `include/` so `IRAsset::ChunkPayload` / `LoadedChunk` and the entity
/// manager don't leak into the world-snapshot public API.
///
/// Current occupant: the `RELN` relation chunk (persist P3, #2214) in
/// `world_snapshot_relations.cpp`.

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/entity/ir_entity_types.hpp>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace IRWorld::detail {

/// Serialize the live world's `CHILD_OF` edge set into a self-describing
/// `RELN` chunk (name table at the head + `(relationTypeId, child, parent)`
/// triples). Only edges whose **both** endpoints are in @p servedIds — the
/// exact set of masked ids P2 wrote to `ARCH` + `SNGL` — are emitted, so
/// every restored triple resolves. Triples are sorted by child id (each
/// `CHILD_OF` child has exactly one parent, a total order), so a same-world
/// double-save is byte-identical. The synthetic relation entities are never
/// serialized — `setParent` regenerates them on load.
IRAsset::ChunkPayload makeRelationChunk(
    IREntity::EntityManager &entityManager, const std::unordered_set<IREntity::EntityId> &servedIds
);

/// Replay the `RELN` chunk as the **final** load phase — after every entity,
/// component column, and singleton is in place and the id watermark has
/// advanced (so `setParent`'s synthetic relation entities mint above every
/// restored id). Absent chunk (a pre-P3 file) is a clean no-op. Each triple's
/// relation id is translated disk-name → current `Relation` enum (Rule #2);
/// an unknown/unsupported name or a missing endpoint is skipped with a
/// diagnostic and counted in @p relationsSkipped, not fatal. Every triple is
/// decoded into a staged buffer in a mutation-free pass first, so a
/// structurally malformed chunk (bad name table / count / truncated triple)
/// returns a recoverable error status with **zero** `setParent` replayed
/// (Rule #5 — no partial world mutation on error), never a subset of edges
/// applied. Endpoints resolve through @p singletonAliases (identity for a
/// regular restored entity; the alias for a singleton).
IRAsset::BinaryStatus applyRelationChunk(
    IREntity::EntityManager &entityManager,
    std::span<const IRAsset::LoadedChunk> chunks,
    const std::unordered_map<IREntity::EntityId, IREntity::EntityId> &singletonAliases,
    std::uint64_t &relationsRestored,
    std::uint64_t &relationsSkipped
);

} // namespace IRWorld::detail

#endif /* WORLD_SNAPSHOT_INTERNAL_H */
