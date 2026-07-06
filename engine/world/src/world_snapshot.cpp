#include <irreden/world/world_snapshot.hpp>

#include "world_snapshot_internal.hpp"

#include <irreden/ir_profile.hpp>

#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/json_sidecar.hpp>

#include <irreden/entity/archetype_node.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/entity/i_component_data.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_persistent.hpp>

#include <irreden/utility/path_utils.hpp>

#include <algorithm>
#include <map>
#include <span>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace IRWorld {

using IREntity::ArchetypeNode;
using IREntity::ComponentId;
using IREntity::EntityId;

namespace {

const std::array<char, 4> kTagCmpn = IRAsset::makeTag("CMPN");
const std::array<char, 4> kTagArch = IRAsset::makeTag("ARCH");
const std::array<char, 4> kTagSngl = IRAsset::makeTag("SNGL");
const std::array<char, 4> kTagMeta = IRAsset::makeTag("META");

// One live (entity, column-source) reference gathered by the walker. A
// merged saved archetype pulls its rows from several live nodes, so each
// entity remembers which node/row its columns come from.
struct SavedEntityRef {
    EntityId maskedId_;
    ArchetypeNode *node_;
    int row_;
};

// A saved archetype block: one projection (set of opted-in ComponentIds)
// and every gameplay entity that projects onto it, across all merged nodes.
struct SavedArchetype {
    IREntity::Archetype projection_;
    std::vector<SavedEntityRef> entities_;
};

// A live singleton to persist by value in the SNGL chunk.
struct SavedSingleton {
    const SaveComponentEntry *entry_;
    EntityId savedId_;
};

// A saved archetype projection's component local indices, ascending — the
// order the ARCH chunk writes columns. Shared by the deterministic archetype
// sort, the ARCH writer, and the debug dump so all three agree.
std::vector<std::uint32_t> sortedLocalIndices(
    const IREntity::Archetype &projection,
    const std::unordered_map<ComponentId, std::uint32_t> &localIndexByComponentId
) {
    std::vector<std::uint32_t> indices;
    indices.reserve(projection.size());
    for (const ComponentId componentId : projection) {
        indices.push_back(localIndexByComponentId.at(componentId));
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}

// Emit the component save-name array (CMPN name-table order) under the current
// JSON key. Shared by the `.json` sidecar and the `.json.txt` debug dump.
void writeComponentsArray(
    IRAsset::JsonSidecarWriter &json, const std::vector<const SaveComponentEntry *> &orderedEntries
) {
    json.beginArray();
    for (const SaveComponentEntry *entry : orderedEntries) {
        json.valueString(entry->saveName_);
    }
    json.endArray();
}

// Human-readable `.json.txt` debug dump (persist W-11, #2218), gated by
// IR_PERSIST_DUMP. A *second writer over the same walk* — it reads the data
// already collected for the binary write (archetype groups, component name
// order, singletons, CHILD_OF edges) rather than re-parsing the binary bytes,
// so it stays a pure side-output: the binary at `path` is byte-identical
// whether the flag is set or not (the epic's W-8 byte-parity is unaffected).
// Best-effort — a write failure never fails the save. This is a richer view
// than the always-on `.json` sidecar (a magic/version/count summary): it lists
// each archetype's members and every parent→child edge, for eyeballing a save.
void writeWorldDump(
    const std::string &path,
    IREntity::EntityManager &em,
    const std::unordered_set<EntityId> &servedIds,
    const std::vector<SavedArchetype> &groups,
    const std::vector<const SaveComponentEntry *> &orderedEntries,
    const std::vector<SavedSingleton> &singletons,
    const std::unordered_map<ComponentId, std::uint32_t> &localIndexByComponentId,
    std::uint64_t totalEntities
) {
    IRAsset::JsonSidecarWriter json;
    json.beginObject();
    json.key("format");
    json.valueString("IRWS-dump");
    json.key("version");
    json.valueUInt(kWorldSnapshotVersion);
    json.key("entities");
    json.valueUInt(totalEntities);

    json.key("components");
    writeComponentsArray(json, orderedEntries);

    json.key("archetypes");
    json.beginArray();
    for (const SavedArchetype &group : groups) {
        // Component save-names for this projection, in ascending local index —
        // the same order the binary writes the columns.
        const std::vector<std::uint32_t> indices =
            sortedLocalIndices(group.projection_, localIndexByComponentId);

        json.beginObject();
        json.key("components");
        json.beginArray();
        for (const std::uint32_t li : indices) {
            json.valueString(orderedEntries[li]->saveName_);
        }
        json.endArray();
        json.key("entities");
        json.beginArray();
        for (const SavedEntityRef &ref : group.entities_) {
            json.valueUInt(ref.maskedId_);
        }
        json.endArray();
        json.endObject();
    }
    json.endArray();

    json.key("singletons");
    json.beginArray();
    for (const SavedSingleton &singleton : singletons) {
        json.beginObject();
        json.key("component");
        json.valueString(singleton.entry_->saveName_);
        json.key("id");
        json.valueUInt(singleton.savedId_);
        json.endObject();
    }
    json.endArray();

    // Relations: only CHILD_OF materializes edges today (mirrors the RELN
    // chunk writer, which writes CHILD_OF as the constant relation id), so the
    // literal type name is emitted per edge.
    json.key("relations");
    json.beginArray();
    for (const auto &[child, parent] : detail::collectChildParentEdges(em, servedIds)) {
        json.beginObject();
        json.key("type");
        json.valueString("CHILD_OF");
        json.key("child");
        json.valueUInt(child);
        json.key("parent");
        json.valueUInt(parent);
        json.endObject();
    }
    json.endArray();
    json.endObject();

    IRAsset::writeJsonSidecarToFile(path + ".json.txt", json.str());
}

} // namespace

IRAsset::BinaryStatus saveWorld(const SaveRegistry &registry, const std::string &path) {
    IREntity::EntityManager &em = IREntity::getEntityManager();

    // Frame-boundary contract: drain deferred structural changes + marked
    // deletions so the archetype graph reflects exactly the live world.
    em.flushStructuralChanges();
    em.destroyMarkedEntities();

    // --- Exclusion set (mirror resetGameplay's destroyAllExceptPreserved) ---
    std::unordered_set<EntityId> singletonIds;
    for (const auto &[componentId, singletonEntity] : em.singletonEntityCache()) {
        if (em.entityExists(singletonEntity)) {
            singletonIds.insert(singletonEntity & IREntity::IR_ENTITY_ID_BITS);
        }
    }
    // C_Persistent id via the by-name lookup so an unregistered C_Persistent
    // (no persistent entity exists yet) does NOT auto-register + mint a
    // backing entity mid-save. kNullComponent never appears in a node type_.
    const ComponentId cPersistentId =
        em.getComponentTypeByName(typeid(IRComponents::C_Persistent).name());

    // --- Walk archetype nodes, group by projection (projection-merge) ---
    std::map<IREntity::Archetype, std::size_t> groupIndexByProjection;
    std::vector<SavedArchetype> groups;
    std::unordered_set<ComponentId> referenced;

    for (const auto &nodePtr : em.getArchetypeNodes()) {
        ArchetypeNode *node = nodePtr.get();
        if (node->length_ <= 0) {
            continue;
        }
        // Whole-node exclusion: C_Persistent entities are recreated by their
        // owner on load, so persisting them would collide on restore.
        if (cPersistentId != IREntity::kNullComponent && node->type_.contains(cPersistentId)) {
            continue;
        }
        // Projection = opted-in + registered subset of this node's archetype.
        IREntity::Archetype projection;
        for (const ComponentId componentId : node->type_) {
            if (registry.findByComponentId(componentId) != nullptr) {
                projection.insert(componentId);
            }
        }
        if (projection.empty()) {
            continue;
        }

        auto [it, inserted] = groupIndexByProjection.try_emplace(projection, groups.size());
        if (inserted) {
            groups.push_back(SavedArchetype{projection, {}});
            for (const ComponentId componentId : projection) {
                referenced.insert(componentId);
            }
        }
        SavedArchetype &group = groups[it->second];

        for (int i = 0; i < node->length_; ++i) {
            const EntityId rawId = node->entities_[i];
            const EntityId maskedId = rawId & IREntity::IR_ENTITY_ID_BITS;
            // Per-entity exclusions: singletons (SNGL chunk), component-
            // backing entities, relation-flagged entities.
            if (singletonIds.contains(maskedId) || em.isComponentBackingEntity(rawId) ||
                (rawId & IREntity::kEntityFlagIsRelation)) {
                continue;
            }
            group.entities_.push_back(SavedEntityRef{maskedId, node, i});
        }
    }

    // Drop groups left empty after per-entity exclusion, and sort each
    // group's entities ascending by id (locked write order).
    groups.erase(
        std::remove_if(
            groups.begin(),
            groups.end(),
            [](const SavedArchetype &g) { return g.entities_.empty(); }
        ),
        groups.end()
    );
    for (SavedArchetype &group : groups) {
        std::sort(
            group.entities_.begin(),
            group.entities_.end(),
            [](const SavedEntityRef &a, const SavedEntityRef &b) {
                return a.maskedId_ < b.maskedId_;
            }
        );
    }

    // --- Collect singletons (by value) ---
    std::vector<SavedSingleton> singletons;
    for (const auto &[componentId, singletonEntity] : em.singletonEntityCache()) {
        const SaveComponentEntry *entry = registry.findByComponentId(componentId);
        if (entry == nullptr || !em.entityExists(singletonEntity)) {
            continue;
        }
        singletons.push_back(SavedSingleton{entry, singletonEntity & IREntity::IR_ENTITY_ID_BITS});
        referenced.insert(componentId);
    }

    // --- Assign local indices: order referenced components by save-name ---
    std::vector<const SaveComponentEntry *> orderedEntries;
    orderedEntries.reserve(referenced.size());
    for (const ComponentId componentId : referenced) {
        orderedEntries.push_back(registry.findByComponentId(componentId));
    }
    std::sort(
        orderedEntries.begin(),
        orderedEntries.end(),
        [](const SaveComponentEntry *a, const SaveComponentEntry *b) {
            return a->saveName_ < b->saveName_;
        }
    );
    std::unordered_map<ComponentId, std::uint32_t> localIndexByComponentId;
    for (std::uint32_t i = 0; i < orderedEntries.size(); ++i) {
        localIndexByComponentId[orderedEntries[i]->componentId_] = i;
    }

    // Deterministic archetype order: by ascending local-index list.
    auto localIndexList = [&](const SavedArchetype &g) {
        return sortedLocalIndices(g.projection_, localIndexByComponentId);
    };
    std::sort(groups.begin(), groups.end(), [&](const SavedArchetype &a, const SavedArchetype &b) {
        return localIndexList(a) < localIndexList(b);
    });
    // Deterministic singleton order: by save-name.
    std::sort(
        singletons.begin(),
        singletons.end(),
        [](const SavedSingleton &a, const SavedSingleton &b) {
            return a.entry_->saveName_ < b.entry_->saveName_;
        }
    );

    // --- Build chunk bodies ---
    std::uint64_t totalEntities = 0;

    // CMPN
    IRAsset::MemoryBinaryWriter cmpnW;
    cmpnW.writeVarUInt(orderedEntries.size());
    for (const SaveComponentEntry *entry : orderedEntries) {
        cmpnW.writeString(entry->saveName_);
    }

    // ARCH
    IRAsset::MemoryBinaryWriter archW;
    archW.writeVarUInt(groups.size());
    for (const SavedArchetype &group : groups) {
        std::vector<std::uint32_t> indices = localIndexList(group);
        archW.writeVarUInt(indices.size());
        for (const std::uint32_t li : indices) {
            archW.writeVarUInt(li);
        }
        archW.writeVarUInt(group.entities_.size());
        for (const SavedEntityRef &ref : group.entities_) {
            archW.writeVarUInt(ref.maskedId_);
        }
        totalEntities += group.entities_.size();
        // One column per component, in the same (ascending local-index)
        // order as the component list above.
        for (const std::uint32_t li : indices) {
            const SaveComponentEntry *entry = orderedEntries[li];
            archW.writeU32(entry->saveVersion_);
            IRAsset::MemoryBinaryWriter colW;
            for (const SavedEntityRef &ref : group.entities_) {
                IREntity::IComponentData *col =
                    ref.node_->components_.at(entry->componentId_).get();
                entry->writeRow_(colW, col, ref.row_);
            }
            const std::vector<std::uint8_t> colBytes = colW.takeBuffer();
            archW.writeVarUInt(colBytes.size());
            archW.writeBytes(colBytes.data(), colBytes.size());
        }
    }

    // SNGL
    IRAsset::MemoryBinaryWriter snglW;
    snglW.writeVarUInt(singletons.size());
    for (const SavedSingleton &singleton : singletons) {
        const std::uint32_t li = localIndexByComponentId.at(singleton.entry_->componentId_);
        snglW.writeVarUInt(li);
        snglW.writeVarUInt(singleton.savedId_);
        snglW.writeU32(singleton.entry_->saveVersion_);
        const EntityId liveEntity = singleton.entry_->getOrCreateSingletonEntity_();
        IRAsset::MemoryBinaryWriter valW;
        singleton.entry_->writeSingleton_(valW, liveEntity);
        const std::vector<std::uint8_t> valBytes = valW.takeBuffer();
        snglW.writeVarUInt(valBytes.size());
        snglW.writeBytes(valBytes.data(), valBytes.size());
    }

    // META
    IRAsset::MemoryBinaryWriter metaW;
    metaW.writeVarUInt(em.entityIdWatermark());
    metaW.writeVarUInt(totalEntities);

    // RELN (persist P3, #2214) — CHILD_OF edges whose both endpoints P2 wrote
    // (ARCH gameplay entities + SNGL singletons). The set is the projection/
    // exclusion decision already made above, so the relation walk reuses it
    // rather than re-deriving the exclusions.
    std::unordered_set<EntityId> servedIds;
    for (const SavedArchetype &group : groups) {
        for (const SavedEntityRef &ref : group.entities_) {
            servedIds.insert(ref.maskedId_);
        }
    }
    for (const SavedSingleton &singleton : singletons) {
        servedIds.insert(singleton.savedId_);
    }

    std::vector<IRAsset::ChunkPayload> chunks;
    chunks.push_back({kTagCmpn, cmpnW.takeBuffer()});
    chunks.push_back({kTagArch, archW.takeBuffer()});
    chunks.push_back({kTagSngl, snglW.takeBuffer()});
    chunks.push_back(detail::makeRelationChunk(em, servedIds));
    chunks.push_back({kTagMeta, metaW.takeBuffer()});

    IRAsset::FileBinaryWriter fileW(path);
    if (!fileW.ok()) {
        return IRAsset::BinaryStatus::error(
            IRAsset::BinaryIOError::OpenFailed,
            "world snapshot: could not open " + path
        );
    }
    IRAsset::BinaryStatus status =
        IRAsset::writeChunked(fileW, kWorldSnapshotMagic, kWorldSnapshotVersion, chunks);
    if (!status.ok()) {
        return status;
    }

    // JSON sidecar (Rule #6, write-only). Best-effort — failure to write it
    // never fails the save (the binary is the source of truth).
    IRAsset::JsonSidecarWriter json;
    json.beginObject();
    json.key("magic");
    json.valueString("IRWS");
    json.key("version");
    json.valueUInt(kWorldSnapshotVersion);
    json.key("entities");
    json.valueUInt(totalEntities);
    json.key("archetypes");
    json.valueUInt(groups.size());
    json.key("singletons");
    json.valueUInt(singletons.size());
    json.key("components");
    writeComponentsArray(json, orderedEntries);
    json.endObject();
    IRAsset::writeJsonSidecarToFile(path + ".json", json.str());

    // Optional detailed human-readable dump (W-11), gated by IR_PERSIST_DUMP.
    // A pure side-output over the same collected data — never alters the binary
    // (flag-off byte-parity holds), so it sits after the binary write like the
    // sidecar above.
    if (IRUtility::envFlagSet("IR_PERSIST_DUMP")) {
        writeWorldDump(
            path,
            em,
            servedIds,
            groups,
            orderedEntries,
            singletons,
            localIndexByComponentId,
            totalEntities
        );
    }

    return IRAsset::BinaryStatus::success();
}

namespace {

// Staged, world-mutation-free parse of one ARCH/SNGL column: where its
// bytes live inside the chunk buffer, so phase 3 can re-read after the
// phase-2 collision check clears.
struct StagedColumn {
    std::uint32_t version_ = 0;
    std::uint64_t byteLength_ = 0;
    std::uint64_t dataOffset_ = 0; // offset within the chunk buffer
};
struct StagedArchetype {
    std::vector<std::uint32_t> localIndices_;
    std::vector<EntityId> entityIds_;
    std::vector<StagedColumn> columns_;
};
struct StagedSingleton {
    std::uint32_t localIndex_ = 0;
    EntityId savedId_ = 0;
    StagedColumn column_;
};

// A failed load: the status, everything else zeroed (Rule #5 — no partial
// counts on a non-ok result).
LoadResult loadFail(IRAsset::BinaryStatus status) {
    LoadResult result;
    result.status_ = std::move(status);
    return result;
}
LoadResult loadError(IRAsset::BinaryIOError code, std::string message) {
    return loadFail(IRAsset::BinaryStatus::error(code, std::move(message)));
}

} // namespace

LoadResult loadWorld(const SaveRegistry &registry, const std::string &path) {
    IREntity::EntityManager &em = IREntity::getEntityManager();

    IRAsset::FileBinaryReader fileR(path);
    if (!fileR.ok()) {
        return loadError(
            IRAsset::BinaryIOError::OpenFailed,
            "world snapshot: could not open " + path
        );
    }

    IRAsset::Result<std::vector<IRAsset::LoadedChunk>> chunksRes =
        IRAsset::readChunks(fileR, kWorldSnapshotMagic, kWorldSnapshotVersion);
    if (!chunksRes.ok()) {
        return loadFail(chunksRes.status_);
    }
    const std::vector<IRAsset::LoadedChunk> &chunks = chunksRes.value_;
    const IRAsset::LoadedChunk *cmpn = IRAsset::findChunk(chunks, kTagCmpn);
    const IRAsset::LoadedChunk *arch = IRAsset::findChunk(chunks, kTagArch);
    const IRAsset::LoadedChunk *sngl = IRAsset::findChunk(chunks, kTagSngl);
    const IRAsset::LoadedChunk *meta = IRAsset::findChunk(chunks, kTagMeta);

    // --- Phase 1: parse (zero world mutation) ---
    // CMPN name table -> resolved registry entries (nullptr = unresolvable).
    std::vector<const SaveComponentEntry *> localEntries;
    if (cmpn != nullptr) {
        IRAsset::MemoryBinaryReader r(cmpn->data_.data(), cmpn->data_.size(), "CMPN");
        IRAsset::Result<std::uint64_t> count = r.readVarUInt();
        if (!count.ok()) {
            return loadFail(count.status_);
        }
        localEntries.reserve(count.value_);
        for (std::uint64_t i = 0; i < count.value_; ++i) {
            IRAsset::Result<std::string> name = r.readString();
            if (!name.ok()) {
                return loadFail(name.status_);
            }
            localEntries.push_back(registry.findByName(name.value_));
        }
    }
    const std::uint64_t localCount = localEntries.size();

    std::vector<StagedArchetype> stagedArchetypes;
    if (arch != nullptr) {
        IRAsset::MemoryBinaryReader r(arch->data_.data(), arch->data_.size(), "ARCH");
        IRAsset::Result<std::uint64_t> archCount = r.readVarUInt();
        if (!archCount.ok()) {
            return loadFail(archCount.status_);
        }
        stagedArchetypes.reserve(archCount.value_);
        for (std::uint64_t a = 0; a < archCount.value_; ++a) {
            StagedArchetype staged;
            IRAsset::Result<std::uint64_t> compCount = r.readVarUInt();
            if (!compCount.ok()) {
                return loadFail(compCount.status_);
            }
            staged.localIndices_.reserve(compCount.value_);
            for (std::uint64_t c = 0; c < compCount.value_; ++c) {
                IRAsset::Result<std::uint64_t> li = r.readVarUInt();
                if (!li.ok()) {
                    return loadFail(li.status_);
                }
                if (li.value_ >= localCount) {
                    return loadError(
                        IRAsset::BinaryIOError::Truncated,
                        "world snapshot: ARCH local index out of range"
                    );
                }
                staged.localIndices_.push_back(static_cast<std::uint32_t>(li.value_));
            }
            IRAsset::Result<std::uint64_t> entCount = r.readVarUInt();
            if (!entCount.ok()) {
                return loadFail(entCount.status_);
            }
            staged.entityIds_.reserve(entCount.value_);
            for (std::uint64_t e = 0; e < entCount.value_; ++e) {
                IRAsset::Result<std::uint64_t> id = r.readVarUInt();
                if (!id.ok()) {
                    return loadFail(id.status_);
                }
                // Saved ids are written masked (`& IR_ENTITY_ID_BITS`). A value
                // above that mask is corruption — reject it here rather than
                // let the phase-2 `& IR_ENTITY_ID_BITS` silently alias it onto
                // a low, possibly-live id.
                if (id.value_ > IREntity::IR_ENTITY_ID_BITS) {
                    return loadError(
                        IRAsset::BinaryIOError::Truncated,
                        "world snapshot: ARCH entity id out of range"
                    );
                }
                staged.entityIds_.push_back(static_cast<EntityId>(id.value_));
            }
            staged.columns_.reserve(compCount.value_);
            for (std::uint64_t c = 0; c < compCount.value_; ++c) {
                StagedColumn column;
                IRAsset::Result<std::uint32_t> version = r.readU32();
                if (!version.ok()) {
                    return loadFail(version.status_);
                }
                IRAsset::Result<std::uint64_t> byteLength = r.readVarUInt();
                if (!byteLength.ok()) {
                    return loadFail(byteLength.status_);
                }
                column.version_ = version.value_;
                column.byteLength_ = byteLength.value_;
                column.dataOffset_ = r.tell();
                IRAsset::BinaryStatus skip = r.seek(column.dataOffset_ + column.byteLength_);
                if (!skip.ok()) {
                    return loadFail(skip);
                }
                staged.columns_.push_back(column);
            }
            stagedArchetypes.push_back(std::move(staged));
        }
    }

    std::vector<StagedSingleton> stagedSingletons;
    if (sngl != nullptr) {
        IRAsset::MemoryBinaryReader r(sngl->data_.data(), sngl->data_.size(), "SNGL");
        IRAsset::Result<std::uint64_t> count = r.readVarUInt();
        if (!count.ok()) {
            return loadFail(count.status_);
        }
        stagedSingletons.reserve(count.value_);
        for (std::uint64_t s = 0; s < count.value_; ++s) {
            StagedSingleton staged;
            IRAsset::Result<std::uint64_t> li = r.readVarUInt();
            if (!li.ok()) {
                return loadFail(li.status_);
            }
            if (li.value_ >= localCount) {
                return loadError(
                    IRAsset::BinaryIOError::Truncated,
                    "world snapshot: SNGL local index out of range"
                );
            }
            staged.localIndex_ = static_cast<std::uint32_t>(li.value_);
            IRAsset::Result<std::uint64_t> savedId = r.readVarUInt();
            if (!savedId.ok()) {
                return loadFail(savedId.status_);
            }
            if (savedId.value_ > IREntity::IR_ENTITY_ID_BITS) {
                return loadError(
                    IRAsset::BinaryIOError::Truncated,
                    "world snapshot: SNGL entity id out of range"
                );
            }
            staged.savedId_ = static_cast<EntityId>(savedId.value_);
            IRAsset::Result<std::uint32_t> version = r.readU32();
            if (!version.ok()) {
                return loadFail(version.status_);
            }
            IRAsset::Result<std::uint64_t> byteLength = r.readVarUInt();
            if (!byteLength.ok()) {
                return loadFail(byteLength.status_);
            }
            staged.column_.version_ = version.value_;
            staged.column_.byteLength_ = byteLength.value_;
            staged.column_.dataOffset_ = r.tell();
            IRAsset::BinaryStatus skip =
                r.seek(staged.column_.dataOffset_ + staged.column_.byteLength_);
            if (!skip.ok()) {
                return loadFail(skip);
            }
            stagedSingletons.push_back(staged);
        }
    }

    EntityId watermark = 0;
    if (meta != nullptr) {
        IRAsset::MemoryBinaryReader r(meta->data_.data(), meta->data_.size(), "META");
        IRAsset::Result<std::uint64_t> w = r.readVarUInt();
        if (!w.ok()) {
            return loadFail(w.status_);
        }
        watermark = static_cast<EntityId>(w.value_);
        // total-entity count is informational; skip if truncated.
    }

    // --- Phase 2: validate (still zero mutation) ---
    // Every restored gameplay id must be free in the live world, and unique
    // within the file. Singleton ids are NOT inserted (restored by value),
    // so they are not collision-checked.
    std::unordered_set<EntityId> seen;
    for (const StagedArchetype &staged : stagedArchetypes) {
        for (const EntityId id : staged.entityIds_) {
            const EntityId masked = id & IREntity::IR_ENTITY_ID_BITS;
            if (em.entityExists(masked)) {
                return loadError(
                    IRAsset::BinaryIOError::Truncated,
                    "world snapshot: restored entity id collides with a live entity — "
                    "call resetGameplay()/destroyAllEntities() before loadWorld()"
                );
            }
            if (!seen.insert(masked).second) {
                return loadError(
                    IRAsset::BinaryIOError::Truncated,
                    "world snapshot: duplicate entity id in file"
                );
            }
        }
    }

    // --- Phase 2b: decode-validate every resolved column (still zero
    // mutation) --- Phase 1 only recorded each column's byte span; a
    // length-valid-but-corrupt column (disk corruption, a half-written file,
    // hand-edited bytes) would otherwise not surface until phase 3's
    // `appendRow_`, by which point `restoreEntitiesBatch` has already spliced
    // this archetype's entities — and every earlier one — into the live
    // graph, leaving a column short of `length_` with no way to unwind. That
    // is the partial mutation the header's "zero world mutation on error"
    // contract (Rule #5) forbids. Dry-run the exact `SaveSerialize<C>::read`
    // decode here, discarding the values, so any failure aborts before the
    // first live write. Unresolvable columns (`entry == nullptr`) are skipped
    // by byte length in phase 3, so they are skipped here too.
    for (const StagedArchetype &staged : stagedArchetypes) {
        for (std::size_t c = 0; c < staged.localIndices_.size(); ++c) {
            const SaveComponentEntry *entry = localEntries[staged.localIndices_[c]];
            if (entry == nullptr) {
                continue;
            }
            const StagedColumn &column = staged.columns_[c];
            // P5 migration dispatch: resolve the reader for the column's
            // on-disk version (current, migrator, or a VersionTooNew /
            // MigratorMissing error). Runs in the zero-mutation phase, so an
            // unmigratable version aborts before phase 3 touches the world.
            IRAsset::BinaryStatus versionStatus;
            const ColumnReadHooks *reader = entry->readerForVersion(column.version_, versionStatus);
            if (reader == nullptr) {
                return loadFail(versionStatus);
            }
            IRAsset::MemoryBinaryReader r(arch->data_.data(), arch->data_.size(), "ARCH");
            IRAsset::BinaryStatus seek = r.seek(column.dataOffset_);
            if (!seek.ok()) {
                return loadFail(seek);
            }
            for (std::size_t e = 0; e < staged.entityIds_.size(); ++e) {
                IRAsset::BinaryStatus rowStatus = reader->decodeRow_(r);
                if (!rowStatus.ok()) {
                    return loadFail(rowStatus);
                }
            }
        }
    }
    for (const StagedSingleton &staged : stagedSingletons) {
        const SaveComponentEntry *entry = localEntries[staged.localIndex_];
        if (entry == nullptr) {
            continue;
        }
        IRAsset::BinaryStatus versionStatus;
        const ColumnReadHooks *reader =
            entry->readerForVersion(staged.column_.version_, versionStatus);
        if (reader == nullptr) {
            return loadFail(versionStatus);
        }
        IRAsset::MemoryBinaryReader r(sngl->data_.data(), sngl->data_.size(), "SNGL");
        IRAsset::BinaryStatus seek = r.seek(staged.column_.dataOffset_);
        if (!seek.ok()) {
            return loadFail(seek);
        }
        IRAsset::BinaryStatus valueStatus = reader->decodeRow_(r);
        if (!valueStatus.ok()) {
            return loadFail(valueStatus);
        }
    }

    // Decode the RELN relation chunk here too, in the mutation-free pass —
    // before the watermark advance and any phase-3 write. The parse is fallible
    // (bad name table / triple count / a truncated triple), so it must run
    // alongside the ARCH/SNGL decode-validate above: a malformed RELN chunk has
    // to abort with a pristine world exactly like a malformed column does.
    // Deferring the parse to the replay below (which runs after phase 3, since
    // setParent needs the live entities and singleton aliases) would leave the
    // entire restored entity set live on a failed load — the Rule #5 violation
    // this split closes. Only the infallible replay stays in the final phase.
    detail::StagedRelations stagedRelations;
    const IRAsset::BinaryStatus relationDecodeStatus =
        detail::decodeRelationChunk(chunks, stagedRelations);
    if (!relationDecodeStatus.ok()) {
        return loadFail(relationDecodeStatus);
    }

    // Every id- and decode-validation has passed, so phase 3 is now
    // guaranteed to complete — advance the allocator watermark HERE, before
    // any mutation. It must precede the singleton loop: a fresh-session
    // singleton lazy-create (`getOrCreateSingletonEntity_` -> `createEntity`)
    // draws its id off `m_nextEntityId`, and if the watermark still sat at its
    // stale pre-load value that draw would collide with a just-restored
    // gameplay id (`allocateEntity`'s `m_entityIndex.emplace` silently no-ops
    // on a dup, and the singleton value then cross-wires onto that gameplay
    // entity's record — no error, no assert). The archetype loop plants
    // explicit ids via `restoreEntitiesBatch` and never allocates, so
    // advancing ahead of it is safe too.
    em.advanceEntityIdWatermark(watermark);

    // --- Phase 3: apply ---
    LoadResult result;
    result.status_ = IRAsset::BinaryStatus::success();

    for (const StagedArchetype &staged : stagedArchetypes) {
        // Resolved archetype = live ComponentIds of the non-skipped columns.
        IREntity::Archetype resolvedSet;
        for (const std::uint32_t li : staged.localIndices_) {
            const SaveComponentEntry *entry = localEntries[li];
            if (entry != nullptr) {
                resolvedSet.insert(entry->componentId_);
            }
        }
        ArchetypeNode *node = em.findCreateArchetypeNode(resolvedSet);
        em.restoreEntitiesBatch(node, staged.entityIds_);

        for (std::size_t c = 0; c < staged.localIndices_.size(); ++c) {
            const std::uint32_t li = staged.localIndices_[c];
            const StagedColumn &column = staged.columns_[c];
            const SaveComponentEntry *entry = localEntries[li];
            if (entry == nullptr) {
                ++result.columnsSkipped_; // unresolvable component — skipped by byte length
                continue;
            }
            // Same P5 version dispatch phase 2b already validated; re-resolve
            // for the append hooks (a version error here would be a phase-2b
            // bug, but the null-check keeps the apply path honest).
            IRAsset::BinaryStatus versionStatus;
            const ColumnReadHooks *reader = entry->readerForVersion(column.version_, versionStatus);
            if (reader == nullptr) {
                return loadFail(versionStatus);
            }
            IRAsset::MemoryBinaryReader r(arch->data_.data(), arch->data_.size(), "ARCH");
            IRAsset::BinaryStatus seek = r.seek(column.dataOffset_);
            if (!seek.ok()) {
                return loadFail(seek);
            }
            IREntity::IComponentData *col = node->components_.at(entry->componentId_).get();
            for (std::size_t e = 0; e < staged.entityIds_.size(); ++e) {
                IRAsset::BinaryStatus rowStatus = reader->appendRow_(r, col);
                if (!rowStatus.ok()) {
                    return loadFail(rowStatus);
                }
            }
            IR_ASSERT(
                static_cast<int>(col->size()) == node->length_,
                "world snapshot restore: column out of sync with archetype row count"
            );
        }
        result.entitiesRestored_ += staged.entityIds_.size();
    }

    for (const StagedSingleton &staged : stagedSingletons) {
        const SaveComponentEntry *entry = localEntries[staged.localIndex_];
        if (entry == nullptr) {
            ++result.singletonsSkipped_;
            continue;
        }
        // Resolve the versioned reader before the lazy singleton get-or-create,
        // so a (phase-2b-validated) version can't strand a freshly-minted
        // singleton entity on an error path.
        IRAsset::BinaryStatus versionStatus;
        const ColumnReadHooks *reader =
            entry->readerForVersion(staged.column_.version_, versionStatus);
        if (reader == nullptr) {
            return loadFail(versionStatus);
        }
        const EntityId liveEntity = entry->getOrCreateSingletonEntity_();
        IRAsset::MemoryBinaryReader r(sngl->data_.data(), sngl->data_.size(), "SNGL");
        IRAsset::BinaryStatus seek = r.seek(staged.column_.dataOffset_);
        if (!seek.ok()) {
            return loadFail(seek);
        }
        IRAsset::BinaryStatus valueStatus = reader->readIntoEntity_(r, liveEntity);
        if (!valueStatus.ok()) {
            return loadFail(valueStatus);
        }
        result.singletonAliases_.emplace(staged.savedId_, liveEntity);
        ++result.singletonsRestored_;
    }

    // Watermark was advanced after phase-2 validation, before any phase-3
    // mutation — advancing it here (after the singleton loop) would let a
    // fresh-session singleton lazy-create draw a just-restored id. See #2213.

    // Final phase: replay CHILD_OF relation edges (persist P3, #2214) from the
    // buffer decoded in phase 2b. Runs after every entity, column, and
    // singleton is in place and the watermark has advanced (in phase 2b, before
    // any phase-3 write), so setParent's synthetic relation entities mint above
    // every restored id and moving a child's archetype node corrupts nothing.
    // The parse already happened and passed — this replay makes no fallible
    // read, so it cannot fail partway and strand a partial edge set.
    detail::applyStagedRelations(
        em,
        stagedRelations,
        result.singletonAliases_,
        result.relationsRestored_,
        result.relationsSkipped_
    );
    return result;
}

} // namespace IRWorld
