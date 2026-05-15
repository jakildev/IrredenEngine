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
// The actual propagation work happens in beginTick(). The framework
// drives a per-archetype-batch tick afterwards, but because parent and
// child archetypes can be visited in arbitrary order, the per-archetype
// tick body is a no-op. The beginTick path queries all candidate
// archetype nodes, topologically sorts them by parent-chain depth, and
// writes C_WorldTransform on each entity in order. The combined cost is
// O(N * passes) where passes ≤ tree depth + 1; for typical hierarchies
// (depth < 16) this is effectively linear in the number of transform
// entities.
//
// Pipeline placement: after the modifier resolver pipeline so the
// resolved fields are current, and before any consumer (render, gizmo,
// physics) that reads C_WorldTransform.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/transform_modifier_fields.hpp>

#include <unordered_set>
#include <vector>

namespace IRSystem {

template <> struct System<PROPAGATE_TRANSFORM> {
    // Scratch containers live as members so the per-frame work allocates
    // once at create time and reuses capacity each tick.
    std::vector<IREntity::ArchetypeNode *> ordered_;
    std::vector<IREntity::ArchetypeNode *> pending_;
    std::unordered_set<IREntity::NodeId> candidateSet_;
    std::unordered_set<IREntity::NodeId> doneSet_;

    void beginTick() {
        const auto translationField = IRPrefab::TransformModifier::translationField();
        const auto scaleField = IRPrefab::TransformModifier::scaleField();
        const auto resolvedFieldsComponentId =
            IREntity::getComponentType<IRComponents::C_ResolvedFields>();

        const auto archetype = IREntity::getArchetype<
            IRComponents::C_LocalTransform,
            IRComponents::C_WorldTransform>();
        auto nodes = IREntity::queryArchetypeNodesSimple(archetype);

        ordered_.clear();
        ordered_.reserve(nodes.size());
        pending_.clear();
        pending_.reserve(nodes.size());
        pending_.insert(pending_.end(), nodes.begin(), nodes.end());

        // Build a set of node ids in the candidate set so we can decide
        // whether a parent archetype is "our problem" or a root from our
        // POV (i.e., parent lacks C_LocalTransform and we treat its
        // world as identity).
        candidateSet_.clear();
        candidateSet_.reserve(nodes.size());
        for (auto *node : nodes) {
            candidateSet_.insert(node->id_);
        }

        doneSet_.clear();
        doneSet_.reserve(nodes.size());

        // Topological sweep — each pass admits any node whose parent's
        // archetype is either outside our candidate set (treated as root)
        // or already processed. Worst case is one pass per chain level.
        while (!pending_.empty()) {
            const auto sizeBefore = ordered_.size();
            for (auto it = pending_.begin(); it != pending_.end();) {
                auto *node = *it;
                IREntity::EntityId parent =
                    IREntity::getParentEntityFromArchetype(node->type_);
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
                            doneSet_.contains(parentNodeId)) {
                            ready = true;
                        }
                    }
                }
                if (ready) {
                    ordered_.push_back(node);
                    doneSet_.insert(node->id_);
                    it = pending_.erase(it);
                } else {
                    ++it;
                }
            }
            if (ordered_.size() == sizeBefore) {
                // No progress this pass — a parent-chain cycle would be
                // an engine bug, but rather than spin forever we admit
                // the remainder in arbitrary order so downstream
                // consumers see SOMETHING.
                IR_ASSERT(
                    false,
                    "PROPAGATE_TRANSFORM: parent-chain cycle detected; admitting remaining "
                    "{} archetype nodes in arbitrary order to avoid infinite loop",
                    pending_.size()
                );
                ordered_.insert(ordered_.end(), pending_.begin(), pending_.end());
                pending_.clear();
                break;
            }
        }

        for (auto *node : ordered_) {
            composeNode(node, translationField, scaleField, resolvedFieldsComponentId);
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
    static void composeNode(
        IREntity::ArchetypeNode *node,
        IRComponents::FieldBindingId translationField,
        IRComponents::FieldBindingId scaleField,
        IREntity::ComponentId resolvedFieldsComponentId
    ) {
        IRComponents::C_WorldTransform parentWorld{};
        IREntity::EntityId parent =
            IREntity::getParentEntityFromArchetype(node->type_);
        if (parent != IREntity::kNullEntity) {
            auto parentWorldOpt =
                IREntity::getComponentOptional<IRComponents::C_WorldTransform>(parent);
            if (auto *p = parentWorldOpt.value_or(nullptr)) {
                parentWorld = *p;
            }
        }

        auto &locals =
            IREntity::getComponentData<IRComponents::C_LocalTransform>(node);
        auto &worlds =
            IREntity::getComponentData<IRComponents::C_WorldTransform>(node);

        // Resolved-field column is optional — only present on entities
        // that opted into the modifier framework. Reading the column once
        // per node (vs. once per entity) keeps the modifier path on the
        // batched, cache-friendly path.
        std::vector<IRComponents::C_ResolvedFields> *resolvedCol = nullptr;
        if (node->type_.contains(resolvedFieldsComponentId)) {
            resolvedCol =
                &IREntity::getComponentData<IRComponents::C_ResolvedFields>(node);
        }

        for (int i = 0; i < node->length_; ++i) {
            const auto &local = locals[i];

            IRMath::vec3 modTranslation(0.0f);
            IRMath::vec3 modScale(1.0f);
            if (resolvedCol != nullptr) {
                modTranslation =
                    (*resolvedCol)[i].getVec3(translationField, IRMath::vec3(0.0f));
                modScale = (*resolvedCol)[i].getVec3(scaleField, IRMath::vec3(1.0f));
            }

            const IRMath::vec3 worldScale =
                parentWorld.scale_ * local.scale_ * modScale;
            const IRMath::vec4 worldRotation =
                IRMath::quatMul(parentWorld.rotation_, local.rotation_);
            const IRMath::vec3 worldTranslation =
                parentWorld.translation_ +
                IRMath::rotateVectorByQuat(
                    parentWorld.scale_ * local.translation_,
                    parentWorld.rotation_
                ) +
                modTranslation;

            worlds[i] =
                IRComponents::C_WorldTransform{worldTranslation, worldRotation, worldScale};
        }
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PROPAGATE_TRANSFORM_H */
