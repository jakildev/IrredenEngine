#include "world_snapshot_internal.hpp"

#include <irreden/ir_profile.hpp>

#include <irreden/asset/name_table.hpp>

#include <irreden/entity/archetype_node.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/ir_entity.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

// Persist P3 (#2214, epic #667): the self-describing `RELN` relation chunk.
// It stores logical `CHILD_OF` edges by real endpoint entity id and a name
// table of the `Relation` enum at the chunk head — Save Format Extensibility
// Rule #4 (relations as first-class data, so adding `OWNS`/`ATTACHED_TO` is a
// one-line `enum Relation` extension). Only `CHILD_OF` is implemented today
// (`PARENT_TO` is the inverse view; `SIBLING_OF` is unimplemented — verified
// engine-wide), so those are named for forward-compat but never emit triples.
//
// Body layout:
//     name-table   (writeNameTable of the current Relation enum)
//     varuint      tripleCount
//     repeat:
//       varuint    relationTypeId   (numeric enum id; name is on-disk identity)
//       varuint    entityA          (child)
//       varuint    entityB          (parent)

namespace IRWorld::detail {

namespace {

using IREntity::EntityId;

const std::array<char, 4> kTagReln = IRAsset::makeTag("RELN");

// The writer's view of the Relation enum, emitted in enum order for a
// deterministic name table. NONE is the "no relation" sentinel and is not a
// storable relation type, so it is omitted.
std::vector<IRAsset::NameTableEntry> buildCurrentRelationNameTable() {
    return {
        {IREntity::CHILD_OF, "CHILD_OF"},
        {IREntity::PARENT_TO, "PARENT_TO"},
        {IREntity::SIBLING_OF, "SIBLING_OF"},
    };
}

// name -> current enum value (Rule #2: the name is the on-disk identity).
// NONE means "not a name this build knows" -> the triple is skipped.
IREntity::Relation relationForName(std::string_view name) {
    if (name == "CHILD_OF") {
        return IREntity::CHILD_OF;
    }
    if (name == "PARENT_TO") {
        return IREntity::PARENT_TO;
    }
    if (name == "SIBLING_OF") {
        return IREntity::SIBLING_OF;
    }
    return IREntity::NONE;
}

} // namespace

IRAsset::ChunkPayload makeRelationChunk(
    IREntity::EntityManager &entityManager, const std::unordered_set<EntityId> &servedIds
) {
    // Every entity sharing an archetype node shares that node's CHILD_OF
    // parent (the parent is part of the archetype identity — a relation
    // pseudo-component). So one parent lookup per node covers all its
    // children. Both endpoints must be in servedIds: a parent P2 opted out
    // of (C_Persistent, a component-backing entity) is recreated with a
    // possibly-new id on load, so its edges can't round-trip through the
    // saved ids and are skipped rather than dangling.
    // Only CHILD_OF is a materialized edge set, so the collection holds bare
    // (child, parent) pairs and CHILD_OF is written as the constant relation
    // id below — the on-disk triple stays generic (a future OWNS relation
    // would append its own (relationId, A, B) triples) without this pass
    // carrying a column of one repeated value.
    std::vector<std::pair<EntityId, EntityId>> childParentEdges;
    for (const auto &nodePtr : entityManager.getArchetypeNodes()) {
        IREntity::ArchetypeNode *node = nodePtr.get();
        if (node->length_ <= 0) {
            continue;
        }
        const EntityId parent = entityManager.getParentEntityFromArchetype(node->type_);
        if (parent == IREntity::kNullEntity || !servedIds.contains(parent)) {
            continue;
        }
        for (int i = 0; i < node->length_; ++i) {
            const EntityId rawChild = node->entities_[i];
            // A relation entity is never itself serialized as a child.
            if (rawChild & IREntity::kEntityFlagIsRelation) {
                continue;
            }
            const EntityId child = rawChild & IREntity::IR_ENTITY_ID_BITS;
            if (!servedIds.contains(child)) {
                continue;
            }
            childParentEdges.emplace_back(child, parent);
        }
    }

    // Deterministic write order: a CHILD_OF child has exactly one parent, so
    // ordering by (child, parent) is a total order and the double-save is
    // byte-identical.
    std::sort(childParentEdges.begin(), childParentEdges.end());

    IRAsset::MemoryBinaryWriter w;
    const std::vector<IRAsset::NameTableEntry> relationNames = buildCurrentRelationNameTable();
    const IRAsset::BinaryStatus nameStatus = IRAsset::writeNameTable(w, relationNames);
    if (!nameStatus.ok()) {
        // A MemoryBinaryWriter over a fixed 3-entry table never fails; this is
        // defensive so a future writer change surfaces rather than shipping a
        // half-written chunk.
        IRE_LOG_ERROR("makeRelationChunk: writeNameTable failed: {}", nameStatus.message_);
        return IRAsset::ChunkPayload{kTagReln, {}};
    }
    w.writeVarUInt(childParentEdges.size());
    for (const auto &[child, parent] : childParentEdges) {
        w.writeVarUInt(static_cast<std::uint64_t>(IREntity::CHILD_OF));
        w.writeVarUInt(child);
        w.writeVarUInt(parent);
    }

    IRAsset::ChunkPayload out;
    out.tag_ = kTagReln;
    out.data_ = w.takeBuffer();
    return out;
}

IRAsset::BinaryStatus applyRelationChunk(
    IREntity::EntityManager &entityManager,
    std::span<const IRAsset::LoadedChunk> chunks,
    const std::unordered_map<EntityId, EntityId> &singletonAliases,
    std::uint64_t &relationsRestored,
    std::uint64_t &relationsSkipped
) {
    const IRAsset::LoadedChunk *reln = IRAsset::findChunk(chunks, kTagReln);
    if (reln == nullptr) {
        return IRAsset::BinaryStatus::success(); // no relations, or a pre-P3 file
    }
    IRAsset::MemoryBinaryReader r(reln->data_.data(), reln->data_.size(), "RELN");

    IRAsset::Result<std::vector<IRAsset::NameTableEntry>> nameEntries = IRAsset::readNameTable(r);
    if (!nameEntries.ok()) {
        return nameEntries.status_;
    }
    const IRAsset::NameTable diskTable(std::move(nameEntries.value_));

    IRAsset::Result<std::uint64_t> tripleCount = r.readVarUInt();
    if (!tripleCount.ok()) {
        return tripleCount.status_;
    }

    // Identity for a regular restored entity (ids restore exact); the alias
    // lookup for a singleton (restored by value onto the live singleton).
    auto resolve = [&](EntityId diskId) -> EntityId {
        const auto it = singletonAliases.find(diskId);
        return it != singletonAliases.end() ? it->second : diskId;
    };

    for (std::uint64_t i = 0; i < tripleCount.value_; ++i) {
        IRAsset::Result<std::uint64_t> relationTypeId = r.readVarUInt();
        if (!relationTypeId.ok()) {
            return relationTypeId.status_;
        }
        IRAsset::Result<std::uint64_t> childRaw = r.readVarUInt();
        if (!childRaw.ok()) {
            return childRaw.status_;
        }
        IRAsset::Result<std::uint64_t> parentRaw = r.readVarUInt();
        if (!parentRaw.ok()) {
            return parentRaw.status_;
        }

        // Rule #2: disk id -> disk name -> current enum. Only CHILD_OF is
        // replayable (setParent handles nothing else); an unknown name or a
        // forward-compat PARENT_TO/SIBLING_OF triple is skipped, not fatal.
        const std::optional<std::string_view> diskName =
            diskTable.nameById(static_cast<std::uint32_t>(relationTypeId.value_));
        if (!diskName.has_value() || relationForName(*diskName) != IREntity::CHILD_OF) {
            ++relationsSkipped;
            IRE_LOG_WARN(
                "world snapshot: RELN triple {} has unknown/unsupported relation id {} — skipped",
                i,
                relationTypeId.value_
            );
            continue;
        }

        const EntityId child = resolve(static_cast<EntityId>(childRaw.value_));
        const EntityId parent = resolve(static_cast<EntityId>(parentRaw.value_));
        if (!entityManager.entityExists(child) || !entityManager.entityExists(parent)) {
            ++relationsSkipped;
            IRE_LOG_WARN(
                "world snapshot: RELN triple {} references a missing entity "
                "(child={}, parent={}) — skipped",
                i,
                child,
                parent
            );
            continue;
        }

        IREntity::setParent(child, parent);
        ++relationsRestored;
    }

    return IRAsset::BinaryStatus::success();
}

} // namespace IRWorld::detail
