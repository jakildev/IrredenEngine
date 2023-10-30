/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_lifetime.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_LIFETIME_H
#define SYSTEM_LIFETIME_H

#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class IRSystem<LIFETIME> : public IRSystemBase<
        LIFETIME,
        C_Lifetime
    >   {
    public:
        IRSystem() {
            IRProfile::engLogInfo("Created system LIFETIME");
        }
        virtual ~IRSystem() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            std::vector<C_Lifetime>& lifetimes
        )
        {
            for(int i=0; i < entities.size(); i++) {
                lifetimes[i].life_--;
                if(lifetimes[i].life_ <= 0) {
                    IRECS::getEntityManager().markEntityForDeletion(entities[i]);
                }
            }
        }
    private:
        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };

} // namespace IRECS

#endif /* SYSTEM_LIFETIME_H */
