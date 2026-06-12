#ifndef SYSTEM_AUDIO_MIDI_MESSAGE_IN_H
#define SYSTEM_AUDIO_MIDI_MESSAGE_IN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/audio/components/component_midi_device.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_source_port.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRAudio;

namespace IRSystem {

template <> struct System<INPUT_MIDI_MESSAGE_IN> {
    static SystemId create() {
        // Match C_MidiSourcePort so only genuine inbound messages (tagged with
        // their source port by MidiIn::processMidiMessageQueue) are drained
        // into the query buffer — outbound C_MidiMessage entities lack it and
        // are left for the OUTPUT system. The port id routes each message into
        // both the merged (all-ports) and per-port query views.
        SystemId system = createSystem<C_MidiMessage, C_MidiSourcePort>(
            "InputMidiMessageIn",
            [](C_MidiMessage &midiMessage, C_MidiSourcePort &sourcePort) {
                const MidiStatus statusBits = midiMessage.getStatusBits();
                const MidiChannel channel = midiMessage.getChannelBits();
                const int portIndex = sourcePort.portIndex_;

                if (statusBits == IRAudio::kMidiStatus_NOTE_ON) {
                    IRE_LOG_INFO("Midi message note on!");
                    IRAudio::insertNoteOnMessage(portIndex, channel, midiMessage);
                }
                if (statusBits == IRAudio::kMidiStatus_NOTE_OFF) {
                    IRE_LOG_INFO("Midi message note off!");
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

    // EntityId createMidiDeviceIn(
    //     std::string name,
    //     MidiChannels channel
    // )
    // {
    //     MidiChannel channelValue = (MidiChannel)channel;
    //     if(m_midiChannelToDeviceMappings.contains(channelValue)) {
    //         IRE_LOG_ERROR("Device already exists for channel, skipping {}", channelValue);
    //         return m_midiInDevices[m_midiChannelToDeviceMappings[channelValue]];
    //     }

    //     int newDeviceId = m_nextDeviceId++;
    //     EntityId device = IRECS::createEntity(
    //         C_Name{name},
    //         C_MidiChannel{channelValue},
    //         C_MidiIn{},
    //         C_MidiDevice{newDeviceId}
    //     );
    //     m_midiChannelToDeviceMappings.insert({channel, newDeviceId});
    //     m_midiDeviceToChannelMappings.insert({newDeviceId, channel});
    //     m_midiInDevices.push_back(device);
    //     IRE_LOG_INFO("
    //         "Created MIDI device {} (id: {}) on channel {} (value: {})",
    //         name,
    //         newDeviceId,
    //         static_cast<int>(channelValue) + 1,
    //         static_cast<int>(channelValue)
    //     );
    //     return device;

    // }
};

} // namespace IRSystem

#endif /* SYSTEM_AUDIO_MIDI_MESSAGE_IN_H */
