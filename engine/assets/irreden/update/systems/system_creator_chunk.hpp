/*
 * Project: Irreden Engine
 * File: system_creator_chunk.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Depricated but still some useful stuff here so keeping
// around for now.

// #ifndef SYSTEM_CHUNK_CREATOR_H
// #define SYSTEM_CHUNK_CREATOR_H

// #include "..\world\global.hpp"
// #include "../systems/system.hpp"
// #include <irreden/ir_constants.hpp>
// #include "..\world\glfw_helper.hpp"
// #include <irreden/ecs/entity_handle.hpp>

// #include <irreden/common/components/component_tags_all.hpp>
// #include "..\input\ir_input.hpp"

// using namespace IRComponents;
// using namespace IRMath;
// using namespace IRConstants;
// using namespace IRECS;
// using namespace IRInput;

// namespace IRECS {

//      // Commands shape modes

//     constexpr std::pair<KeyMouseButtons, Shape3D> kKeyCommandsShapeMode[] = {
//         {kKeyButtonR, Shape3D::RECTANGULAR_PRISM},
//         {kKeyButtonS, Shape3D::SPHERE}
//     };
//     constexpr int kNumKeyCommandsShapeMode =
//         sizeof(kKeyCommandsShapeMode) /
//         sizeof(kKeyCommandsShapeMode[0]);

//     class SystemCreatorChunk {
//     public:
//         SystemCreatorChunk();
//         void beginExecute();
//         void endExecute();

//     private:
//         EntityHandle m_chunk;
//         EntityHandle m_paintingGround;
//         int m_maxZLevel;
//         int m_minZLevel;
//         Shape3D m_shapeMode;
//         Shape3D m_nextShapeMode;
//         int m_zLevel;
//         ivec3 m_voxelStartIndex;

//         // TODO: Perhaps shape start and shape end are a part of
//         // voxel set creator, and there is entityStart and entityEnd here
//         template<Shape3D shapeType>
//         void shapeStart();

//         template<Shape3D shapeType>
//         void shapeEnd();
//     };


// } // namespace IRECS

// #endif /* SYSTEM_CHUNK_CREATOR_H */
