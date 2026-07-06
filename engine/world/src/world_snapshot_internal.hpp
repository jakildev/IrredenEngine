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
#include <irreden/asset/name_table.hpp>
#include <irreden/entity/ir_entity_types.hpp>

#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

/// One CHILD_OF triple decoded from the `RELN` chunk during the mutation-free
/// decode pass, carried from `decodeRelationChunk` (load Phase 2b) to
/// `applyStagedRelations` (the final apply phase). Endpoints are disk ids; they
/// resolve to live ids only at apply time (a singleton's disk id maps through
/// `singletonAliases`; a regular restored entity keeps its exact id).
struct StagedRelationTriple {
    std::uint64_t relationTypeId_ = 0;
    IREntity::EntityId child_ = 0;
    IREntity::EntityId parent_ = 0;
};

/// The fully-decoded `RELN` chunk: the disk-side relation name table plus every
/// triple's bytes, produced in a pass that touches no live world state.
/// `present_ == false` means the file carried no RELN chunk (a pre-P3 save), so
/// `applyStagedRelations` is a clean no-op.
struct StagedRelations {
    IRAsset::NameTable diskTable_;
    std::vector<StagedRelationTriple> triples_;
    bool present_ = false;
};

/// Decode the `RELN` chunk into @p out in a **mutation-free** pass — read the
/// relation name table, the triple count, and every `(relationTypeId, child,
/// parent)` triple's bytes into the staged buffer, touching no live world
/// state. Runs during load Phase 2b, *before* the id-watermark advance and any
/// Phase-3 entity write, so a structurally malformed chunk (bad name table /
/// count / truncated triple) returns a recoverable error status with the world
/// still pristine — zero entities, zero edges (Rule #5 — no partial world
/// mutation on error), exactly like a malformed ARCH/SNGL column aborts. An
/// absent RELN chunk (a pre-P3 file) is a clean success with
/// `out.present_ == false`. No `reserve` on the disk-controlled triple count:
/// a corrupt oversized count fails on the first read past the chunk body, never
/// a huge preallocation.
IRAsset::BinaryStatus
decodeRelationChunk(std::span<const IRAsset::LoadedChunk> chunks, StagedRelations &out);

/// Replay the pre-decoded @p staged relations as the **final** load phase —
/// after every entity, component column, and singleton is in place and the id
/// watermark has advanced (so `setParent`'s synthetic relation entities mint
/// above every restored id). Every triple was already decoded by
/// `decodeRelationChunk`, so this pass makes no fallible read and is guaranteed
/// to complete — no mid-loop failure can strand a partial edge set past the
/// first live write. Each triple's relation id is translated disk-name →
/// current `Relation` enum (Rule #2); an unknown/unsupported name or a missing
/// endpoint is skipped with a diagnostic and counted in @p relationsSkipped,
/// not fatal. Endpoints resolve through @p singletonAliases (identity for a
/// regular restored entity; the alias for a singleton). A `staged` with
/// `present_ == false` (no RELN chunk) is a no-op.
void applyStagedRelations(
    IREntity::EntityManager &entityManager,
    const StagedRelations &staged,
    const std::unordered_map<IREntity::EntityId, IREntity::EntityId> &singletonAliases,
    std::uint64_t &relationsRestored,
    std::uint64_t &relationsSkipped
);

} // namespace IRWorld::detail

#endif /* WORLD_SNAPSHOT_INTERNAL_H */
