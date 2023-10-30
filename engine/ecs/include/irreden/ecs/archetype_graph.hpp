/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\archetype_graph.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ARCHETYPE_GRAPH_H
#define ARCHETYPE_GRAPH_H

#include <irreden/ecs/archetype_node.hpp>
#include <irreden/ir_profiling.hpp>
#include <algorithm>
#include <sstream>
#include <tuple>
#include <vector>

namespace IRECS {
    // I had the idea of making seperate graphs for seperate note types.
    // A rendering type node will be stored in a seperate graph.
    // But can be referenced via a entity type??

    // TODO: STRUCTURE SHOULD BE DOCUMENTED
    // DOCUMENT THIS WITH A DEFAULT GRAPH
    class ArchetypeGraph {
    public:
        ArchetypeGraph();
        // TODO: Load archetype graph from file
        // ArchetypeGraph(const char* filename);

        ArchetypeNode* findArchetypeNode(const Archetype& type);
        ArchetypeNode* findCreateArchetypeNode(const Archetype& type);
        inline ArchetypeNode* getBaseNode() { return m_baseNode; }
        inline const std::vector<smart_ArchetypeNode>& getArchetypeNodes() {
            return m_nodes;
        }

        std::vector<ArchetypeNode*> queryArchetypeNodesSimple(
            const Archetype includeComponents,
            const Archetype excludeComponents = Archetype{}
        ) const
        {
            IRProfile::profileFunction(IR_PROFILER_COLOR_UPDATE);
            std::vector<ArchetypeNode*> nodes;
            for(auto& node : m_nodes) {
                if(node->length_ > 0 && std::includes(
                    node->type_.begin(),
                    node->type_.end(),
                    includeComponents.begin(),
                    includeComponents.end()
                ))
                {
                    nodes.push_back(node.get());
                }
            }
            return nodes;
        }

        template <IRRelationType relation>
        void sortSubgraphByRelation(ArchetypeNode* localRootNote) {
            // TODO:

        }

       // DO i need read / write operations that can be atomic

    private:
        ArchetypeNode* m_baseNode;

        // read_buffer
        // write_buffer
        uint32_t m_nodeCount = 0; // TODO: Remove nodes, reuse IDs, etc.
        // Essentially I need to double buffer this maybe??
        std::vector<smart_ArchetypeNode> m_nodes; // write
        std::vector<smart_ArchetypeNode> m_nodesLastFrame; // read (todo)


        // WIP idea
        // virtual void beginPipeline() override {
        //     // copy write_buffer into read_buffer
        //     // clear write_buffer

        // }

        void createAndConnectNode(ArchetypeNode* prevNode, ComponentId nextType);

        // Unused currently
        void createArchetypeNodeWithArchetype(const Archetype& type);
        void connectNodeToBase(ArchetypeNode* node);
        ArchetypeNode* createNodeChainToBase(ArchetypeNode* node, Archetype subtype);
    };

} // namespace IRECS

#endif /* ARCHETYPE_GRAPH_H */