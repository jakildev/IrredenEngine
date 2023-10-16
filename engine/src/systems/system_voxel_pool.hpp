/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_voxel_pool.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Must be extended to sort heirarchies and the like

#ifndef SYSTEM_VOXEL_POOL_H
#define SYSTEM_VOXEL_POOL_H

#include "..\ecs\ir_system_base.hpp"

#include "..\entities\entity_single_voxel.hpp"
#include "..\components\component_lifetime.hpp"
#include "..\components\component_voxel_pool.hpp"

#include <queue>
#include <span>
#include <map>
#include <set>
#include <optional>

// using namespace IRECS;
using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

    class VoxelPool {
    public:
        VoxelPool(ivec3 numVoxels)
        :   m_voxelPoolSize{numVoxels.x * numVoxels.y * numVoxels.z}
        ,   m_voxelPositions{}
        ,   m_voxelPositionsOffset{}
        ,   m_voxelPositionsGlobal{}
        ,   m_voxelColors{}
        {
            m_voxelPositions.resize(m_voxelPoolSize);
            std::fill(
                m_voxelPositions.begin(),
                m_voxelPositions.end(),
                C_Position3D{vec3(0, 0, 0)}
            );

            m_voxelPositionsOffset.resize(m_voxelPoolSize);
            std::fill(
                m_voxelPositionsOffset.begin(),
                m_voxelPositionsOffset.end(),
                C_PositionOffset3D{vec3(0, 0, 0)}
            );

            m_voxelPositionsGlobal.resize(m_voxelPoolSize);
            std::fill(
                m_voxelPositionsGlobal.begin(),
                m_voxelPositionsGlobal.end(),
                C_PositionGlobal3D{vec3(0, 0, 0)}
            );

            m_voxelColors.resize(m_voxelPoolSize);
            std::fill(
                m_voxelColors.begin(),
                m_voxelColors.end(),
                C_Voxel{Color{0, 0, 0, 0}}
            );

            ENG_LOG_INFO("Created system VOXEL_POOL");
        }

        std::tuple<
            std::span<C_Position3D>,
            std::span<C_PositionOffset3D>,
            std::span<C_PositionGlobal3D>,
            std::span<C_Voxel>
        > allocateVoxels(unsigned int size)
        {
            auto freeSpan = findFreeSpan(size);
            if (freeSpan.has_value()) {
                size_t startIndex = freeSpan->first;
                ENG_LOG_DEBUG("Reusing existing span from {} to {}", startIndex, startIndex + size - 1);
                m_freeSpanLookup[size].erase(*freeSpan);
                if (m_freeSpanLookup[size].empty()) {
                    m_freeSpanLookup.erase(size);
                }
                return std::make_tuple(
                    std::span<C_Position3D>{
                        m_voxelPositions.data() + startIndex,
                        size
                    },
                    std::span<C_PositionOffset3D>{
                        m_voxelPositionsOffset.data() + startIndex,
                        size
                    },
                    std::span<C_PositionGlobal3D>{
                        m_voxelPositionsGlobal.data() + startIndex,
                        size
                    },
                    std::span<C_Voxel>{
                        m_voxelColors.data() + startIndex,
                        size
                    }
                );
            }

            if (m_voxelPoolIndex + size <= m_voxelPoolSize) {
                int startIndex = m_voxelPoolIndex;
                m_voxelPoolIndex += size;
                ENG_LOG_DEBUG("Allocated voxels from {} to {}", startIndex, m_voxelPoolIndex - 1);
                return std::make_tuple(
                    std::span<C_Position3D>{
                        m_voxelPositions.data() + startIndex,
                        size
                    },
                    std::span<C_PositionOffset3D>{
                        m_voxelPositionsOffset.data() + startIndex,
                        size
                    },
                    std::span<C_PositionGlobal3D>{
                        m_voxelPositionsGlobal.data() + startIndex,
                        size
                    },
                    std::span<C_Voxel>{
                        m_voxelColors.data() + startIndex,
                        size
                    }
                );
            }

            ENG_ASSERT(false, "Ran out of voxels");
        }

        void deallocateVoxels(
            std::span<C_Position3D> positions,
            std::span<C_PositionOffset3D> positionOffsets,
            std::span<C_PositionGlobal3D> positionGlobals,
            std::span<C_Voxel> colors
        )
        {
            for(int i = 0; i < colors.size(); i++) {
                C_Voxel& voxel = colors[i];
                voxel.color_ = Color{0, 0, 0, 0};
            }
            size_t startIndex = positions.data() - m_voxelPositions.data();
            size_t size = positions.size();

            m_freeVoxelSpans.push_back({startIndex, size});
            updateFreeSpanLookup(startIndex, size);
        }

        std::vector<C_Position3D>& getPositions() {
            return m_voxelPositions;
        }
        std::vector<C_PositionOffset3D>& getPositionOffsets() {
            return m_voxelPositionsOffset;
        }
        std::vector<C_PositionGlobal3D>& getPositionGlobals() {
            return m_voxelPositionsGlobal;
        }
        std::vector<C_Voxel>& getColors() {
            return m_voxelColors;
        }
        int getVoxelPoolSize() {
            return m_voxelPoolSize;
        }
        ivec3 getVoxelPoolSize3D() {
            return m_voxelPoolSize3D;
        }

    private:
        int m_voxelPoolSize;
        ivec3 m_voxelPoolSize3D;
        std::vector<C_Position3D> m_voxelPositions;
        std::vector<C_PositionOffset3D> m_voxelPositionsOffset;
        std::vector<C_PositionGlobal3D> m_voxelPositionsGlobal;
        std::vector<C_Voxel> m_voxelColors;
        std::vector<std::pair<size_t, size_t>> m_freeVoxelSpans;
        std::map<size_t, std::set<std::pair<size_t, size_t>>> m_freeSpanLookup;

        int m_voxelPoolIndex = 0;

        void updateFreeSpanLookup(size_t startIndex, size_t size) {
            m_freeSpanLookup[size].insert({startIndex, size});
        }

        std::optional<std::pair<size_t, size_t>> findFreeSpan(size_t requestedSize) const {
            auto it = m_freeSpanLookup.lower_bound(requestedSize);
            if (it != m_freeSpanLookup.end()) {
                std::pair<size_t, size_t> span = *it->second.begin();
                // Return the span if it's not larger than needed
                if (span.second <= requestedSize) {
                    return span;
                }
            }
            return std::nullopt; // No suitable free span found
        }

    };

    template<>
    class IRSystem<VOXEL_POOL> : public IRSystemBase<
        VOXEL_POOL
    >   {
    public:
        IRSystem(ivec3 numVoxelsMainCanvas, ivec3 numVoxelsPlayer)
        :   m_voxelPools{}
        ,   m_voxelPoolIdMainCanvas{-1}
        ,   m_voxelPoolIdPlayer{-1}
        {
            m_voxelPoolIdMainCanvas = m_voxelPools.size();
            m_voxelPools.push_back(VoxelPool(numVoxelsMainCanvas));
            m_voxelPoolIdPlayer = m_voxelPools.size();
            m_voxelPools.push_back(VoxelPool(numVoxelsPlayer));
        }

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities
        )
        {

        }

        std::tuple<
            std::span<C_Position3D>,
            std::span<C_PositionOffset3D>,
            std::span<C_PositionGlobal3D>,
            std::span<C_Voxel>
        > allocateVoxels(int numVoxels, int voxelPoolId = 0)
        {
            return m_voxelPools[voxelPoolId].allocateVoxels(numVoxels);
        }

        void deallocateVoxels(
            std::span<C_Position3D> positions,
            std::span<C_PositionOffset3D> positionOffsets,
            std::span<C_PositionGlobal3D> positionGlobals,
            std::span<C_Voxel> colors,
            int voxelPoolId = 0
        )
        {
            m_voxelPools[voxelPoolId].deallocateVoxels(
                positions,
                positionOffsets,
                positionGlobals,
                colors
            );
        }

        C_VoxelPool getVoxelPoolComponent(int id) {
            C_VoxelPool res{};
            res.numVoxels_ = m_voxelPools[id].getVoxelPoolSize();
            res.size_ = m_voxelPools[id].getVoxelPoolSize3D();
            res.positions_ = m_voxelPools[id].getPositions();
            res.positionOffsets_ = m_voxelPools[id].getPositionOffsets();
            res.positionGlobals_ = m_voxelPools[id].getPositionGlobals();
            res.voxels_ = m_voxelPools[id].getColors();
            return res;
        }

    private:
        std::vector<VoxelPool> m_voxelPools;
        int m_voxelPoolIdMainCanvas;
        int m_voxelPoolIdPlayer;

        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };
}

#endif /* SYSTEM_VOXEL_POOL_H */
