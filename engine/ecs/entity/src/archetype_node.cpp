/*
 * Project: Irreden Engine
 * File: archetype_node.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/entity/archetype_node.hpp>

// TODO: SHOULD absolutly have node specialization
// that can handle multiple tickWithArchetype functions...
// maybe just dynamic not actually template specilization

// Is there a way to store all of same component in one 1d array
// instead of across multiple nodes? Archetype changes in components
// would be specific indexes where certain arrays start and stop.
// I'll draw something up about what I am thinking...

namespace IRECS {

    ArchetypeNode::ArchetypeNode(NodeId nodeId, const Archetype& archetype)
    :   type_{archetype}
    ,   entities_{}
    ,   edges_{}
    ,   length_(0)
    ,   id_(nodeId)
    {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        for(auto itr = archetype.begin(); itr != archetype.end(); itr++) {
            if(IRECS::isPureComponent(*itr)) {
                components_[*itr] =
                    IRECS::createComponentData(*itr);
            }
        }
        IRE_LOG_INFO("Created archetype node with components: {}, id={}",
            makeComponentStringInternal(archetype),
            id_
        );
    }

    RelationId ArchetypeNode::getChildOfRelation() {
        for(RelationId relation : type_) {
            if(IRECS::isChildOfRelation(relation)) {
                return relation;
            }
        }
        return kNullRelation;
    }

} // namespace IRECS