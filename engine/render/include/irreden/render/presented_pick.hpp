#ifndef PRESENTED_PICK_H
#define PRESENTED_PICK_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/ir_render_types.hpp>

#include <cstdint>

namespace IRRender {

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
    double cursorSampleSeconds_ = 0.0;
};

class PresentedPickHistory {
  public:
    void accept(const PresentedPickRecord &record) {
        if (!m_hasNewest || record.frameId_ > m_newest.frameId_) {
            if (m_hasNewest) {
                m_previous = m_newest;
                m_hasPrevious = true;
            }
            m_newest = record;
            m_hasNewest = true;
            return;
        }
        if (record.frameId_ == m_newest.frameId_) {
            m_newest = record;
            return;
        }
        if (!m_hasPrevious || record.frameId_ > m_previous.frameId_) {
            m_previous = record;
            m_hasPrevious = true;
        }
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

} // namespace IRRender

#endif /* PRESENTED_PICK_H */
