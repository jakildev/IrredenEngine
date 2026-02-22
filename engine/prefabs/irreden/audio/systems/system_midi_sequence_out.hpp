#ifndef SYSTEM_MIDI_SEQUENCE_OUT_H
#define SYSTEM_MIDI_SEQUENCE_OUT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/audio/components/component_midi_sequence.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<MIDI_SEQUENCE_OUT> {
    static SystemId create() {
        return createSystem<C_MidiSequence>(
            "MidiSequenceOut", [](C_MidiSequence &sequence) {
                if (sequence.isFinished() && !sequence.looping_) {
                    return;
                }

                double midiTicksThisFrame = sequence.calcMidiTicksPerFrameTick();
                sequence.tickCount_ += midiTicksThisFrame;

                std::optional<C_MidiMessage> msg = sequence.getNextMessage();
                while (msg.has_value()) {
                    IREntity::createEntity(msg.value(), C_MidiOut{}, C_Lifetime{1});
                    msg = sequence.getNextMessage();
                }

                if (sequence.isFinished() && sequence.looping_) {
                    sequence.reset();
                }
            });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MIDI_SEQUENCE_OUT_H */
