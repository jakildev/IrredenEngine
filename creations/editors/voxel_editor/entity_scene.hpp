#ifndef IR_VOXEL_EDITOR_ENTITY_SCENE_H
#define IR_VOXEL_EDITOR_ENTITY_SCENE_H

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/gizmo.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/utility/path_utils.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/dense_bridge.hpp>

#include "component_records.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <iterator>
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
    IRMath::ivec2 canvasSize_{0};
    IRRender::LodLevel lodMin_ = IRRender::LodLevel::LOD_4;
    IRRender::LodLevel lodMax_ = IRRender::LodLevel::LOD_0;
    bool resident_ = false;
    std::vector<ComponentRecord> components_;
    int groupId_ = 0;
    IRMath::vec3 groupAxis_ = IRMath::vec3(0.0f, 0.0f, 1.0f);
    int rotationalOrder_ = 0;
};

// The root as a component-attach target; a part is its index.
constexpr int kEntitySceneRootTarget = -1;

struct EntitySceneResult {
    bool ok_ = false;
    std::string error_;
};

struct EntitySceneCloneResult {
    std::vector<IREntity::EntityId> entities_;
    std::string error_;
};

class EntityScene {
  public:
    bool active() const {
        return m_root != IREntity::kNullEntity;
    }

    IREntity::EntityId root() const {
        return m_root;
    }

    const std::vector<EditorPart> &parts() const {
        return m_parts;
    }

    int selectedIndex() const {
        return m_selected;
    }

    IREntity::EntityId targetEntity(int target) const {
        if (target == kEntitySceneRootTarget)
            return m_root;
        return target >= 0 && target < static_cast<int>(m_parts.size())
                   ? m_parts[static_cast<std::size_t>(target)].entity_
                   : IREntity::kNullEntity;
    }

    // Nullptr for a target that does not exist.
    std::vector<ComponentRecord> *targetComponents(int target) {
        if (target == kEntitySceneRootTarget)
            return active() ? &m_rootComponents : nullptr;
        return target >= 0 && target < static_cast<int>(m_parts.size())
                   ? &m_parts[static_cast<std::size_t>(target)].components_
                   : nullptr;
    }

    IREntity::EntityId selectedEntity() const {
        return m_selected >= 0 && m_selected < static_cast<int>(m_parts.size())
                   ? m_parts[static_cast<std::size_t>(m_selected)].entity_
                   : IREntity::kNullEntity;
    }

