/*
 * Project: Irreden Engine
 * File: system_creator_chunk.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Depricated but still some useful stuff here so keeping
// around for now.

// #include "system_creator_chunk.hpp"
// #include "../game_entities/entity_chunk.hpp"
// #include "../game_entities/entity_ground_floor.hpp"

// #include "../game_components/component_voxel_set.hpp"
// #include "../game_systems/system_mouse_input.hpp"

// namespace IRECS {

//     SystemCreatorChunk::SystemCreatorChunk()
//     :   m_chunk{EntityChunk::create(IRConstants::kChunkSize, ivec3(0, 0, 0))}
//     ,   m_paintingGround(EntityGroundFloor::create(
//             IRConstants::kChunkGroundOrigin + ivec3(0, 0, 1),
//             // ivec3(0, 0, 0),
//             uvec2(IRConstants::kChunkSize.x, IRConstants::kChunkSize.y)
//         ))
//     ,   m_shapeMode(RECTANGULAR_PRISM)
//     ,   m_nextShapeMode(NONE_SHAPE_3D)
//     ,   m_minZLevel(0)
//     ,   m_maxZLevel(IRConstants::kChunkSize.z - 1)
//     ,   m_zLevel(m_maxZLevel)
//     {
//         IRProfile::engLogInfo("Created system chunk creator");
//     }

//     template<>
//     void SystemCreatorChunk::shapeStart<RECTANGULAR_PRISM>()
//     {
//         // uint32_t distance = chunkVoxelSet.getDistance(mouseIndex);
//         // IRProfile::engLogInfo("Distance: {}", distance & IRConstants::kTriangleDistanceDistanceBits);
//         // ivec3 voxelIndex = IRMath::pos2DIsoToPos3DRectSurface<IRConstants::kChunkSize>(
//         //     mouseIndex
//         // );
//         // m_voxelStartIndex =
//         //     IRMath::pos2DIsoToPos3DAtZLevel<IRConstants::kChunkSize>(
//         //     *global.hoveredTriangleIndexScreen_,
//         //     m_zLevel
//         // );
//         m_voxelStartIndex =
//             IRMath::pos2DIsoToPos3DAtZLevelNew(
//                 (
//                     *global.hoveredTriangleIndexScreen_ -
//                     IRConstants::kScreenTriangleOriginOffsetZ3
//                 ),
//                 m_zLevel
//         );
//         // IRProfile::engLogInfo("Mouse position: {}, {}", mouseIndex.x, mouseIndex.y);
//         IRProfile::engLogInfo("Voxel hovered: {}, {}, {}",
//             m_voxelStartIndex.x,
//             m_voxelStartIndex.y,
//             m_voxelStartIndex.z
//         );
//         // IRProfile::engLogInfo("Hovered triangle index: {}, {}",
//         //     global.hoveredTriangleIndexScreen_->x,
//         //     global.hoveredTriangleIndexScreen_->y
//         // );
//     }

//     template<>
//     void SystemCreatorChunk::shapeEnd<RECTANGULAR_PRISM>()
//     {
//         // ivec3 voxelEndIndex = IRMath::pos2DIsoToPos3DAtZLevel<IRConstants::kChunkSize>(
//         //     *global.hoveredTriangleIndexScreen_,
//         //     m_zLevel
//         // );
//         ivec3 voxelEndIndex =
//             IRMath::pos2DIsoToPos3DAtZLevelNew(
//                 (
//                     *global.hoveredTriangleIndexScreen_ -
//                     IRConstants::kScreenTriangleOriginOffsetZ3
//                 ),
//                 m_zLevel
//         );
//         uvec3 newVoxelsSize =
//             uvec3(glm::abs(voxelEndIndex - m_voxelStartIndex)) +
//             uvec3(1, 1, 1);
//         uvec3 offset = glm::min(m_voxelStartIndex, voxelEndIndex);
//         C_VoxelSet& chunkVoxelSet = m_chunk.get<C_VoxelSet>();
//         chunkVoxelSet.addVoxelsRectangle(
//             newVoxelsSize,
//             offset,
//             IRConstants::kColorBlack
//         );

//         if(m_nextShapeMode != NONE_SHAPE_3D) {
//             m_shapeMode = m_nextShapeMode;
//         }
//         m_nextShapeMode = NONE_SHAPE_3D;
//     }

//     void SystemCreatorChunk::beginExecute() {
//         IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);
//         C_VoxelSet& chunkVoxelSet = m_chunk.get<C_VoxelSet>();

//         // check commands shape mode
//         for(int i = 0; i < kNumKeyCommandsShapeMode; ++i) {
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyCommandsShapeMode[i].first))
//             {
//                 m_shapeMode = kKeyCommandsShapeMode[i].second;
//                 IRProfile::engLogInfo(
//                     "Set painting mode={}",
//                     kKeyCommandsShapeMode[i].second
//                 );
//             }
//         }

//         // check commands mouse scroll
//         for(int i = 0; i < global.scrollEntitiesThisFrame_->size(); i++) {
//             const C_MouseScroll& componentMouseScroll =
//                 IRECS::getEntityManager().getComponent<C_MouseScroll>(
//                     global.scrollEntitiesThisFrame_->at(i)
//                 );
//             if(componentMouseScroll.yoffset_ > 0) {
//                 m_zLevel = glm::min(m_zLevel + 1, m_maxZLevel);
//             }
//             if(componentMouseScroll.yoffset_ < 0) {
//                 m_zLevel = glm::max(m_zLevel - 1, m_minZLevel);
//             }
//         }

//         // check commands shape start
//         if(global.systemKeyMouseInput_->checkButtonPressed(
//             KeyMouseButtons::kMouseButtonLeft
//         ))
//         {
//             if(m_shapeMode == RECTANGULAR_PRISM) {
//                 shapeStart<RECTANGULAR_PRISM>();
//             }
//         }

//         if(global.systemKeyMouseInput_->checkButtonReleased(
//             KeyMouseButtons::kMouseButtonLeft
//         ))
//         {
//             if(m_shapeMode == RECTANGULAR_PRISM) {
//                 shapeEnd<RECTANGULAR_PRISM>();
//             }
//         }
//     }

//     void SystemCreatorChunk::endExecute() {
//         IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);
//     }

// } // namespace IRECS