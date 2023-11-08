/*
 * Project: Irreden Engine
 * File: component_voxel_pool.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_VOXEL_POOL_H
#define COMPONENT_VOXEL_POOL_H

// Not sure if this is used rn, might get rid of it

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

#include <span>
#include <optional>
#include <map>

using namespace IRMath;
using IRECS::EntityId;

namespace IRComponents {

    // Should be the top level scene
    struct C_VoxelPool {
    public:
        C_VoxelPool(ivec3 numVoxels)
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
                C_Voxel{Color{255, 0, 0, 255}}
            );

        }

        C_VoxelPool() {

        }

        // EntityId addVoxel

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
                IRProfile::engLogDebug("Reusing existing span from {} to {}", startIndex, startIndex + size - 1);
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
                IRProfile::engLogDebug("Allocated voxels from {} to {}", startIndex, m_voxelPoolIndex - 1);
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

            IR_ASSERT(false, "Ran out of voxels");

            return std::make_tuple(
                std::span<C_Position3D>{},
                std::span<C_PositionOffset3D>{},
                std::span<C_PositionGlobal3D>{},
                std::span<C_Voxel>{}
            );
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

        const std::vector<C_Position3D>& getPositions() const {
            return m_voxelPositions;
        }

        const std::vector<C_PositionOffset3D>& getPositionOffsets() const {
            return m_voxelPositionsOffset;
        }

        const std::vector<C_PositionGlobal3D>& getPositionGlobals() const {
            return m_voxelPositionsGlobal;
        }

        const std::vector<C_Voxel>& getColors() const {
            return m_voxelColors;
        }

        int getVoxelPoolSize() const {
            return m_voxelPoolSize;
        }
        ivec3 getVoxelPoolSize3D() const {
            return m_voxelPoolSize3D;
        }

    private:
        int m_voxelPoolSize;
        ivec3 m_voxelPoolSize3D;

        // Guarentee that IDs increment with index;
        // This is the key to updating the scene.
        std::vector<EntityId> m_voxelEntities;
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
} // namespace IRComponents

#endif /* COMPONENT_VOXEL_POOL_H */
