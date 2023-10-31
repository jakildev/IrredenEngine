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
#include <irreden/ir_constants.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/ecs/system_manager.hpp>
#include <irreden/ecs/entity_handle.hpp>
#include <irreden/voxel/systems/system_voxel_pool.hpp>

using namespace IRMath;
using IRRender::ImageData;
using IRRender::Texture2D;
using IRRender::ResourceId;
using IRECS::EntityHandle;
// TODO: add primitives to voxel set, not just setting individual voxels...
// UPDATE: see component_geometric_shape.hpp

namespace IRComponents {

    struct C_VoxelSceneNode {
        EntityHandle self_{0};
        EntityHandle parent_{0};
        std::vector<EntityHandle> children_{};

        C_VoxelSceneNode(EntityHandle self, EntityHandle parent)
        :   parent_{parent}
        ,   self_{self}
        {

        }

        C_VoxelSceneNode()
        :   C_VoxelSceneNode{
                EntityHandle{0},
                EntityHandle{0}
            }
        {

        }

        void addChild(EntityHandle child) {
            children_.push_back(child);
        }

        void setParent(EntityHandle parent) {
            parent_ = parent;
        }

        void updateChildren(const C_PositionGlobal3D& position) {
            for(auto child: children_) {
                auto& childGlobalPosition = child.get<C_PositionGlobal3D>();
                auto& childPosition = child.get<C_Position3D>();
                childGlobalPosition.pos_ =
                    childPosition.pos_ +
                    position.pos_;
                child.get<C_VoxelSceneNode>().updateChildren(
                    childGlobalPosition
                );
                // ENG_LOG_IN
            }
        }

        void onDestroy() {
            removeNodeFromScene();
        }

        void removeNodeFromScene() {
            auto& parent = self_.get<C_VoxelSceneNode>().parent_;
            auto& children = parent.get<C_VoxelSceneNode>().children_;
            // TODO: also recursively remove childrens children
            // Or recursively destory all child entities...
            children.erase(
                std::remove(
                    children.begin(),
                    children.end(),
                    self_
                ),
                children.end()
            );
        }
    };

    struct C_VoxelScene {
        EntityHandle root_;

        C_VoxelScene()
        :   root_{}
        {
            root_.set(C_VoxelSceneNode{root_, EntityHandle{0}});
            root_.set(C_PositionGlobal3D{vec3(0, 0, 0)});
        }

        void addNode(
            EntityHandle child,
            EntityHandle parent = EntityHandle{0}
        ) {
            if(parent.id_ == 0) {
                parent = root_;
            }
            parent.get<C_VoxelSceneNode>().addChild(child);
            child.set(C_VoxelSceneNode{child, parent});
        }

        void removeNode(
            EntityHandle node
        )
        {
            node.get<C_VoxelSceneNode>().removeNodeFromScene();
        }

        void update() {
            root_.get<C_VoxelSceneNode>().updateChildren(
                root_.get<C_PositionGlobal3D>()
            );
        }

    };



    // struct VoxelSubset {
    //     int startIndex_;
    //     int size_;
    // };

    struct C_VoxelSetNew {
        // Move parent child stuff out of here and treat all voxels as
        // individual entities just like any other voxel set...
        int numVoxels_;
        ivec3 size_;
        std::span<C_Position3D> positions_;
        std::span<C_PositionOffset3D> positionOffsets_; // WIP
        std::span<C_PositionGlobal3D> globalPositions_;
        std::span<C_Voxel> voxels_;

        // OO wait this is probably a problem with being copied around and
        // allocating voxels multiple times
        // Allocate a voxel set with the given size
        C_VoxelSetNew(
            ivec3 size,
            Color color = IRConstants::kColorGreen,
            int voxelPoolId = 0
        )
        :   numVoxels_{size.x * size.y * size.z}
        ,   size_{size}
        {
            auto voxels = IRECS::getSystem<IRECS::VOXEL_POOL>().allocateVoxels(
                size.x * size.y * size.z,
                voxelPoolId
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
                        // voxel.set(C_PositionGlobal3D{vec3(x, y, z)});
                        // voxel.set(C_Voxel{IRConstants::kColorGreen});
                    }
                }
            }
            IRProfile::engLogDebug("Allocated {} voxels", numVoxels_);
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
            IRECS::getSystem<IRECS::VOXEL_POOL>().deallocateVoxels(
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
        // todo: this is called by the user after all posible mutations
        // are defined such as what components are added to it
        void finalize(const EntityHandle& entity) {
            // freeClippedVoxels();
        }

        // TODO: get rid of all unneeded voxels
        void freeInvisableVoxels(bool withAnimation = false) {
            // Voxel pool will have to resort allocation and free
            // a whole chunk at a time
        }

        // int addVoxelSceneNode


        // private:
        //     void calcMaxSphereRadius() {
        //         o
        //     }

    };

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_SET_H */
