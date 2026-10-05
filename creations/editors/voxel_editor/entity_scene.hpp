#ifndef IR_VOXEL_EDITOR_ENTITY_SCENE_H
#define IR_VOXEL_EDITOR_ENTITY_SCENE_H

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/gizmo.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/utility/path_utils.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/dense_bridge.hpp>

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IRVoxelEditor {

enum class EditorPartKind { VOXEL_SET, SHAPE };

struct EditorPart {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    std::string id_;
    EditorPartKind kind_ = EditorPartKind::VOXEL_SET;
    IRComponents::RotationMode mode_ = IRComponents::RotationMode::GRID;
    IRRender::LodLevel lodMin_ = IRRender::LodLevel::LOD_4;
    IRRender::LodLevel lodMax_ = IRRender::LodLevel::LOD_0;
    std::vector<std::string> components_;
};

struct EntitySceneResult {
    bool ok_ = false;
    std::string error_;
};

class EntityScene {
  public:
    bool active() const {
        return root_ != IREntity::kNullEntity;
    }

    IREntity::EntityId root() const {
        return root_;
    }

    const std::vector<EditorPart> &parts() const {
        return parts_;
    }

    int selectedIndex() const {
        return selected_;
    }

    IREntity::EntityId selectedEntity() const {
        return selected_ >= 0 && selected_ < static_cast<int>(parts_.size())
                   ? parts_[static_cast<std::size_t>(selected_)].entity_
                   : IREntity::kNullEntity;
    }

    void begin() {
        clear();
        root_ = IREntity::createEntity(IRComponents::C_LocalTransform{IRMath::vec3(0.0f)});
    }

    IREntity::EntityId addVoxelPart(
        IRMath::ivec3 size,
        IRMath::vec3 translation,
        IRMath::Color color = IRMath::Color{200, 200, 210, 255}
    ) {
        if (!active()) {
            begin();
        }
        const std::string id = "part_" + std::to_string(m_nextPartId++);
        const IREntity::EntityId entity = IREntity::createEntity(
            IRComponents::C_LocalTransform{translation},
            IRComponents::C_VoxelSetNew{size, color},
            IRComponents::C_RotationMode{IRComponents::RotationMode::GRID}
        );
        return appendPart(entity, id, EditorPartKind::VOXEL_SET);
    }

    IREntity::EntityId addShapePart(
        const IRPrefab::Prefab::PrefabShapeDescription &shape,
        const IRComponents::C_LocalTransform &transform
    ) {
        if (!active()) {
            begin();
        }
        const std::string id = "part_" + std::to_string(m_nextPartId++);
        IRComponents::C_ShapeDescriptor descriptor{shape.type_, shape.params_, shape.color_};
        descriptor.flags_ = shape.flags_;
        const IREntity::EntityId entity = IREntity::createEntity(
            transform,
            descriptor,
            IRComponents::C_RotationMode{IRComponents::RotationMode::GRID}
        );
        return appendPart(entity, id, EditorPartKind::SHAPE);
    }

    IREntity::EntityId select(int index, bool createGizmos = true) {
        destroySelectionGizmos();
        if (parts_.empty()) {
            selected_ = -1;
            return IREntity::kNullEntity;
        }
        selected_ = IRMath::clamp(index, 0, static_cast<int>(parts_.size()) - 1);
        const IREntity::EntityId selected = selectedEntity();
        if (createGizmos) {
            IRPrefab::Gizmo::createTranslateGizmoForAnchor(selected);
            IRPrefab::Gizmo::createRotateGizmoForAnchor(selected);
        }
        return selected;
    }

    void clear() {
        if (root_ != IREntity::kNullEntity && IREntity::entityExists(root_)) {
            IREntity::destroyTree(root_);
        }
        root_ = IREntity::kNullEntity;
        parts_.clear();
        selected_ = -1;
        m_nextPartId = 0;
    }

    EntitySceneResult save(const std::string &dir, const std::string &baseName) const {
        if (!active()) {
            return {false, "entity scene is not active"};
        }
        std::error_code directoryError;
        std::filesystem::create_directories(dir, directoryError);
        if (directoryError) {
            return {false, "could not create '" + dir + "': " + directoryError.message()};
        }

        IRPrefab::Prefab::PrefabDescription description;
        description.parts_.reserve(parts_.size());
        for (const EditorPart &editorPart : parts_) {
            IRPrefab::Prefab::PrefabPartDescription part;
            part.id_ = editorPart.id_;
            part.transform_ =
                IREntity::getComponent<IRComponents::C_LocalTransform>(editorPart.entity_);
            part.rotationMode_ = editorPart.mode_;
            part.lodMin_ = editorPart.lodMin_;
            part.lodMax_ = editorPart.lodMax_;

            if (editorPart.kind_ == EditorPartKind::SHAPE) {
                const auto &shape =
                    IREntity::getComponent<IRComponents::C_ShapeDescriptor>(editorPart.entity_);
                part.shape_ = IRPrefab::Prefab::PrefabShapeDescription{
                    shape.shapeType_,
                    shape.params_,
                    shape.color_,
                    shape.flags_
                };
            } else {
                const auto &set =
                    IREntity::getComponent<IRComponents::C_VoxelSetNew>(editorPart.entity_);
                IRAsset::DenseVoxelSet dense = IRPrefab::DenseVoxel::fromComponent(set);
                const std::string filename = baseName + "_part_" + editorPart.id_ + ".vxs";
                const std::string voxelPath = IRUtility::joinPath(dir, filename, "");
                if (const IRAsset::BinaryStatus status =
                        IRAsset::saveDenseVoxelSet(voxelPath, dense);
                    !status.ok()) {
                    return {false, "failed to save voxel part '" + editorPart.id_ + "'"};
                }
                part.voxelRef_ = voxelPath;
            }
            description.parts_.push_back(std::move(part));
        }

        const std::string manifestPath = IRUtility::joinPath(dir, baseName, ".prefab.lua");
        if (const std::optional<std::string> error =
                IRPrefab::Prefab::writeManifest(manifestPath, description)) {
            return {false, *error};
        }
        return {true, {}};
    }

    EntitySceneResult
    load(IRScript::LuaScript &script, const std::string &dir, const std::string &baseName) {
        const std::string manifestPath = IRUtility::joinPath(dir, baseName, ".prefab.lua");
        IRPrefab::Prefab::ManifestResult manifest =
            IRPrefab::Prefab::readManifest(script, manifestPath);
        if (!manifest.ok()) {
            return {false, manifest.error_};
        }

        clear();
        root_ = IREntity::createEntity(IRComponents::C_LocalTransform{IRMath::vec3(0.0f)});
        for (const IRPrefab::Prefab::PrefabPartDescription &description :
             manifest.description_->parts_) {
            IREntity::EntityId entity = IREntity::kNullEntity;
            EditorPartKind kind = EditorPartKind::VOXEL_SET;
            if (description.shape_) {
                IRComponents::C_ShapeDescriptor shape{
                    description.shape_->type_,
                    description.shape_->params_,
                    description.shape_->color_
                };
                shape.flags_ = description.shape_->flags_;
                entity = IREntity::createEntity(
                    description.transform_,
                    shape,
                    IRComponents::C_RotationMode{description.rotationMode_}
                );
                kind = EditorPartKind::SHAPE;
            } else {
                auto loaded = IRAsset::loadDenseVoxelSet(description.voxelRef_);
                if (!loaded.ok()) {
                    clear();
                    return {false, "failed to load voxel part '" + description.id_ + "'"};
                }
                entity = IREntity::createEntity(
                    description.transform_,
                    IRPrefab::DenseVoxel::toComponent(loaded.value_.dense_),
                    IRComponents::C_RotationMode{description.rotationMode_}
                );
            }
            IREntity::setParent(entity, root_);
            parts_.push_back(
                EditorPart{
                    entity,
                    description.id_,
                    kind,
                    description.rotationMode_,
                    description.lodMin_,
                    description.lodMax_
                }
            );
            const std::string prefix = "part_";
            if (description.id_.starts_with(prefix)) {
                const std::string_view suffix =
                    std::string_view(description.id_).substr(prefix.size());
                int numericId = 0;
                const auto parsed =
                    std::from_chars(suffix.data(), suffix.data() + suffix.size(), numericId);
                if (parsed.ec == std::errc{} && parsed.ptr == suffix.data() + suffix.size()) {
                    m_nextPartId = IRMath::max(m_nextPartId, numericId + 1);
                }
            }
        }
        select(0, false);
        return {true, {}};
    }

  private:
    IREntity::EntityId appendPart(IREntity::EntityId entity, std::string id, EditorPartKind kind) {
        IREntity::setParent(entity, root_);
        parts_.push_back(EditorPart{entity, std::move(id), kind});
        select(static_cast<int>(parts_.size()) - 1, false);
        return entity;
    }

    void destroySelectionGizmos() const {
        if (selectedEntity() == IREntity::kNullEntity) {
            return;
        }
        IRPrefab::Gizmo::destroyForAnchor(selectedEntity());
    }

    IREntity::EntityId root_ = IREntity::kNullEntity;
    std::vector<EditorPart> parts_;
    int selected_ = -1;
    int m_nextPartId = 0;
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_ENTITY_SCENE_H */
