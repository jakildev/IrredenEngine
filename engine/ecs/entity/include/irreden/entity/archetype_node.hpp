/*
 * Project: Irreden Engine
 * File: archetype_node.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ARCHETYPE_NODE_H
#define ARCHETYPE_NODE_H

#include <irreden/entity/ir_entity_types.hpp>
#include "i_component_data.hpp"

#include <vector>
#include <unordered_map>
#include <memory>
// TODO: Specialize nodes as systems and consolidate system code
// this could eliminate the need to query archetype nodes

namespace IRECS {
    // TODO: ecs can know a certain archetype and how to sort it

    struct ArchetypeNodeEdge {
        ArchetypeNode* add = nullptr;
        ArchetypeNode* remove = nullptr;
    };

    // TODO: make archetype node own all i_component_data inserting and whatnot
    class ArchetypeNode{
    public:
        uint32_t id_;
        Archetype type_;
        std::vector<EntityId> entities_;
        std::unordered_map<ComponentId, smart_ComponentData> components_;
        std::unordered_map<ComponentId, ArchetypeNodeEdge> edges_;
        int length_;

        ArchetypeNode(uint32_t nodeId, const Archetype &archetype);
    };

} // namespace IRECS

#endif /* ARCHETYPE_NODE_H */
