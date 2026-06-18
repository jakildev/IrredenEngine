#ifndef SYSTEM_PROPAGATE_TRANSFORM_H
#define SYSTEM_PROPAGATE_TRANSFORM_H

// SYSTEM_PROPAGATE_TRANSFORM — composes C_WorldTransform for every
// entity carrying C_LocalTransform + C_WorldTransform, walking the
// CHILD_OF parent chain in topological order (parents-before-children).
//
// Composition formula (per the locked SQT design in
// engine/prefabs/irreden/common/CLAUDE.md):
//
//   world.scale       = parent.world.scale * local.scale * modifier_scale
//   world.rotation    = quatMul(parent.world.rotation, local.rotation)
//   world.translation = parent.world.translation
//                     + rotateVectorByQuat(parent.world.scale * local.translation,
//                                          parent.world.rotation)
//                     + modifier_translation
//
// Roots (entities whose archetype carries no CHILD_OF, or whose parent
// archetype lacks C_WorldTransform) use identity as the parent transform.
//
// The modifier_translation / modifier_scale come from the entity's
// C_ResolvedFields under the TRANSFORM_TRANSLATION / TRANSFORM_SCALE
// vec3 fields (see transform_modifier_fields.hpp). Default values when
// no resolved field exists: translation 0, scale 1 — i.e. no
// perturbation. The matching ROTATION quat field arrives with T-198;
// until then, modifier_rotation is identity.
//
// Two-pass architecture (T-378):
//
//   Pass 1 (serial, beginTick prelude) — topological partition: group
//   the candidate archetype nodes by parent-chain depth into per-level
//   buckets. Archetypes at the same depth are pairwise independent
//   (no shared writes; their parents have strictly smaller depth and
//   are finalized by the prior level's pass).
//
//   Pass 2 (parallel per level) — for each depth in order, resolve
//   each archetype's parent C_WorldTransform on the main thread (the
//   prior level finished its writes, so the lookup is safe), then
//   dispatch composition to IRJob workers via a single parallelFor.
//   The level is flattened into a list of row-chunks: a small
//   archetype contributes one chunk (its whole row range), while a
//   large archetype is split into several row-range chunks so a
//   single dominant node (e.g. IRPerfGrid's 262K-entity node) fans
//   out across workers instead of running serially on one. Disjoint
//   row ranges are race-free — each entity writes only its own
//   C_WorldTransform[i] from its own C_LocalTransform[i] + a
//   read-only parent — so the result is bit-identical to the serial
//   path regardless of chunk boundaries. The implicit barrier between
//   levels guarantees the prior level's writes are visible.
//
// The level partition only depends on the archetype graph (entity
// spawn/destroy/reparent that introduces or removes archetype nodes,
// or that moves a parent entity to a different archetype). Cached
// across frames by signature comparison of (nodeId, parentNodeId)
// pairs; stable scenes skip the partition pass entirely. The
// per-tick parent-world resolve and the parallel composition itself
// always run — that's the hot work.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/modifier_compose.hpp>
#include <irreden/common/transform_modifier_fields.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace IRSystem {

