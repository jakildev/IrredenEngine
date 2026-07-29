#ifndef IR_SAVE_SERIALIZERS_AUDIO_H
#define IR_SAVE_SERIALIZERS_AUDIO_H

/// `SaveSerialize<C>` specialization for `engine/prefabs/irreden/audio/`
/// (#2242). Opt-in serializer header: include it wherever a registry
/// registers `C_MidiSequence`; never pulled by the component header.

#include <irreden/audio/components/component_midi_sequence.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>

#include <irreden/asset/binary_io.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace IRWorld {

/// Only the **authored** sequence plus its playback cursor is persisted.
/// `C_MidiSequence` derives a block of tempo scalars in its constructor
/// (`m_bps`, `m_ticksPerMeasure`, `m_measuresPerSecond`, `m_ticksPerSecond`,
/// and the public `lengthMidiTicks_`) purely from `bpm_`, `timeSignature_`
/// and `lengthMeasures_`. Writing those out would persist the same facts
/// twice and let a hand-edited file present a tempo whose derived scalars
/// disagree with it; instead `read` funnels the authored fields back through
/// the constructor so every derived value is recomputed by the one piece of
/// code that owns the formula, then restores the two genuine pieces of live
/// state the constructor zeroes (`tickCount_`, `nextMessageIndex_`).
template <> struct SaveSerialize<IRComponents::C_MidiSequence> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_MidiSequence &value) {
        w.writeF32(value.bpm_);
        w.writeI32(value.timeSignature_.first);
        w.writeI32(value.timeSignature_.second);
        w.writeI32(value.lengthMeasures_);
        w.writeU8(value.looping_ ? 1 : 0);
        // Element-wise rather than detail::writeTrivialVector: the element is
        // a std::pair, and std::pair is NOT trivially copyable (it declares a
        // copy-assignment operator), even when both of its members are. The
        // tick and the message are written as their own fields instead.
        w.writeVarUInt(value.messageSequence_.size());
        for (const std::pair<int, IRComponents::C_MidiMessage> &entry : value.messageSequence_) {
            w.writeI32(entry.first);
            w.writeBytes(&entry.second, sizeof(IRComponents::C_MidiMessage));
        }
        w.writeF64(value.tickCount_);
        w.writeI32(value.nextMessageIndex_);
    }

    static IRAsset::Result<IRComponents::C_MidiSequence> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_MidiSequence>;

        float bpm = 0.0f;
        std::int32_t timeSignatureNumerator = 0;
        std::int32_t timeSignatureDenominator = 0;
        std::int32_t lengthMeasures = 0;
        bool looping = false;
        std::vector<std::pair<int, IRComponents::C_MidiMessage>> messageSequence;
        double tickCount = 0.0;
        std::int32_t nextMessageIndex = 0;

        IR_SAVE_READ(bpm, r.readF32());
        IR_SAVE_READ(timeSignatureNumerator, r.readI32());
        IR_SAVE_READ(timeSignatureDenominator, r.readI32());
        IR_SAVE_READ(lengthMeasures, r.readI32());
        IR_SAVE_READ_BOOL(looping, r.readU8());

        std::uint64_t messageCount = 0;
        IR_SAVE_READ(messageCount, r.readVarUInt());
        messageSequence.reserve(detail::boundedReserve(messageCount));
        for (std::uint64_t i = 0; i < messageCount; ++i) {
            std::int32_t tick = 0;
            IR_SAVE_READ(tick, r.readI32());
            std::array<std::byte, sizeof(IRComponents::C_MidiMessage)> raw{};
            IR_SAVE_READ_STATUS(r.readBytes(raw.data(), raw.size()));
            messageSequence.emplace_back(tick, std::bit_cast<IRComponents::C_MidiMessage>(raw));
        }

        IR_SAVE_READ(tickCount, r.readF64());
        IR_SAVE_READ(nextMessageIndex, r.readI32());

        IRComponents::C_MidiSequence value{
            bpm,
            std::pair<int, int>{timeSignatureNumerator, timeSignatureDenominator},
            lengthMeasures,
            looping,
            std::move(messageSequence)
        };
        value.tickCount_ = tickCount;
        value.nextMessageIndex_ = nextMessageIndex;
        return Res::success(std::move(value));
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_AUDIO_H */
