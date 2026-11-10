#ifndef IR_VOXEL_EDITOR_LOCAL_PART_EDIT_H
#define IR_VOXEL_EDITOR_LOCAL_PART_EDIT_H

#include "entity_scene.hpp"

#include <irreden/ir_render.hpp>
#include <irreden/render/camera.hpp>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kLocalEditButtonPos{650, 60};
constexpr IRMath::ivec2 kLocalEditButtonSize{100, 18};

struct EditorStageView {
    IRMath::vec2 cameraIso_{};
    float zoom_ = 1.0f;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    IRRender::RotationPivotMode pivotMode_ = IRRender::RotationPivotMode::CAMERA_CENTER;
    std::optional<IRMath::vec3> pivotFocus_;

    void capture() {
        cameraIso_ = IRRender::getCameraPosition2DIso();
        zoom_ = IRRender::getCameraZoom().x;
        yaw_ = IRPrefab::Camera::getYaw();
        pitch_ = IRPrefab::Camera::getPitch();
        pivotMode_ = IRRender::getRotationPivotMode();
        pivotFocus_ = IRRender::hasRotationPivotFocus()
                          ? std::optional{IRRender::getRotationPivotFocus()}
                          : std::nullopt;
    }

    void focus(IRMath::vec3 center) const {
        IRRender::setRotationPivotMode(IRRender::RotationPivotMode::CAMERA_CENTER);
        IRRender::setRotationPivotFocus(center);
        IRRender::setCameraVisualYaw(0.0f);
        const auto pivotOffset =
            IRRender::getEffectiveCameraIso() - IRRender::getCameraPosition2DIso();
        IRRender::setCameraPosition2DIso(-IRMath::pos3DtoPos2DIso(center) - pivotOffset);
        IRRender::setCameraZoom(4.0f);
    }

    void restore() const {
        IRPrefab::Camera::setYawPitch(yaw_, pitch_);
        IRRender::setRotationPivotMode(pivotMode_);
        if (pivotFocus_)
            IRRender::setRotationPivotFocus(*pivotFocus_);
        else
            IRRender::clearRotationPivotFocus();
        IRRender::setCameraPosition2DIso(cameraIso_);
        IRRender::setCameraZoom(zoom_);
    }
};

// A distant stage keeps scene content out of the local view without changing
// authored transforms, visibility, or render modes, including SDF parents.
inline IRMath::vec3 localEditStageOrigin(const EntityScene &scene, IRMath::ivec3 size) {
    float radius = IRMath::length(IRMath::vec3(size)) + 64.0f;
    for (const EditorPart &part : scene.parts()) {
        const auto world =
            IREntity::getComponentOptional<IRComponents::C_WorldTransform>(part.entity_);
        const auto &local = IREntity::getComponent<IRComponents::C_LocalTransform>(part.entity_);
        const IRMath::vec3 position = world ? (*world)->translation_ : local.translation_;
        const IRMath::vec3 scale = world ? (*world)->scale_ : local.scale_;
        IRMath::vec3 extent(16.0f);
        if (const auto set =
                IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(part.entity_))
            extent = IRMath::vec3((*set)->size_);
        else if (
            const auto shape =
                IREntity::getComponentOptional<IRComponents::C_ShapeDescriptor>(part.entity_)
        )
            extent = IRMath::vec3((*shape)->params_) * 2.0f;
        radius = IRMath::max(radius, IRMath::length(position) + IRMath::length(extent * scale));
    }
    return IRMath::vec3(IRMath::ceil(radius * 4.0f), 0.0f, 0.0f);
}

// The proxy is presentation only. Source ids, hierarchy and serialized parts
// remain owned by EntityScene; history always records source entities.
class LocalPartEditSession {
  public:
    bool begin(EntityScene &scene, const std::string &partId, IRMath::vec3 origin) {
        const EditorPart *part = scene.findPart(partId);
        if (!part || part->kind_ != EditorPartKind::VOXEL_SET)
            return false;
        const auto &source = IREntity::getComponent<IRComponents::C_VoxelSetNew>(part->entity_);
        const auto dense = IRPrefab::DenseVoxel::fromComponent(source);
        const auto anchor = source.anchor_;
        const auto size = source.size_;
        if (dense.voxels_.empty())
            return false;
        close();
        m_partId = partId;
        m_origin = origin;
        m_proxy = IREntity::createEntity(
            IRComponents::C_LocalTransform{origin - IRComponents::anchorOffset(anchor, size)},
            IRPrefab::DenseVoxel::toComponent(dense, anchor),
            IRComponents::C_RotationMode{IRComponents::RotationMode::GRID},
            IRComponents::C_LodTierOverride{IRRender::LodLevel::LOD_0}
        );
        // Runtime priority and other authored carriers are not serialized by
        // DenseVoxel; seed the view from the complete source records as well.
        if (!refresh(scene)) {
            close();
            return false;
        }
        return true;
    }

    bool active() const {
        return m_proxy != IREntity::kNullEntity;
    }
    IREntity::EntityId proxy() const {
        return m_proxy;
    }
    const std::string &partId() const {
        return m_partId;
    }
    IRMath::vec3 origin() const {
        return m_origin;
    }

    IREntity::EntityId source(const EntityScene &scene) const {
        const auto *part = active() ? scene.findPart(m_partId) : nullptr;
        return part ? part->entity_ : IREntity::kNullEntity;
    }

    IREntity::EntityId canonical(const EntityScene &scene, IREntity::EntityId entity) const {
        return active() && entity == m_proxy ? source(scene) : entity;
    }

    bool refresh(const EntityScene &scene) {
        const auto entity = source(scene);
        if (entity == IREntity::kNullEntity || !IREntity::entityExists(m_proxy))
            return false;
        const auto &original = IREntity::getComponent<IRComponents::C_VoxelSetNew>(entity);
        auto &proxySet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(m_proxy);
        const auto records = original.authoredRecords();
        auto destination = proxySet.mutableAuthoredRecords();
        if (original.size_ != proxySet.size_ || records.size() != destination.size())
            return false;
        std::copy(records.begin(), records.end(), destination.begin());
        proxySet.resyncAfterAuthoredEdits();
        return true;
    }

    void mirrorCell(
        const EntityScene &scene,
        IREntity::EntityId entity,
        std::size_t flat,
        const IRComponents::C_Voxel &voxel
    ) {
        if (!active() || entity != source(scene))
            return;
        auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(m_proxy);
        auto records = set.mutableAuthoredRecords();
        if (flat < records.size())
            records[flat] = voxel;
    }

    void close() {
        if (m_proxy != IREntity::kNullEntity && IREntity::entityExists(m_proxy))
            IREntity::destroyEntity(m_proxy);
        m_proxy = IREntity::kNullEntity;
        m_partId.clear();
    }

  private:
    IREntity::EntityId m_proxy = IREntity::kNullEntity;
    std::string m_partId;
    IRMath::vec3 m_origin{};
};

} // namespace IRVoxelEditor
#endif
