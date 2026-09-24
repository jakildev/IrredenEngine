#ifndef IR_PREFAB_FOG_REVEAL_SYSTEMS_H
#define IR_PREFAB_FOG_REVEAL_SYSTEMS_H

// The fog subject-class systems a creation registers as one unit, kept apart
// from `fog_of_war.hpp` because the systems themselves include that header.

#include <irreden/ir_system.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_shape.hpp>
#include <irreden/render/systems/system_fog_subject_adopt.hpp>
#include <irreden/render/systems/system_fog_subject_adopt_shape.hpp>
#include <irreden/render/systems/system_fog_subject_exempt.hpp>

#include <list>

namespace IRPrefab::Fog {

/// Create the BODY reveal systems in their required order — EXEMPT
/// realization, BODY adoption, then the verdict — for splicing into the
/// UPDATE pipeline after PROPAGATE_TRANSFORM and before
/// UPDATE_VOXEL_SET_CHILDREN. Each system serialises on its own
/// (FOG_SUBJECT_ADOPT and FOG_REVEAL_EVAL fan out through PARALLEL_FOR).
inline std::list<IRSystem::SystemId> revealSystems() {
    return {
        IRSystem::createSystem<IRSystem::FOG_SUBJECT_EXEMPT>(),
        IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT>(),
        IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT_SHAPE>(),
        IRSystem::createSystem<IRSystem::FOG_REVEAL_EVAL>(),
        IRSystem::createSystem<IRSystem::FOG_REVEAL_EVAL_SHAPE>(),
    };
}

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_REVEAL_SYSTEMS_H */
