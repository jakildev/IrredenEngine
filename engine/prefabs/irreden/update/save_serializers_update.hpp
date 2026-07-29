#ifndef IR_SAVE_SERIALIZERS_UPDATE_H
#define IR_SAVE_SERIALIZERS_UPDATE_H

/// `SaveSerialize<C>` specialization for `engine/prefabs/irreden/update/`
/// (#2242). Opt-in serializer header: include it wherever a registry
/// registers `C_PeriodicIdle`; never pulled by the component header.
///
/// The two other heap-owning components in this domain — `C_GotoEasing3D` and
/// `C_RotationTarget` — are **not** here: both store a resolved
/// `GLMEasingFunction` (a `std::function`) rather than the
/// `IREasingFunctions` enum they were built from, so the authored curve is
/// unrecoverable at save time. They are opted OUT in
/// `save_component_inventory.hpp` alongside the other callback-bearing
/// components; see the note there.

#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/math_binary_io.hpp>

#include <cstdint>
#include <utility>

namespace IRWorld {

/// `PeriodStage` stores its easing as the `IREasingFunctions` **enum**, not a
/// resolved callback, so the stage list round-trips as plain records — the
/// difference that makes this component serializable where its
/// `C_GotoEasing3D` sibling is not.
///
/// The private cache (`m_angleIncrementPerTick`, `m_currentValue`,
/// `m_previousValue`) is derived, so `read` reconstructs through the
/// constructor — which computes the per-tick angle increment from
/// `periodLengthSeconds_` — and then re-evaluates the current value rather
/// than persisting either. `updateValue()` indexes `stages_`, so it only runs
/// once the restored stage list is known to cover the restored index; a set
/// with no stages keeps the constructor's zero value and self-heals on the
/// first `tick()`.
template <> struct SaveSerialize<IRComponents::C_PeriodicIdle> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_PeriodicIdle &value) {
        w.writeI32(value.tickCount_);
        w.writeF32(value.angle_);
        IRMath::BinaryIO::writeVec3(w, value.amplitude_);
        w.writeF32(value.periodLengthSeconds_);
        detail::writeTrivialVector(w, value.stages_);
        w.writeI32(value.currentStageIndex_);
        w.writeU8(value.cycleCompleted_ ? 1 : 0);
        w.writeU8(value.pauseRequested_ ? 1 : 0);
        w.writeU8(value.paused_ ? 1 : 0);
        w.writeF32(value.resumeCountdownSec_);
    }

    static IRAsset::Result<IRComponents::C_PeriodicIdle> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_PeriodicIdle>;

        std::int32_t tickCount = 0;
        float angle = 0.0f;
        IRMath::vec3 amplitude{0.0f};
        float periodLengthSeconds = 0.0f;

        IR_SAVE_READ(tickCount, r.readI32());
        IR_SAVE_READ(angle, r.readF32());
        IR_SAVE_READ(amplitude, IRMath::BinaryIO::readVec3(r));
        IR_SAVE_READ(periodLengthSeconds, r.readF32());

        // Constructed with the authored amplitude/period/offset so the
        // constructor derives m_angleIncrementPerTick from the same formula
        // the live component used.
        IRComponents::C_PeriodicIdle value{amplitude, periodLengthSeconds, angle};
        value.tickCount_ = tickCount;

        IR_SAVE_READ_STATUS(detail::readTrivialVector(r, value.stages_));
        IR_SAVE_READ(value.currentStageIndex_, r.readI32());
        IR_SAVE_READ_BOOL(value.cycleCompleted_, r.readU8());
        IR_SAVE_READ_BOOL(value.pauseRequested_, r.readU8());
        IR_SAVE_READ_BOOL(value.paused_, r.readU8());
        IR_SAVE_READ(value.resumeCountdownSec_, r.readF32());

        if (value.currentStageIndex_ >= 0 &&
            value.currentStageIndex_ < static_cast<int>(value.stages_.size())) {
            value.updateValue();
        }
        return Res::success(std::move(value));
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_UPDATE_H */
