#ifndef IR_VOXEL_EDITOR_LOD_PREVIEW_H
#define IR_VOXEL_EDITOR_LOD_PREVIEW_H

#include "motion_preview.hpp"
#include <irreden/render/comparison_image.hpp>
#include <irreden/render/lod_utils.hpp>

#include <array>
#include <optional>

namespace IRVoxelEditor {

inline constexpr std::array<float, 5> kLodPreviewZooms{16, 8, 4, 2, 1};

struct LodCaptureRequest {
    IRRender::LodLevel tier_;
    float zoom_;
    IRMath::ivec2 size_;
    int magnification_;
};

// A fixed world aperture: the cropped pixel extent halves when zoom halves.
// Enlarge by the reciprocal to compare real extents, never occupied-bounds fits.
class LodCapturePlan {
  public:
    bool begin(IRMath::ivec2 finestPixels, bool captureClipped = false) {
        clear();
        if (finestPixels.x <= 0 || finestPixels.y <= 0 || finestPixels.x > 1024 ||
            finestPixels.y > 1024 || finestPixels.x % 16 != 0 || finestPixels.y % 16 != 0)
            return false;
        m_finestPixels = finestPixels;
        m_captureClipped = captureClipped;
        m_started = true;
        return true;
    }
    std::optional<LodCaptureRequest> request() const {
        if (!m_started || complete())
            return std::nullopt;
        const int factor = 1 << m_next;
        return LodCaptureRequest{
            static_cast<IRRender::LodLevel>(m_next),
            kLodPreviewZooms[m_next],
            m_finestPixels / factor,
            factor
        };
    }
    bool accept(IRRender::ComparisonImage image) {
        const auto next = request();
        if (!next || !image.valid() || image.size_ != next->size_)
            return false;
        m_images[m_next++] = std::move(image);
        return true;
    }
    bool complete() const {
        return m_started && m_next == m_images.size();
    }
    std::size_t captured() const {
        return m_next;
    }
    bool captureClipped() const {
        return m_captureClipped;
    }
    const std::array<IRRender::ComparisonImage, 5> &images() const {
        return m_images;
    }
    IRMath::ivec2 finestSize() const {
        return m_finestPixels;
    }
    void clear() {
        m_images = {};
        m_next = 0;
        m_started = false;
        m_captureClipped = false;
        m_finestPixels = IRMath::ivec2(0);
    }

  private:
    std::array<IRRender::ComparisonImage, 5> m_images;
    IRMath::ivec2 m_finestPixels{0};
    std::size_t m_next = 0;
    bool m_started = false;
    bool m_captureClipped = false;
};

// Rendering-only assembly clone. No module callbacks or motion are sampled.
// Its parts never join the authored EntityScene or editable-picking population.
class LodPreviewSession {
  public:
    EntitySceneResult begin(const EntityScene &source, IRMath::vec3 stageOffset) {
        if (active())
            return {false, "LOD comparison is already open."};
        const auto result = m_copy.begin(source, stageOffset);
        if (!result.ok_)
            return result;
        for (const auto &part : source.parts())
            m_parts.push_back(m_copy.part(part.id_));
        setTier(IRRender::LodLevel::LOD_0);
        return {true, {}};
    }
    void setTier(IRRender::LodLevel tier) {
        if (!active() || !IRRender::lodLevelFromIndex(static_cast<int>(tier)))
            return;
        IREntity::setComponent(root(), IRComponents::C_LodTierOverride{tier});
        for (const auto entity : m_parts)
            if (IREntity::entityExists(entity))
                IREntity::setComponent(entity, IRComponents::C_LodTierOverride{tier});
        m_tier = tier;
    }
    bool active() const {
        return m_copy.active();
    }
    IREntity::EntityId root() const {
        return m_copy.root();
    }
    IREntity::EntityId part(const std::string &id) const {
        return m_copy.part(id);
    }
    IRRender::LodLevel tier() const {
        return m_tier;
    }
    void stop() {
        m_copy.stop();
        m_parts.clear();
    }

  private:
    MotionPreview m_copy;
    std::vector<IREntity::EntityId> m_parts;
    IRRender::LodLevel m_tier = IRRender::LodLevel::LOD_0;
};

struct DerivedLodPart {
    RemovedEditorPart snapshot_;
    std::vector<IRComponents::C_Voxel> records_;
    bool overlapsSourceBand_ = false;
};

// A starting copy, not an automatic decimator. Parent/local pose, voxel bounds,
// anchor and records stay exact; only the new part's tier band changes.
inline std::optional<DerivedLodPart> deriveLodPart(
    const EntityScene &scene, const std::string &sourceId, IRRender::LodLevel targetTier
) {
    const auto *part = scene.findPart(sourceId);
    if (!part || !IRRender::lodLevelFromIndex(static_cast<int>(targetTier)))
        return std::nullopt;
    const auto source = scene.snapshotPart(part->entity_);
    if (!source)
        return std::nullopt;
    DerivedLodPart result{
        *source,
        {},
        !IRRender::shouldSkipAtLod(part->lodMin_, part->lodMax_, targetTier)
    };
    result.snapshot_.index_ = scene.parts().size();
    result.snapshot_.detachedChildren_.clear();
    auto &copy = result.snapshot_.part_;
    copy.entity_ = IREntity::kNullEntity;
    const auto base = sourceId + "_lod" + std::to_string(static_cast<int>(targetTier));
    copy.id_ = base;
    for (int suffix = 1; scene.findPart(copy.id_); ++suffix)
        copy.id_ = base + "_" + std::to_string(suffix);
    copy.groupId_ = 0;
    copy.rotationalOrder_ = 0;
    copy.lodMin_ = targetTier;
    copy.lodMax_ = targetTier;
    if (part->kind_ == EditorPartKind::VOXEL_SET) {
        const auto records =
            IREntity::getComponent<IRComponents::C_VoxelSetNew>(part->entity_).authoredRecords();
        result.records_.assign(records.begin(), records.end());
    }
    return result;
}

inline EntitySceneRestoreResult restoreDerivedLodPart(
    EntityScene &scene, IRScript::LuaScript &script, const DerivedLodPart &derived
) {
    if (scene.findPart(derived.snapshot_.part_.id_))
        return {IREntity::kNullEntity, "The derived part id already exists."};
    auto result = scene.restorePart(script, derived.snapshot_);
    if (!result.ok() || derived.records_.empty())
        return result;
    auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(result.entity_);
    auto records = set.mutableAuthoredRecords();
    if (records.size() != derived.records_.size()) {
        scene.removeParts({result.entity_});
        return {IREntity::kNullEntity, "Not enough voxel storage to derive the LOD part."};
    }
    std::copy(derived.records_.begin(), derived.records_.end(), records.begin());
    set.resyncAfterAuthoredEdits();
    return result;
}

} // namespace IRVoxelEditor
#endif /* IR_VOXEL_EDITOR_LOD_PREVIEW_H */
