#ifndef SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H
#define SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H

// Upserts the entity's idle-bob value as a vec3 ADD modifier each UPDATE
// tick via upsertBySourceInPlace. The slot key is (kSourceKey,
// TRANSFORM_TRANSLATION, ADD) — one entry per bob-eligible entity,
// updated in place each tick. No ticksRemaining_ countdown; no
// MODIFIER_DECAY dependency. SYSTEM_PROPAGATE_TRANSFORM later folds the
// resolved vec3 into C_WorldTransform.translation_ per the SQT formula.
//
// Pipeline ordering required by callers:
//   PERIODIC_IDLE → PERIODIC_IDLE_POSITION_OFFSET
//   → ... (modifier resolver pipeline) → PROPAGATE_TRANSFORM

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/transform_modifier_fields.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>

namespace IRSystem {

template <> struct System<PERIODIC_IDLE_POSITION_OFFSET> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;
    static constexpr int kGrainSize = 512;

    // Stable per-system source key for the modifier slot. Using kNullEntity
    // (0) avoids passing EntityId through the tick — which would require the
    // IRSystem::ParallelSafe tag, hitting a gap in PartitionExcludes /
    // MakeMemberTickFn (T-328 sub-task D). The slot
    // (kNullEntity, TRANSFORM_TRANSLATION, ADD) is unique on each entity's
    // own mods because this is the only writer of that (field, kind) pair
    // with source=0. removeBySource(kNullEntity) is never called during
    // normal entity destruction, so the slot lives as long as the entity.
    static constexpr IREntity::EntityId kSourceKey = IREntity::kNullEntity;

    void tick(IRComponents::C_PeriodicIdle &idle, IRComponents::C_Modifiers &mods) {
        IRPrefab::Modifier::upsertBySourceInPlace(
            mods,
            IRPrefab::TransformModifier::translationField(),
            IRComponents::TransformKind::ADD,
            idle.getValue(),
            kSourceKey
        );
    }

    static SystemId create() {
        return registerSystem<
            PERIODIC_IDLE_POSITION_OFFSET,
            IRComponents::C_PeriodicIdle,
            IRComponents::C_Modifiers>("PeriodicIdlePositionOffset");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H */