    void begin() {
        clear();
        m_root = createRoot();
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

    // Sets the part's inclusive band and mirrors it onto the part's own
    // C_VoxelSetNew / C_ShapeDescriptor, so the engine's LOD gate previews the
    // band. Requires fine <= coarse: the manifest reader rejects an inverted band.
    void setPartBand(int index, IRRender::LodLevel fine, IRRender::LodLevel coarse) {
        EditorPart &part = m_parts[static_cast<std::size_t>(index)];
        part.lodMax_ = fine;
        part.lodMin_ = coarse;
        applyBand(part);
    }

    // Pins every scene entity to @p tier, or with nullopt removes the pin so
    // they follow the camera-zoom tier again. Callable from a system tick: the
    // pins are staged and land at the next structural flush. Parts added or
    // loaded later take the same state.
    void setTierOverride(std::optional<IRRender::LodLevel> tier) {
        m_tierOverride = tier;
        if (!active()) {
            return;
        }
        stageTierOverride(m_root);
        for (const EditorPart &part : m_parts) {
            stageTierOverride(part.entity_);
        }
    }

    std::optional<IRRender::LodLevel> tierOverride() const {
        return m_tierOverride;
    }

    EntitySceneCloneResult cloneSelected(
        IRScript::LuaScript &script,
        const std::vector<IRComponents::C_LocalTransform> &offsets,
        IRMath::vec3 groupAxis,
        int rotationalOrder
    ) {
        EntitySceneCloneResult result;
        if (selectedEntity() == IREntity::kNullEntity || offsets.empty()) {
            return result;
        }

        const EditorPart source = m_parts[static_cast<std::size_t>(m_selected)];
        const auto sourceTransform =
            IREntity::getComponent<IRComponents::C_LocalTransform>(source.entity_);
        const int groupId = m_nextGroupId++;
        result.entities_.reserve(offsets.size());
        std::vector<EditorPart> clones;
        clones.reserve(offsets.size());
        for (const IRComponents::C_LocalTransform &offset : offsets) {
            IRComponents::C_LocalTransform transform = sourceTransform;
            transform.translation_ += offset.translation_;
            transform.rotation_ = IRMath::quatMul(offset.rotation_, sourceTransform.rotation_);
            const std::string id = "part_" + std::to_string(m_nextPartId++);
            IREntity::EntityId entity = IREntity::kNullEntity;
            if (source.kind_ == EditorPartKind::SHAPE) {
                const auto shape =
                    IREntity::getComponent<IRComponents::C_ShapeDescriptor>(source.entity_);
                entity = IREntity::createEntity(
                    transform,
                    shape,
                    IRComponents::C_RotationMode{source.mode_}
                );
            } else {
                const auto &set =
                    IREntity::getComponent<IRComponents::C_VoxelSetNew>(source.entity_);
                entity = IREntity::createEntity(
                    transform,
                    IRPrefab::DenseVoxel::toComponent(IRPrefab::DenseVoxel::fromComponent(set)),
                    IRComponents::C_RotationMode{source.mode_}
                );
            }
            IREntity::setParent(entity, m_root);
            EditorPart clone{
                entity,
                id,
                source.kind_,
                source.mode_,
                source.canvasSize_,
                source.lodMin_,
                source.lodMax_,
                source.resident_,
                source.components_,
                groupId,
                groupAxis,
                rotationalOrder
            };
            applyBand(clone);
            applyTierOverride(entity);
            for (ComponentRecord &component : clone.components_) {
                if (const auto error = applyComponentRecord(script, entity, component)) {
                    IREntity::destroyEntity(entity);
                    for (IREntity::EntityId created : result.entities_) {
                        IREntity::destroyEntity(created);
                    }
                    result.entities_.clear();
                    result.error_ = *error;
                    return result;
                }
            }
            clones.push_back(std::move(clone));
            result.entities_.push_back(entity);
        }
        m_parts.insert(
            m_parts.end(),
            std::make_move_iterator(clones.begin()),
            std::make_move_iterator(clones.end())
        );
        select(static_cast<int>(m_parts.size()) - 1, false);
        return result;
    }

    void removeParts(const std::vector<IREntity::EntityId> &entities) {
        destroySelectionGizmos();
        for (IREntity::EntityId entity : entities) {
            if (IREntity::entityExists(entity)) {
                IREntity::destroyEntity(entity);
            }
        }
        m_parts.erase(
            std::remove_if(
                m_parts.begin(),
                m_parts.end(),
                [&](const EditorPart &part) {
                    return std::find(entities.begin(), entities.end(), part.entity_) !=
                           entities.end();
                }
            ),
            m_parts.end()
        );
        m_selected = m_parts.empty()
                         ? -1
                         : IRMath::clamp(m_selected, 0, static_cast<int>(m_parts.size()) - 1);
    }

    IREntity::EntityId select(int index, bool createGizmos = true) {
        destroySelectionGizmos();
        if (m_parts.empty()) {
            m_selected = -1;
            return IREntity::kNullEntity;
        }
        m_selected = IRMath::clamp(index, 0, static_cast<int>(m_parts.size()) - 1);
        const IREntity::EntityId selected = selectedEntity();
        if (createGizmos) {
            IRPrefab::Gizmo::createTranslateGizmoForAnchor(selected);
            IRPrefab::Gizmo::createRotateGizmoForAnchor(selected);
        }
        return selected;
    }

    void clear() {
        if (m_root != IREntity::kNullEntity && IREntity::entityExists(m_root)) {
            IREntity::destroyTree(m_root);
        }
        m_root = IREntity::kNullEntity;
        m_rootComponents.clear();
        m_parts.clear();
        m_selected = -1;
        m_nextPartId = 0;
        m_nextGroupId = 1;
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
        description.components_ = describeComponents(m_rootComponents);
        description.parts_.reserve(m_parts.size());
        for (const EditorPart &editorPart : m_parts) {
            IRPrefab::Prefab::PrefabPartDescription part;
            part.id_ = editorPart.id_;
            part.transform_ =
                IREntity::getComponent<IRComponents::C_LocalTransform>(editorPart.entity_);
            part.rotationMode_ = editorPart.mode_;
            part.canvasSize_ = editorPart.canvasSize_;
            part.lodMin_ = editorPart.lodMin_;
            part.lodMax_ = editorPart.lodMax_;
            part.resident_ = editorPart.resident_;
            part.components_ = describeComponents(editorPart.components_);

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
                const std::string filename = baseName + "_" + editorPart.id_ + ".vxs";
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

    // A failed load leaves the live scene as it was.
    EntitySceneResult
    load(IRScript::LuaScript &script, const std::string &dir, const std::string &baseName) {
        const std::string manifestPath = IRUtility::joinPath(dir, baseName, ".prefab.lua");
        IRPrefab::Prefab::ManifestResult manifest =
            IRPrefab::Prefab::readManifest(script, manifestPath);
        if (!manifest.ok()) {
            return {false, manifest.error_};
        }

        std::vector<std::optional<IRAsset::DenseVoxelSet>> stagedVoxelSets;
        stagedVoxelSets.reserve(manifest.description_->parts_.size());
        for (const IRPrefab::Prefab::PrefabPartDescription &description :
             manifest.description_->parts_) {
            if (description.shape_) {
                stagedVoxelSets.emplace_back(std::nullopt);
                continue;
            }
            if (description.voxelRef_.empty()) {
                return {false, "part '" + description.id_ + "' needs voxel_ref or shape"};
            }
            auto loaded = IRAsset::loadDenseVoxelSet(description.voxelRef_);
            if (!loaded.ok()) {
                return {false, "failed to load voxel part '" + description.id_ + "'"};
            }
            stagedVoxelSets.emplace_back(std::move(loaded.value_.dense_));
        }

        // The replacement is built beside the live scene and takes its place
        // only once every component factory has run: reading the manifest
        // proves each factory exists, not that it accepts its fields.
        const IREntity::EntityId stagedRoot = createRoot();
        std::vector<EditorPart> stagedParts;
        stagedParts.reserve(manifest.description_->parts_.size());
        int stagedNextPartId = 0;
        for (std::size_t i = 0; i < manifest.description_->parts_.size(); ++i) {
            const IRPrefab::Prefab::PrefabPartDescription &description =
                manifest.description_->parts_[i];
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
                entity = IREntity::createEntity(
                    description.transform_,
                    IRPrefab::DenseVoxel::toComponent(*stagedVoxelSets[i]),
                    IRComponents::C_RotationMode{description.rotationMode_}
                );
            }
            IREntity::setParent(entity, stagedRoot);
            stagedParts.push_back(
                EditorPart{
                    entity,
                    description.id_,
                    kind,
                    description.rotationMode_,
                    description.canvasSize_,
                    description.lodMin_,
                    description.lodMax_,
                    description.resident_
                }
            );
            applyBand(stagedParts.back());
            applyTierOverride(entity);
            const std::string prefix = "part_";
            if (description.id_.starts_with(prefix)) {
                const std::string_view suffix =
                    std::string_view(description.id_).substr(prefix.size());
                int numericId = 0;
                const auto parsed =
                    std::from_chars(suffix.data(), suffix.data() + suffix.size(), numericId);
                if (parsed.ec == std::errc{} && parsed.ptr == suffix.data() + suffix.size()) {
                    stagedNextPartId = IRMath::max(stagedNextPartId, numericId + 1);
                }
            }
        }

        std::string componentErrors;
        std::vector<ComponentRecord> stagedRootComponents = restoreComponents(
            script,
            stagedRoot,
            manifest.description_->components_,
            componentErrors
        );
        for (std::size_t i = 0; i < stagedParts.size(); ++i) {
            stagedParts[i].components_ = restoreComponents(
                script,
                stagedParts[i].entity_,
                manifest.description_->parts_[i].components_,
                componentErrors
            );
        }
        if (!componentErrors.empty()) {
            IREntity::destroyTree(stagedRoot);
            return {false, componentErrors};
        }

        clear();
        m_root = stagedRoot;
        m_rootComponents = std::move(stagedRootComponents);
        m_parts = std::move(stagedParts);
        m_nextPartId = stagedNextPartId;
        select(0, false);
        return {true, {}};
    }

  private:
    static std::vector<IRPrefab::Prefab::PrefabComponentDescription>
    describeComponents(const std::vector<ComponentRecord> &records) {
        std::vector<IRPrefab::Prefab::PrefabComponentDescription> described;
        described.reserve(records.size());
        for (const ComponentRecord &record : records)
            described.push_back({record.name_, componentLiteral(record)});
        return described;
    }

    // Applies @p described to @p entity and returns the records that attached;
    // each one that did not appends its reason to @p errors.
    static std::vector<ComponentRecord> restoreComponents(
        IRScript::LuaScript &script,
        IREntity::EntityId entity,
        const std::vector<IRPrefab::Prefab::PrefabComponentDescription> &described,
        std::string &errors
    ) {
        std::vector<ComponentRecord> records;
        records.reserve(described.size());
        for (const IRPrefab::Prefab::PrefabComponentDescription &component : described) {
            ComponentRecord record = makeComponentRecord(script, component.name_);
            if (auto error = applyComponentLiteral(script, entity, record, component.fields_)) {
                errors += (errors.empty() ? "" : "; ") + *error;
                continue;
            }
            records.push_back(std::move(record));
        }
        return records;
    }

    IREntity::EntityId createRoot() const {
        const IREntity::EntityId root =
            IREntity::createEntity(IRComponents::C_LocalTransform{IRMath::vec3(0.0f)});
        applyTierOverride(root);
        return root;
    }

    IREntity::EntityId appendPart(IREntity::EntityId entity, std::string id, EditorPartKind kind) {
        IREntity::setParent(entity, m_root);
        m_parts.push_back(EditorPart{entity, std::move(id), kind});
        applyTierOverride(entity);
        select(static_cast<int>(m_parts.size()) - 1, false);
        return entity;
    }

    static void applyBand(const EditorPart &part) {
        if (part.kind_ == EditorPartKind::SHAPE) {
            auto &shape = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(part.entity_);
            shape.lodMin_ = part.lodMin_;
            shape.lodMax_ = part.lodMax_;
            return;
        }
        auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(part.entity_);
        set.lodMin_ = part.lodMin_;
        set.lodMax_ = part.lodMax_;
    }

    void applyTierOverride(IREntity::EntityId entity) const {
        if (m_tierOverride) {
            IREntity::setComponent(entity, IRComponents::C_LodTierOverride{*m_tierOverride});
        } else if (IREntity::getComponentOptional<IRComponents::C_LodTierOverride>(entity)) {
            IREntity::removeComponent<IRComponents::C_LodTierOverride>(entity);
        }
    }

    // A flush drains removals before sets, so stage at most one change per
    // entity between flushes.
    void stageTierOverride(IREntity::EntityId entity) const {
        if (m_tierOverride) {
            IREntity::setComponentDeferred(
                entity,
                IRComponents::C_LodTierOverride{*m_tierOverride}
            );
        } else {
            IREntity::removeComponentDeferred<IRComponents::C_LodTierOverride>(entity);
        }
    }

    void destroySelectionGizmos() const {
        if (selectedEntity() == IREntity::kNullEntity) {
            return;
        }
        IRPrefab::Gizmo::destroyForAnchor(selectedEntity());
    }

    IREntity::EntityId m_root = IREntity::kNullEntity;
    std::vector<ComponentRecord> m_rootComponents;
    std::vector<EditorPart> m_parts;
    int m_selected = -1;
    int m_nextPartId = 0;
    std::optional<IRRender::LodLevel> m_tierOverride;
    int m_nextGroupId = 1;
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_ENTITY_SCENE_H */
