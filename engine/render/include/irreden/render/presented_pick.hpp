#ifndef PRESENTED_PICK_H
#define PRESENTED_PICK_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/ir_render_types.hpp>

#include <cstddef>
#include <cstdint>

namespace IRRender {

class Buffer;

struct PresentedPickMapping {
    IRMath::vec2 windowPointScale_{1.0f};
    IRMath::vec2 framebufferOffset_{};
    IRMath::vec2 outputScale_{1.0f};
    IRMath::ivec2 resolution_{};

    IRMath::ivec2 pixel(IRMath::vec2 windowPoint) const {
        return IRMath::ivec2(
            IRMath::floor(
                (windowPoint * windowPointScale_ - framebufferOffset_) / outputScale_ +
                IRMath::vec2(resolution_) * 0.5f
            )
        );
    }

    std::size_t bufferBytes() const {
        return (1 + static_cast<std::size_t>(resolution_.x) * resolution_.y) *
               sizeof(HoveredEntityIdLayout);
    }
};

struct MouseTrixelPick {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    int priority_ = 0;
    int isoDepth_ = 0;
    std::uint64_t frameId_ = 0;
    IRMath::ivec2 renderedCursorPixel_{};
    bool stale_ = false;
};

struct PresentedPickRecord {
    HoveredEntityIdLayout layout_{};
    std::uint64_t frameId_ = 0;
    IRMath::ivec2 cursorPixel_{};
    PresentedPickMapping mapping_{};
    Buffer *pixels_ = nullptr;
};

class PresentedPickHistory {
  public:
    PresentedPickRecord accept(const PresentedPickRecord &record) {
        if (!m_hasNewest || record.frameId_ > m_newest.frameId_) {
            const PresentedPickRecord evicted = m_previous;
            if (m_hasNewest) {
                m_previous = m_newest;
                m_hasPrevious = true;
            }
            m_newest = record;
            m_hasNewest = true;
            return evicted;
        }
        if (record.frameId_ == m_newest.frameId_) {
            const PresentedPickRecord evicted = m_newest;
            m_newest = record;
            return evicted;
        }
        if (!m_hasPrevious || record.frameId_ > m_previous.frameId_) {
            const PresentedPickRecord evicted = m_previous;
            m_previous = record;
            m_hasPrevious = true;
            return evicted;
        }
        return record;
    }

    const PresentedPickRecord *served(bool servePrevious) const {
        if (servePrevious && m_hasPrevious) {
            return &m_previous;
        }
        return m_hasNewest ? &m_newest : nullptr;
    }

    std::uint64_t newestFrameId() const {
        return m_hasNewest ? m_newest.frameId_ : 0;
    }

  private:
    PresentedPickRecord m_newest{};
    PresentedPickRecord m_previous{};
    bool m_hasNewest = false;
    bool m_hasPrevious = false;
};

template <typename IsEntityLive>
MouseTrixelPick resolvePresentedPick(
    const PresentedPickRecord *record, IRMath::ivec2 currentCursorPixel, IsEntityLive isEntityLive
) {
    if (record == nullptr) {
        return {};
    }

    MouseTrixelPick result;
    result.frameId_ = record->frameId_;
    result.renderedCursorPixel_ = record->cursorPixel_;
    result.stale_ = record->cursorPixel_ != currentCursorPixel;

    const IREntity::EntityId entity =
        static_cast<IREntity::EntityId>(decodeCarrierEntityId(record->layout_.entityId_));
    if (entity == IREntity::kNullEntity || !isEntityLive(entity)) {
        return result;
    }

    const DecodedCompositeDepth depth =
        decodeCompositeDepth(compositeRawDepthFromNormalized(record->layout_.depth_));
    result.entity_ = entity;
    result.priority_ = depth.tier_;
    result.isoDepth_ = depth.iso_;
    return result;
}

template <typename ReadPixel, typename IsEntityLive>
MouseTrixelPick resolvePresentedPickAtCursor(
    const PresentedPickRecord *record,
    IRMath::vec2 windowPoint,
    ReadPixel readPixel,
    IsEntityLive isEntityLive
) {
    if (record == nullptr) {
        return {};
    }
    PresentedPickRecord addressed = *record;
    const IRMath::ivec2 pixel = record->mapping_.pixel(windowPoint);
    const IRMath::ivec2 size = record->mapping_.resolution_;
    addressed.cursorPixel_ = pixel;
    addressed.layout_ = {};
    if (pixel.x >= 0 && pixel.y >= 0 && pixel.x < size.x && pixel.y < size.y) {
        const std::size_t index =
            1 + static_cast<std::size_t>(size.y - 1 - pixel.y) * size.x + pixel.x;
        addressed.layout_ = readPixel(index);
    }
    return resolvePresentedPick(&addressed, pixel, isEntityLive);
}

} // namespace IRRender

#endif /* PRESENTED_PICK_H */
