#ifndef IR_SAVE_SERIALIZERS_COMMON_H
#define IR_SAVE_SERIALIZERS_COMMON_H

/// `SaveSerialize<C>` specializations for the string-bearing components in
/// `engine/prefabs/irreden/common/` (#2242). Each of these opts IN to the
/// world snapshot but owns a `std::string`, so the trivially-copyable arm of
/// `SaveSerialize` does not apply — a raw byte image would persist a dangling
/// pointer.
///
/// Same include contract as `voxel/voxel_set_serialize.hpp`: this is an
/// opt-in serializer header pulled by whoever builds a registry over these
/// components (`engine/world/src/world_default_registry.cpp`), never by the
/// component headers themselves, so `common/`'s render-neutral layering
/// holds.
///
/// Field coverage is the whole struct in every case here — these components
/// carry no derived or process-local state, so "authored truth" and "all
/// members" coincide. Where that stops being true for a component, the
/// specialization says so (see the render / voxel / audio serializer headers).

#include <irreden/common/components/component_cycle.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_stopwatch.hpp>
#include <irreden/common/components/component_timer.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>

#include <irreden/asset/binary_io.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace IRWorld {

template <> struct SaveSerialize<IRComponents::C_Name> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_Name &value) {
        w.writeString(value.name_);
    }

    static IRAsset::Result<IRComponents::C_Name> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_Name>;
        IRComponents::C_Name value{};
        IR_SAVE_READ(value.name_, r.readString());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_Timer> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_Timer &value) {
        w.writeString(value.name_);
        w.writeU64(value.startTick_);
        w.writeU64(value.targetTick_);
        w.writeU64(value.intervalTicks_);
        w.writeU8(value.active_ ? 1 : 0);
        // `fired_` is a one-tick embedded event that TIMER_FIRE recomputes
        // every tick, so it is self-clearing and never meaningful across a
        // save. Persisted anyway: the alternative is a read that silently
        // defaults a member, and a timer restored from a save taken on its
        // firing tick should look exactly like the one that was saved.
        w.writeU8(value.fired_ ? 1 : 0);
    }

    static IRAsset::Result<IRComponents::C_Timer> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_Timer>;
        IRComponents::C_Timer value{};
        IR_SAVE_READ(value.name_, r.readString());
        IR_SAVE_READ(value.startTick_, r.readU64());
        IR_SAVE_READ(value.targetTick_, r.readU64());
        IR_SAVE_READ(value.intervalTicks_, r.readU64());
        IR_SAVE_READ_BOOL(value.active_, r.readU8());
        IR_SAVE_READ_BOOL(value.fired_, r.readU8());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_Stopwatch> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_Stopwatch &value) {
        w.writeString(value.name_);
        w.writeU64(value.startTick_);
        w.writeU64(value.pausedElapsed_);
        w.writeU8(value.running_ ? 1 : 0);
    }

    static IRAsset::Result<IRComponents::C_Stopwatch> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_Stopwatch>;
        IRComponents::C_Stopwatch value{};
        IR_SAVE_READ(value.name_, r.readString());
        IR_SAVE_READ(value.startTick_, r.readU64());
        IR_SAVE_READ(value.pausedElapsed_, r.readU64());
        IR_SAVE_READ_BOOL(value.running_, r.readU8());
        return Res::success(std::move(value));
    }
};

template <> struct SaveSerialize<IRComponents::C_Cycle> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_Cycle &value) {
        w.writeString(value.name_);
        w.writeU64(value.periodTicks_);
        w.writeU64(value.phaseOffset_);
        // `breakpoints_` is a fixed inline array but only the first
        // `numBreakpoints_` entries are live — write the live prefix so the
        // on-disk size tracks the content, not the (tunable) array capacity.
        w.writeU8(value.numBreakpoints_);
        for (std::uint8_t i = 0; i < value.numBreakpoints_; ++i) {
            w.writeU64(value.breakpoints_[i]);
        }
        w.writeU64(value.lastCycleNum_);
        w.writeU8(value.lastSegmentIndex_);
        w.writeU8(value.boundaryCrossed_ ? 1 : 0);
        w.writeU64(value.fromCycle_);
        w.writeU64(value.toCycle_);
        w.writeU8(value.segmentIndex_);
        w.writeU8(value.fromSegment_);
        w.writeU8(value.toSegment_);
    }

    static IRAsset::Result<IRComponents::C_Cycle> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_Cycle>;
        IRComponents::C_Cycle value{};
        IR_SAVE_READ(value.name_, r.readString());
        IR_SAVE_READ(value.periodTicks_, r.readU64());
        IR_SAVE_READ(value.phaseOffset_, r.readU64());

        std::uint8_t numBreakpoints = 0;
        IR_SAVE_READ(numBreakpoints, r.readU8());
        // A count past the inline array's capacity means the bytes came from
        // a build with a larger kMaxBreakpoints (or are corrupt); either way
        // copying them would overrun the array. UnknownTag is the "record
        // shape this build doesn't know, likely a newer writer" code.
        if (numBreakpoints > IRComponents::C_Cycle::kMaxBreakpoints) {
            return Res::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_Cycle: breakpoint count " + std::to_string(numBreakpoints) +
                    " exceeds this build's kMaxBreakpoints (" +
                    std::to_string(IRComponents::C_Cycle::kMaxBreakpoints) + ")"
            );
        }
        value.numBreakpoints_ = numBreakpoints;
        for (std::uint8_t i = 0; i < numBreakpoints; ++i) {
            IR_SAVE_READ(value.breakpoints_[i], r.readU64());
        }

        IR_SAVE_READ(value.lastCycleNum_, r.readU64());
        IR_SAVE_READ(value.lastSegmentIndex_, r.readU8());
        IR_SAVE_READ_BOOL(value.boundaryCrossed_, r.readU8());
        IR_SAVE_READ(value.fromCycle_, r.readU64());
        IR_SAVE_READ(value.toCycle_, r.readU64());
        IR_SAVE_READ(value.segmentIndex_, r.readU8());
        IR_SAVE_READ(value.fromSegment_, r.readU8());
        IR_SAVE_READ(value.toSegment_, r.readU8());
        return Res::success(std::move(value));
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_COMMON_H */
