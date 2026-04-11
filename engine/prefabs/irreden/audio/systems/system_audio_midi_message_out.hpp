#ifndef SYSTEM_AUDIO_MIDI_MESSAGE_OUT_H
#define SYSTEM_AUDIO_MIDI_MESSAGE_OUT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/audio/components/component_midi_device.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRAudio;

namespace IRSystem {

template <> struct System<OUTPUT_MIDI_MESSAGE_OUT> {
    static SystemId create() {
        SystemId system = createSystem<C_MidiMessage>(
            "OutputMidiMessageOut",
            [](const C_MidiMessage &midiMessage) {
                IRE_LOG_DEBUG(
                    "Sending midi message (type 0x{:02X}, channel 0x{:02X}, data1 {}, data2 {})",
                    midiMessage.getStatusBits(),
                    midiMessage.getChannelBits(),
                    midiMessage.data1_,
                    midiMessage.data2_
                );
                IRAudio::sendMidiMessage(midiMessage.toRtMidiMessage());
            }
        );
        IRSystem::addSystemTag<C_MidiOut>(system);
        return system;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_AUDIO_MIDI_MESSAGE_OUT_H */