template <> struct System<PROPAGATE_TRANSFORM> {
    // Parallelization thresholds. A level is dispatched in parallel
    // when it holds at least kMinForParallel archetype nodes (the
    // inter-node path) OR at least kMinRowsForParallel total entity
    // rows (the intra-node path — a single large node still
    // parallelizes). Below both, per-task dispatch overhead isn't
    // worth it and the level composes serially.
    //
    // Within a parallelized level, work is split into row-chunks of
    // ~max(kMinChunkRows, totalRows / targetTasks) rows. A node
    // smaller than the chunk size stays whole (one chunk), so the
    // many-small-node workload keeps its original node-granularity;
    // only nodes larger than the chunk size split across workers.
    static constexpr int kMinForParallel = 8;
    static constexpr int kMinRowsForParallel = 4096;
    static constexpr int kMinChunkRows = 2048;

    // Cached level partition: levels_[d] holds archetype nodes whose
    // parent-chain depth is exactly d. parentWorlds_[d][i] is the
    // per-tick resolved parent transform for levels_[d][i], filled on
    // the main thread immediately before dispatching that level's
    // parallel composition.
    std::vector<std::vector<IREntity::ArchetypeNode *>> levels_;
    std::vector<std::vector<IRComponents::C_WorldTransform>> parentWorlds_;

    // Flattened per-level work list (rebuilt for each level, capacity
    // reused across frames). Each chunk is rows [rowBegin, rowEnd) of
    // levels_[depth][nodeIndex], composed against
    // parentWorlds_[depth][nodeIndex]. Large nodes contribute several
    // chunks, small nodes exactly one. Built on the main thread before
    // the level's parallelFor and read-only on workers during it.
    struct RowChunk {
        int nodeIndex;
        int rowBegin;
        int rowEnd;
    };
    std::vector<RowChunk> chunks_;

    // Topology cache key: (nodeId, parentNodeId) pairs sorted by
    // nodeId. parentNodeId == 0 means "root archetype" (no CHILD_OF,
    // or parent's archetype is not in the candidate set so we treat
    // it as identity). Mismatch ↔ topology changed ↔ re-partition.
    struct CacheEntry {
        IREntity::NodeId nodeId;
        IREntity::NodeId parentNodeId;
        bool operator==(const CacheEntry &o) const noexcept {
            return nodeId == o.nodeId && parentNodeId == o.parentNodeId;
        }
    };
    std::vector<CacheEntry> cachedSignature_;
    std::vector<CacheEntry> scratchSignature_;

    // Topo-sort scratch — reused across re-partition invocations.
    std::vector<IREntity::ArchetypeNode *> pending_;
    std::unordered_set<IREntity::NodeId> candidateSet_;
    std::unordered_map<IREntity::NodeId, int> nodeDepth_;

    void beginTick() {
        const auto translationField = IRPrefab::TransformModifier::translationField();
        const auto scaleField = IRPrefab::TransformModifier::scaleField();
        const auto resolvedFieldsComponentId =
            IREntity::getComponentType<IRComponents::C_ResolvedFields>();
        const auto modifiersComponentId = IREntity::getComponentType<IRComponents::C_Modifiers>();

        const auto archetype =
            IREntity::getArchetype<IRComponents::C_LocalTransform, IRComponents::C_WorldTransform>(
            );
        auto nodes = IREntity::queryArchetypeNodesSimple(archetype);

        buildSignature(nodes, scratchSignature_);
        if (scratchSignature_ != cachedSignature_) {
            rebuildLevels(nodes);
            cachedSignature_.swap(scratchSignature_);
        }

        // Per-level: resolve parent worlds (main thread, prior-level
        // writes are visible), then fan out composition to workers.
        for (std::size_t depth = 0; depth < levels_.size(); ++depth) {
            auto &levelNodes = levels_[depth];
            auto &levelParentWorlds = parentWorlds_[depth];
            levelParentWorlds.resize(levelNodes.size());

            for (std::size_t i = 0; i < levelNodes.size(); ++i) {
                IREntity::EntityId parent =
                    IREntity::getParentEntityFromArchetype(levelNodes[i]->type_);
                IRComponents::C_WorldTransform parentWorld{};
                if (parent != IREntity::kNullEntity) {
                    auto parentWorldOpt =
                        IREntity::getComponentOptional<IRComponents::C_WorldTransform>(parent);
                    if (auto *p = parentWorldOpt.value_or(nullptr)) {
                        parentWorld = *p;
                    }
                }
                levelParentWorlds[i] = parentWorld;
            }

            const int n = static_cast<int>(levelNodes.size());

            // Total entity rows across this level decides whether to
            // split large nodes and how big each row-chunk should be.
            std::int64_t totalRows = 0;
            for (auto *node : levelNodes) {
                totalRows += node->length_;
            }

            auto composeChunkRange = [&](int chunkBegin, int chunkEnd) {
                for (int c = chunkBegin; c < chunkEnd; ++c) {
                    const RowChunk &chunk = chunks_[c];
                    composeNodeRows(
                        levelNodes[chunk.nodeIndex],
                        levelParentWorlds[chunk.nodeIndex],
                        chunk.rowBegin,
                        chunk.rowEnd,
                        translationField,
                        scaleField,
                        resolvedFieldsComponentId,
                        modifiersComponentId
                    );
                }
            };

            const bool runParallel = g_jobManager != nullptr &&
                                     (n >= kMinForParallel || totalRows >= kMinRowsForParallel);

            if (!runParallel) {
                for (int i = 0; i < n; ++i) {
                    composeNodeRows(
                        levelNodes[i],
                        levelParentWorlds[i],
                        0,
                        levelNodes[i]->length_,
                        translationField,
                        scaleField,
                        resolvedFieldsComponentId,
                        modifiersComponentId
                    );
                }
                continue;
            }

            // Aim for ~2 tasks per worker so enkiTS can load-balance
            // without flooding the queue. The chunk size is derived
            // from total work: nodes below it stay whole (preserving
            // node-granularity for many-small-node levels), larger
            // nodes split into multiple row-range chunks.
            const int workers = IRMath::max(1, IRJob::workerCount());
            const int targetTasks = workers * 2;
            const int chunkRows = IRMath::max(
                kMinChunkRows,
                static_cast<int>((totalRows + targetTasks - 1) / targetTasks)
            );

            chunks_.clear();
            for (int i = 0; i < n; ++i) {
                const int len = levelNodes[i]->length_;
                for (int rowBegin = 0; rowBegin < len; rowBegin += chunkRows) {
                    chunks_.push_back({i, rowBegin, IRMath::min(rowBegin + chunkRows, len)});
                }
            }

            const int numChunks = static_cast<int>(chunks_.size());
            const int chunkGrain = IRMath::max(1, (numChunks + targetTasks - 1) / targetTasks);
            IRJob::parallelFor(0, numChunks, chunkGrain, composeChunkRange);
        }
    }

    void tick(
        const IREntity::Archetype &,
        std::vector<IREntity::EntityId> &,
        std::vector<IRComponents::C_LocalTransform> &,
        std::vector<IRComponents::C_WorldTransform> &
    ) {}

    static SystemId create() {
        return registerSystem<
            PROPAGATE_TRANSFORM,
            IRComponents::C_LocalTransform,
            IRComponents::C_WorldTransform>("PropagateTransform");
    }

  private:
    void buildSignature(
        const std::vector<IREntity::ArchetypeNode *> &nodes, std::vector<CacheEntry> &out
    ) const {
        out.clear();
        out.reserve(nodes.size());
        for (auto *node : nodes) {
            IREntity::NodeId parentNodeId = 0;
            IREntity::EntityId parent = IREntity::getParentEntityFromArchetype(node->type_);
            if (parent != IREntity::kNullEntity) {
                auto parentRecord = IREntity::getEntityRecord(parent);
                if (parentRecord.archetypeNode != nullptr) {
                    parentNodeId = parentRecord.archetypeNode->id_;
                }
            }
            out.push_back({node->id_, parentNodeId});
        }
        std::sort(out.begin(), out.end(), [](const CacheEntry &a, const CacheEntry &b) {
            return a.nodeId < b.nodeId;
        });
    }

    void rebuildLevels(const std::vector<IREntity::ArchetypeNode *> &nodes) {
        candidateSet_.clear();
        candidateSet_.reserve(nodes.size());
        for (auto *node : nodes) {
            candidateSet_.insert(node->id_);
        }
        nodeDepth_.clear();
        nodeDepth_.reserve(nodes.size());

        pending_.clear();
        pending_.reserve(nodes.size());
        pending_.insert(pending_.end(), nodes.begin(), nodes.end());

        levels_.clear();

        // Pass admits nodes whose parent is either outside the candidate
        // set (root from our POV) or has had its depth FINALIZED by a
        // strictly earlier pass. Critical: nodeDepth_ is updated only
        // AFTER the pass completes, so two siblings whose parent
        // dependency points within the current pass cannot accidentally
        // co-admit at the same depth.
        while (!pending_.empty()) {
            std::vector<IREntity::ArchetypeNode *> levelNodes;
            for (auto it = pending_.begin(); it != pending_.end();) {
                auto *node = *it;
                IREntity::EntityId parent = IREntity::getParentEntityFromArchetype(node->type_);
                bool ready = false;
                if (parent == IREntity::kNullEntity) {
                    ready = true;
                } else {
                    auto parentRecord = IREntity::getEntityRecord(parent);
                    if (parentRecord.archetypeNode == nullptr) {
                        ready = true;
                    } else {
                        const auto parentNodeId = parentRecord.archetypeNode->id_;
                        if (!candidateSet_.contains(parentNodeId) ||
                            nodeDepth_.contains(parentNodeId)) {
                            ready = true;
                        }
                    }
                }
                if (ready) {
                    levelNodes.push_back(node);
                    it = pending_.erase(it);
                } else {
                    ++it;
                }
            }
            if (levelNodes.empty()) {
                IR_ASSERT(
                    false,
                    "PROPAGATE_TRANSFORM: parent-chain cycle detected; admitting remaining "
                    "{} archetype nodes in arbitrary order to avoid infinite loop",
                    pending_.size()
                );
                levelNodes.insert(levelNodes.end(), pending_.begin(), pending_.end());
                pending_.clear();
            }
            const int currentDepth = static_cast<int>(levels_.size());
            for (auto *node : levelNodes) {
                nodeDepth_.emplace(node->id_, currentDepth);
            }
            levels_.push_back(std::move(levelNodes));
        }

        parentWorlds_.assign(levels_.size(), {});
        for (std::size_t d = 0; d < levels_.size(); ++d) {
            parentWorlds_[d].reserve(levels_[d].size());
        }
    }

    // Composes C_WorldTransform for rows [rowBegin, rowEnd) of node.
    // The per-node column fetches and isRootArchetype check below are
    // cheap relative to the row loop, so re-deriving them per chunk
    // when a large node is split costs nothing meaningful.
    static void composeNodeRows(
        IREntity::ArchetypeNode *node,
        const IRComponents::C_WorldTransform &parentWorld,
        int rowBegin,
        int rowEnd,
        IRComponents::FieldBindingId translationField,
        IRComponents::FieldBindingId scaleField,
        IREntity::ComponentId resolvedFieldsComponentId,
        IREntity::ComponentId modifiersComponentId
    ) {
        auto &locals = IREntity::getComponentData<IRComponents::C_LocalTransform>(node);
        auto &worlds = IREntity::getComponentData<IRComponents::C_WorldTransform>(node);

        // Resolved-field and modifier columns are both optional. Resolved
        // wins when the creation registered the resolver pipeline +
        // attached C_ResolvedFields; otherwise we fall back to composing
        // directly from C_Modifiers so the per-frame additive offset
        // (idle bob, gizmo nudge) still reaches the world transform
        // without forcing every creation to wire the resolver pipeline.
        // Reading both columns once per node keeps the inner loop on the
        // batched cache-friendly path.
        std::vector<IRComponents::C_ResolvedFields> *resolvedCol = nullptr;
        if (node->type_.contains(resolvedFieldsComponentId)) {
            resolvedCol = &IREntity::getComponentData<IRComponents::C_ResolvedFields>(node);
        }
        std::vector<IRComponents::C_Modifiers> *modifiersCol = nullptr;
        if (resolvedCol == nullptr && node->type_.contains(modifiersComponentId)) {
            modifiersCol = &IREntity::getComponentData<IRComponents::C_Modifiers>(node);
        }

        // parentWorld == identity for root archetypes, so quatMul and
        // rotateVectorByQuat are both no-ops. Hoist the check out of the
        // inner loop and skip the quat math — only meaningful with CHILD_OF.
        const bool isRootArchetype =
            (IREntity::getParentEntityFromArchetype(node->type_) == IREntity::kNullEntity);

        for (int i = rowBegin; i < rowEnd; ++i) {
            const auto &local = locals[i];

            IRMath::vec3 modTranslation(0.0f);
            IRMath::vec3 modScale(1.0f);
            if (resolvedCol != nullptr) {
                modTranslation = (*resolvedCol)[i].getVec3(translationField, IRMath::vec3(0.0f));
                modScale = (*resolvedCol)[i].getVec3(scaleField, IRMath::vec3(1.0f));
            } else if (modifiersCol != nullptr) {
                const auto &modsVec3 = (*modifiersCol)[i].modifiersVec3_;
                modTranslation = IRPrefab::Modifier::detail::composeForFieldVec3(
                    IRMath::vec3(0.0f),
                    translationField,
                    modsVec3
                );
                modScale = IRPrefab::Modifier::detail::composeForFieldVec3(
                    IRMath::vec3(1.0f),
                    scaleField,
                    modsVec3
                );
            }

            if (isRootArchetype) {
                worlds[i] = IRComponents::C_WorldTransform{
                    local.translation_ + modTranslation,
                    local.rotation_,
                    local.scale_ * modScale,
                };
                continue;
            }

            const IRMath::vec3 worldScale = parentWorld.scale_ * local.scale_ * modScale;
            const IRMath::vec4 worldRotation =
                IRMath::quatMul(parentWorld.rotation_, local.rotation_);
            const IRMath::vec3 worldTranslation = parentWorld.translation_ +
                                                  IRMath::rotateVectorByQuat(
                                                      parentWorld.scale_ * local.translation_,
                                                      parentWorld.rotation_
                                                  ) +
                                                  modTranslation;

            worlds[i] = IRComponents::C_WorldTransform{worldTranslation, worldRotation, worldScale};
        }
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PROPAGATE_TRANSFORM_H */
