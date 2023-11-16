/*
 * Project: Irreden Engine
 * File: component_voxel_set.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_VOXEL_SET_H
#define COMPONENT_VOXEL_SET_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/voxel/systems/system_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_scene_node.hpp>


using namespace IRMath;
using IRRender::ImageData;
using IRRender::Texture2D;
using IRRender::ResourceId;
// TODO: add primitives to voxel set, not just setting individual voxels...
// UPDATE: see component_geometric_shape.hpp

namespace IRComponents {

    struct C_VoxelSetNew {
        int numVoxels_;
        ivec3 size_;
        std::span<C_Position3D> positions_;
        std::span<C_PositionOffset3D> positionOffsets_; // WIP
        std::span<C_PositionGlobal3D> globalPositions_;
        std::span<C_Voxel> voxels_;

        C_VoxelSetNew(
            ivec3 size,
            Color color = IRColors::kGreen
            // int voxelPoolId = 0
        )
        :   numVoxels_{size.x * size.y * size.z}
        ,   size_{size}
        {
            auto voxels = IRRender::allocateVoxels(
                size.x * size.y * size.z
            );
            positions_ = std::get<0>(voxels);
            positionOffsets_ = std::get<1>(voxels);
            globalPositions_ = std::get<2>(voxels);
            voxels_ = std::get<3>(voxels);

            for(int x = 0; x < size.x; x++) {
                for(int y = 0; y < size.y; y++) {
                    for(int z = 0; z < size.z; z++) {
                        positions_[
                            index3DtoIndex1D(ivec3(x, y, z), size)
                        ] = C_Position3D{vec3(x, y, z)};
                        voxels_[
                            index3DtoIndex1D(ivec3(x, y, z), size)
                        ].color_ = color;
                    }
                }
            }
            IRProfile::engLogDebug("Allocated {} voxel(s)", numVoxels_);
        }

        C_VoxelSetNew(int width, int height, int depth)
        :   C_VoxelSetNew(ivec3(width, height, depth))
        {

        }

        C_VoxelSetNew(int width, int height, int depth, Color color)
        :   C_VoxelSetNew(ivec3(width, height, depth), color)
        {

        }

        // default constructor
        C_VoxelSetNew()
        :   C_VoxelSetNew(ivec3(0, 0, 0))
        {

        }

        // TODO: should a similar onCreate method be used for allocating
        // voxels, just in case the constructor might be called in more than
        // one place?
        void onDestroy() {
            IRRender::deallocateVoxels(
                positions_,
                positionOffsets_,
                globalPositions_,
                voxels_
            );
            IRProfile::engLogDebug("Deallocated {} voxels", numVoxels_);
        }

        void changeVoxelColor(ivec3 index, Color color) {
            voxels_[index3DtoIndex1D(index, size_)].color_ = color;
        }

        void changeVoxelColorAll(Color color) {
            for(int i = 0; i < numVoxels_; i++) {
                voxels_[i].color_ = color;
            }
        }

        void deactivateAll() {
            for(int i = 0; i < numVoxels_; i++) {
                voxels_[i].deactivate();
            }
        }

        void activateAll() {
            for(int i = 0; i < numVoxels_; i++) {
                voxels_[i].activate();
            }
        }

        // take positions of all voxels in voxel object and form a new shape. This could
        // mean moving the parent positions of the Entity to the new desired location OMG

        // void reform(std::vector<EntityHandle>& voxelSetEntities) {
        //     for(auto& voxel: voxelSetEntities) {

        //     }
        // }

        // Be able to bind a function like this to a command!
        void reshape(Shape3D shape3D) {
            if(shape3D == Shape3D::RECTANGULAR_PRISM) {
                for(int x = 0; x < size_.x; x++) {
                    for(int y = 0; y < size_.y; y++) {
                        for(int z = 0; z < size_.z; z++) {
                            int index = index3DtoIndex1D(ivec3(x, y, z), size_);
                            voxels_[index].activate();
                        }
                    }
                }
            }
            if(shape3D == Shape3D::SPHERE) {
                vec3 center = vec3(size_) / 2.0f;
                float radius = min(size_.x, min(size_.y, size_.z)) / 2.0f;
                for(int x = 0; x < size_.x; x++) {
                    for(int y = 0; y < size_.y; y++) {
                        for(int z = 0; z < size_.z; z++) {
                            vec3 pos = vec3(x, y, z);
                            float distance = length(pos - center);
                            int index = index3DtoIndex1D(ivec3(x, y, z), size_);
                            if(distance >= radius) {
                                voxels_[index].deactivate();
                            }
                            else {
                                voxels_[index].activate();
                            }
                        }
                    }
                }
            }
        }

        vec3 getLocalPosition(int index) {
            return
                positions_[index].pos_ +
                positionOffsets_[index].pos_
            ;
        }

        // TODO each individual voxel should be treated like this
        // and a set should only contain local positions...
        void updateAsChild(C_Position3D parentPosition) {

            for(int i = 0; i < numVoxels_; i++) {
                globalPositions_[i].pos_ =
                    getLocalPosition(i) +
                    parentPosition.pos_;
            }
        }

        // TODO: get rid of all unneeded voxels
        void freeInvisableVoxels(bool withAnimation = false) {
            // Voxel pool will have to resort allocation and free
            // a whole chunk at a time
        }

        // int addVoxelSceneNode

    };

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_SET_H */
