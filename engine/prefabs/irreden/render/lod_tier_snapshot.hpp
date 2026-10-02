#ifndef LOD_TIER_SNAPSHOT_H
#define LOD_TIER_SNAPSHOT_H

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/lod_utils.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace IRPrefab::Lod {

// One tick's view of the LOD tier state: the active tier plus every pinned
// entity. A tier consumer owns one as a system member, calls `capture()` in
// `beginTick` and `resolve()` per entity, so the per-entity path is a binary
// search only while some entity is pinned and never a component lookup.
//
// `capture()` allocates nothing once warm: it caches the component ids and
// walks the archetype nodes itself, because `getComponentType<>` builds a type
// name string, `getComponentData<>` / `singletonOrNull<>` copy the node's
// archetype set, and `queryArchetypeNodesSimple` returns a fresh vector, all
// per call. `overrides_` keeps its capacity across captures.
struct TierSnapshot {
    // LOD_4 with no C_ActiveLodLevel row: a creation that never registers
    // LOD_UPDATE keeps every default-band entity drawn.
    IRRender::LodLevel active_ = IRRender::LodLevel::LOD_4;
    // Sorted by entity id.
    std::vector<std::pair<IREntity::EntityId, IRComponents::C_LodTierOverride>> overrides_;
    IREntity::ComponentId activeType_ = IREntity::kNullComponent;
    IREntity::ComponentId overrideType_ = IREntity::kNullComponent;

    void capture() {
        IREntity::EntityManager &entityManager = IREntity::getEntityManager();
        if (activeType_ == IREntity::kNullComponent) {
            activeType_ = entityManager.getComponentType<IRComponents::C_ActiveLodLevel>();
            overrideType_ = entityManager.getComponentType<IRComponents::C_LodTierOverride>();
        }

        active_ = IRRender::LodLevel::LOD_4;
        const IREntity::EntityId lodEntity =
            entityManager.getSingletonByComponentIdOrNull(activeType_);
        const IREntity::EntityRecord *lodRecord =
            lodEntity != IREntity::kNullEntity ? entityManager.findRecord(lodEntity) : nullptr;
        if (lodRecord != nullptr && lodRecord->archetypeNode != nullptr) {
            if (const auto *lod =
                    column<IRComponents::C_ActiveLodLevel>(lodRecord->archetypeNode, activeType_)) {
                active_ = (*lod)[lodRecord->row].current_;
            }
        }

        overrides_.clear();
        for (const auto &node : entityManager.getArchetypeNodes()) {
            if (node->length_ <= 0) {
                continue;
            }
            const auto *pinned = column<IRComponents::C_LodTierOverride>(node.get(), overrideType_);
            if (pinned == nullptr) {
                continue;
            }
            for (int i = 0; i < node->length_; ++i) {
                overrides_.emplace_back(node->entities_[i], (*pinned)[i]);
            }
        }
        std::sort(overrides_.begin(), overrides_.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
    }

    IRRender::LodLevel resolve(IREntity::EntityId entity) const {
        if (overrides_.empty()) {
            return active_;
        }
        const auto pinned = std::lower_bound(
            overrides_.begin(),
            overrides_.end(),
            entity,
            [](const auto &entry, IREntity::EntityId id) { return entry.first < id; }
        );
        const bool found = pinned != overrides_.end() && pinned->first == entity;
        return IRRender::resolveEntityLod(active_, found ? &pinned->second : nullptr);
    }

  private:
    template <typename Component>
    static const std::vector<Component> *
    column(IREntity::ArchetypeNode *node, IREntity::ComponentId type) {
        const auto found = node->components_.find(type);
        if (found == node->components_.end()) {
            return nullptr;
        }
        return &IREntity::castComponentDataPointer<Component>(found->second.get())->dataVector;
    }
};

} // namespace IRPrefab::Lod

#endif /* LOD_TIER_SNAPSHOT_H */
