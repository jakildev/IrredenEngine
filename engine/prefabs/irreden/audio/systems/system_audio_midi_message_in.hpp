#ifndef SYSTEM_AUDIO_MIDI_MESSAGE_IN_H
#define SYSTEM_AUDIO_MIDI_MESSAGE_IN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_source_port.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRAudio;

namespace IRSystem {

// DEPRECATED — use MidiIn::tick() for hardware input instead. Synthetic input
// uses IRAudio::insertNoteOnMessage, insertNoteOffMessage, or insertCCMessage.
template <> struct System<INPUT_MIDI_MESSAGE_IN> {
    static SystemId create() {
        // C_MidiSourcePort distinguishes synthetic inbound messages from
        // outbound messages and preserves their port-scoped query lane.
        SystemId system = createSystem<C_MidiMessage, C_MidiSourcePort>(
            "InputMidiMessageIn",
            [](C_MidiMessage &midiMessage, C_MidiSourcePort &sourcePort) {
                const MidiStatus statusBits = midiMessage.getStatusBits();
                const MidiChannel channel = midiMessage.getChannelBits();
                const int portIndex = sourcePort.portIndex_;

                if (statusBits == IRAudio::kMidiStatus_NOTE_ON) {
                    IRE_LOG_DEBUG("Midi message note on!");
                    IRAudio::insertNoteOnMessage(portIndex, channel, midiMessage);
                }
                if (statusBits == IRAudio::kMidiStatus_NOTE_OFF) {
                    IRE_LOG_DEBUG("Midi message note off!");
                    IRAudio::insertNoteOffMessage(portIndex, channel, midiMessage);
                }
                if (statusBits == IRAudio::kMidiStatus_CONTROL_CHANGE) {
                    IRE_LOG_DEBUG("Midi message control change!");
                    IRAudio::insertCCMessage(portIndex, channel, midiMessage);
                }
            }
        );
        addSystemTag<C_MidiIn>(system);
        return system;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_AUDIO_MIDI_MESSAGE_IN_H */
