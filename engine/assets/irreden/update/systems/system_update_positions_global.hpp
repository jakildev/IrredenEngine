#ifndef SYSTEM_UPDATE_POSITIONS_GLOBAL_H
#define SYSTEM_UPDATE_POSITIONS_GLOBAL_H

// LEFT OFF HERE: This is how parent positions will be updated,
// and eventually, the actual voxels from this
// But updating voxel globals could be later, or all
// voxels will be in node with leaf voxel set as children!

#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class System<UPDATE_POSITIONS_GLOBAL> : public SystemBase<
        UPDATE_POSITIONS_GLOBAL,
        C_Position3D,
        C_PositionGlobal3D
    >   {
    public:
        System()
        {
            setRelationType(Relation::CHILD_OF);
            IRProfile::engLogInfo("Created system UPDATE_POSITIONS_GLOBAL");
        }

        virtual ~System() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            std::vector<C_Position3D>& positions,
            std::vector<C_PositionGlobal3D>& positionsGlobal
        )
        {
            C_PositionGlobal3D parentPositionGlobal{0, 0, 0};
            EntityId parent = getParentEntityFromArchetype(archetype);
            if(parent != kNullEntity) {
                parentPositionGlobal = IRECS::getComponent<C_PositionGlobal3D>(parent);
            }
            for(int i=0; i < entities.size(); i++) {
                positionsGlobal[i].pos_ = positions[i].pos_ + parentPositionGlobal.pos_;
            }
        }
    };
} // namespace IRECS

#endif /* SYSTEM_UPDATE_POSITIONS_GLOBAL_H */
