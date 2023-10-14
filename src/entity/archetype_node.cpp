/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\archetype_node.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "../entity/archetype_node.hpp"
#include "../entity/entity_manager.hpp"
#include "../profiling/logger_spd.hpp"
#include "../world/global.hpp"

// TODO: SHOULD absolutly have node specialization
// that can handle multiple tickWithArchetype functions...
// maybe just dynamic not actually template specilization

namespace IRECS {

    ArchetypeNode::ArchetypeNode(uint32_t nodeId, const Archetype& archetype)
    :   type_{archetype}
    ,   entities_{}
    ,   edges_{}
    ,   length_(0)
    ,   id_(nodeId)
    {
        EASY_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        for(auto itr = archetype.begin(); itr != archetype.end(); itr++) {
            if(global.entityManager_->isPureComponent(*itr)) {
                components_[*itr] =
                    global.entityManager_->createComponentDataVector(*itr);
            }
            else {
                // TODO: archetype based components or something
                ENG_ASSERT(false, "non pure components not supported rn");
            }
        }
        ENG_LOG_INFO("Created archetype node with components: {}, id={}",
            IRECS::makeComponentString(archetype),
            id_
        );
    }

} // namespace IRECS