/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_entities\entity_voxel_sprite.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// NOTES: Needs to be reintegrated.

// #ifndef ENTITY_VOXEL_SPRITE_H
// #define ENTITY_VOXEL_SPRITE_H

// #include "..\world\global.hpp"
// #include <irreden/render/rendering_rm.hpp>
// #include <irreden/ecs/entity_handle.hpp>
// #include <irreden/ir_math.hpp>
// #include "..\entity\ir_ecs.hpp"
// #include <irreden/ecs/prefabs.hpp>
// #include <string>

// #include "..\game_components\component_voxel_set.hpp"
// #include "../game_components/component_triangle_set_texture.hpp"
// #include "..\game_components\component_position_3d.hpp"
// #include "..\game_components\component_size_int_2d.hpp"
// #include "..\game_components\component_size_int_3d.hpp"
// #include "..\game_components\component_size_triangles.hpp"

// using namespace IRMath;
// using namespace IRComponents;

// namespace IRECS {

//     template<>
//     struct Prefab<kVoxelSprite> {
//         static EntityHandle create(
//             vec3 position,
//             std::string fileNameImage
//         )
//         {
//             EntityHandle entity{};
//             entity.set(C_Position3D{
//                 position
//             });
//             ImageData imageData{fileNameImage.c_str()};
//             entity.set(C_SizeInt3D{
//                 ivec3(
//                     imageData.width_,
//                     imageData.height_,
//                     1
//                 )
//             });
//             entity.set(C_SizeInt2D{
//                 ivec2(
//                     imageData.width_,
//                     imageData.height_
//                 )
//             });
//             entity.set(C_SizeTriangles{
//                 ivec2(size3DtoSize2DIso(
//                     entity.get<C_SizeInt3D>().size_
//                 ))
//             });
//             C_VoxelSet& voxelSetComponent = entity.set(C_VoxelSet{imageData});
//             ivec2 size2D = voxelSetComponent.triangleSet_.size_;
//             IRRender::Texture2D* textureColor =
//                 IRRender::createResource<IRRender::Texture2D>(
//                     GL_TEXTURE_2D,
//                     size2D.x,
//                     size2D.y,
//                     GL_RGBA8,
//                     GL_CLAMP_TO_EDGE,
//                     GL_NEAREST
//                 )
//                 .second;
//             IRRender::Texture2D* textureDistance =
//                 IRRender::createResource<IRRender::Texture2D>(
//                     GL_TEXTURE_2D,
//                     size2D.x,
//                     size2D.y,
//                     GL_RGBA8,
//                     GL_CLAMP_TO_EDGE,
//                     GL_NEAREST
//                 )
//                 .second;
//             auto& triangleTextureComponent = entity.set(
//                 C_TriangleSetTexture{textureColor, textureDistance}
//             );
//             triangleTextureComponent.updateTextures(
//                 entity.get<C_VoxelSet>().triangleSet_
//             );
//             return entity;
//         }
//     };
// }

// #endif /* ENTITY_VOXEL_SPRITE_H */
