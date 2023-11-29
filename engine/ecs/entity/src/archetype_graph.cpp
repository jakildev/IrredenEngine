/*
 * Project: Irreden Engine
 * File: archetype_graph.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_entity.hpp>

#include <irreden/entity/archetype_graph.hpp>

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
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
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
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
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
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        Archetype newType = prevNode->type_;
        newType.insert(nextType);
        m_nodes.push_back(std::make_unique<ArchetypeNode>(m_nodeCount++, newType));
        ArchetypeNode* newNode = m_nodes.back().get();
        prevNode->edges_[nextType].add = newNode;
        newNode->edges_[nextType].remove = prevNode;
    }

    // Not sure what happens here if a node has a parent with
    // a different archetype.
    std::vector<ArchetypeNode*> ArchetypeGraph::sortArchetypeNodesByRelationChildOf(
        const std::vector<ArchetypeNode*>& nodes
    ) const
    {
        // A breath first sort of nodes based on relation heirarchy
        std::vector<ArchetypeNode*> sortedNodes;
        std::queue<ArchetypeNode*> nodeQueue;
        for(const auto& node : nodes) {
            if(node->getChildOfRelation() == kNullRelation) {
                nodeQueue.push(node);
                sortedNodes.push_back(node);
            }
        }

        while(!nodeQueue.empty()) {
            auto currentNode = nodeQueue.front();
            nodeQueue.pop();
            for(const auto& node: nodes) {
                RelationId childOfRelation = node->getChildOfRelation();
                if(IRECS::getParentNodeFromRelation(childOfRelation) == currentNode->id_) {
                    nodeQueue.push(node);
                    sortedNodes.push_back(node);
                }
            }
        }
        return sortedNodes;
    }

    /* ------------Unused functions------------------ */


    void ArchetypeGraph::createArchetypeNodeWithArchetype(const Archetype& type) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
        IR_ASSERT(findArchetypeNode(type) == nullptr,
            "Archetype node of this type already exists.");
        m_nodes.push_back(std::make_unique<ArchetypeNode>(m_nodeCount++, type));
        ArchetypeNode* newNode = m_nodes.back().get(); // not threadsafe
        connectNodeToBase(newNode);
    }


    void ArchetypeGraph::connectNodeToBase(ArchetypeNode* node) {
        ArchetypeNode* tempNode = m_baseNode;
        Archetype tempType{};
        Archetype nodeType = node->type_;
        IR_ASSERT(nodeType.size() > 0,
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
                IRE_LOG_INFO("Created archetype node with components HERE: {}, id={}",
                makeComponentStringInternal(tempType),
                edge->add->id_);
            }
            tempNode = edge->add;
        }
        return tempNode;
    }

} // namespace IRECS