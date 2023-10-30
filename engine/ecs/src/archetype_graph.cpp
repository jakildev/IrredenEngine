/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\archetype_graph.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ecs/archetype_graph.hpp>
#include <iterator>

namespace IRECS {

    ArchetypeGraph::ArchetypeGraph()
    :   m_nodes{}
    {
        m_nodes.push_back(std::make_unique<ArchetypeNode>(m_nodeCount++, Archetype{}));
        m_baseNode = m_nodes[0].get();
    }

    ArchetypeNode* ArchetypeGraph::findArchetypeNode(
        const Archetype& type
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        ArchetypeNode* node = m_baseNode;
        for (auto itr = type.begin(); itr != type.end(); itr++) {
            ArchetypeNodeEdge* edge = &node->edges_[*itr];
            if (!edge->add) {
                return nullptr;
            }
            node = edge->add;
        }
        return node;
    }

    ArchetypeNode* ArchetypeGraph::findCreateArchetypeNode(
        const Archetype& type
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        ArchetypeNode* node = m_baseNode;
        for (auto itr = type.begin(); itr != type.end(); itr++) {
            ArchetypeNodeEdge* edge = &node->edges_[*itr];
            if (!edge->add)
                createAndConnectNode(node, *itr);
            node = edge->add;
        }
        return node;
    }


    /* ----------------Private functions----------------- */

    void ArchetypeGraph::createAndConnectNode(ArchetypeNode* prevNode, ComponentId nextType) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        Archetype newType = prevNode->type_;
        newType.insert(nextType);
        m_nodes.push_back(std::make_unique<ArchetypeNode>(m_nodeCount++, newType));
        ArchetypeNode* newNode = m_nodes.back().get();
        prevNode->edges_[nextType].add = newNode;
        newNode->edges_[nextType].remove = prevNode;
    }

    /* ------------Unused functions------------------ */


    void ArchetypeGraph::createArchetypeNodeWithArchetype(const Archetype& type) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_ENTITY_OPS);
        IRProfile::engAssert(findArchetypeNode(type) == nullptr,
            "Archetype node of this type already exists.");
        m_nodes.push_back(std::make_unique<ArchetypeNode>(m_nodeCount++, type));
        ArchetypeNode* newNode = m_nodes.back().get(); // not threadsafe
        connectNodeToBase(newNode);
    }


    void ArchetypeGraph::connectNodeToBase(ArchetypeNode* node) {
        ArchetypeNode* tempNode = m_baseNode;
        Archetype tempType{};
        Archetype nodeType = node->type_;
        IRProfile::engAssert(nodeType.size() > 0,
            "Attempted to connect typeless node to base");
        auto first = nodeType.begin();
        auto last = std::prev(nodeType.end());

        // Iterate all of archetype except for last member
        for(auto itr = first; itr != last; itr++) {
            tempType.insert(*itr);
            ArchetypeNodeEdge* edge = &tempNode->edges_[*itr];
            if(!edge->add) createAndConnectNode(tempNode, *itr);
            tempNode = edge->add;
        }
        tempNode->edges_[*last].add = node;
        node->edges_[*last].remove = tempNode;
    }

    ArchetypeNode* ArchetypeGraph::createNodeChainToBase(ArchetypeNode* node, Archetype subtype) {
        ArchetypeNode* tempNode = m_baseNode;
        Archetype tempType{};

        for(auto itr = subtype.begin(); itr != subtype.end(); itr++) {
            tempType.insert(*itr);
            ArchetypeNodeEdge* edge = &tempNode->edges_[*itr];
            if(!edge->add) {
                m_nodes.push_back(std::make_unique<ArchetypeNode>(m_nodeCount++, tempType));
                edge->add = m_nodes.back().get();
                // for(auto jtr = tempType.begin(); jtr != tempType.end(); jtr++) {
                //     edge->add->components.emplace(*jtr, node->components[*jtr]->cloneEmpty());
                // }
                edge->add->edges_[*itr].remove = tempNode;
                IRProfile::engLogInfo("Created archetype node with components HERE: {}, id={}",
                IRECS::makeComponentString(tempType),
                edge->add->id_);
            }
            tempNode = edge->add;
        }
        return tempNode;
    }

} // namespace IREntity