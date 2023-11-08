#ifndef COMPONENT_VOXEL_SCENE_NODE_H
#define COMPONENT_VOXEL_SCENE_NODE_H


// A node should be "first child"
struct C_VoxelSceneNode {
        EntityHandle self_{0};
        EntityId firstChild_{0};
        EntityId numChildren_{0};
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

#endif /* COMPONENT_VOXEL_SCENE_NODE_H */
