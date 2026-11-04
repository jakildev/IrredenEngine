#ifndef PART_EDIT_HISTORY_H
#define PART_EDIT_HISTORY_H

#include "entity_scene.hpp"

#include <irreden/common/components/component_local_transform.hpp>

#include <optional>
#include <string>
#include <utility>

namespace IRVoxelEditor {

struct PartLodBand {
    IRRender::LodLevel fine_ = IRRender::LodLevel::LOD_0;
    IRRender::LodLevel coarse_ = IRRender::LodLevel::LOD_4;
};

template <typename Value> struct PartEdit {
    std::string partId_;
    Value before_;
    Value after_;
};

using PartTransformEdit = PartEdit<IRComponents::C_LocalTransform>;
using PartLodBandEdit = PartEdit<PartLodBand>;

namespace detail {

inline bool samePartEditValue(
    const IRComponents::C_LocalTransform &left, const IRComponents::C_LocalTransform &right
) {
    return left.translation_ == right.translation_ && left.rotation_ == right.rotation_ &&
           left.scale_ == right.scale_ && left.unbounded_ == right.unbounded_;
}

inline bool samePartEditValue(const PartLodBand &left, const PartLodBand &right) {
    return left.fine_ == right.fine_ && left.coarse_ == right.coarse_;
}

} // namespace detail

// Begin before the first mutation and finish after the release-frame mutation.
// Cancel discards the pending record; restoring an interrupted edit is explicit.
template <typename Value> class PartEditGesture {
  public:
    bool begin(const std::string &partId, const Value &before) {
        if (m_pending) {
            return false;
        }
        m_pending = PartEdit<Value>{partId, before, before};
        return true;
    }

    std::optional<PartEdit<Value>> finish(const std::optional<Value> &after) {
        std::optional<PartEdit<Value>> completed = std::move(m_pending);
        m_pending.reset();
        if (!completed || !after || detail::samePartEditValue(completed->before_, *after)) {
            return std::nullopt;
        }
        completed->after_ = *after;
        return completed;
    }

    void cancel() {
        m_pending.reset();
    }

    bool active() const {
        return m_pending.has_value();
    }

    const PartEdit<Value> *pending() const {
        return m_pending ? &*m_pending : nullptr;
    }

  private:
    std::optional<PartEdit<Value>> m_pending;
};

using PartTransformGesture = PartEditGesture<IRComponents::C_LocalTransform>;
using PartLodBandGesture = PartEditGesture<PartLodBand>;

inline std::optional<IRComponents::C_LocalTransform>
snapshotPartTransform(const EntityScene &scene, const std::string &partId) {
    const EditorPart *part = scene.findPart(partId);
    if (!part) {
        return std::nullopt;
    }
    return IREntity::getComponent<IRComponents::C_LocalTransform>(part->entity_);
}

inline bool applyPartTransform(
    EntityScene &scene, const std::string &partId, const IRComponents::C_LocalTransform &value
) {
    const EditorPart *part = scene.findPart(partId);
    if (!part) {
        return false;
    }
    IREntity::getComponent<IRComponents::C_LocalTransform>(part->entity_) = value;
    return true;
}

inline std::optional<PartLodBand>
snapshotPartLodBand(const EntityScene &scene, const std::string &partId) {
    const EditorPart *part = scene.findPart(partId);
    if (!part) {
        return std::nullopt;
    }
    return PartLodBand{part->lodMax_, part->lodMin_};
}

inline bool
applyPartLodBand(EntityScene &scene, const std::string &partId, const PartLodBand &value) {
    const EditorPart *part = scene.findPart(partId);
    if (!part) {
        return false;
    }
    const int index = static_cast<int>(part - scene.parts().data());
    scene.setPartBand(index, value.fine_, value.coarse_);
    return true;
}

} // namespace IRVoxelEditor

#endif /* PART_EDIT_HISTORY_H */
