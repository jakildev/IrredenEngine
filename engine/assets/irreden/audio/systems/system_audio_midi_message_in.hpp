/*
 * Project: Irreden Engine
 * File: system_input_midi_message_in.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_AUDIO_MIDI_MESSAGE_IN_H
#define SYSTEM_AUDIO_MIDI_MESSAGE_IN_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/audio/components/component_midi_device.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRAudio;

namespace IRECS {

    template<>
    struct System<INPUT_MIDI_MESSAGE_IN> {
        static SystemId create() {
            SystemId system = createSystem<C_MidiMessage>(
                "InputMidiMessageIn",
                [](
                    C_MidiMessage& midiMessage
                )
                {
                    const MidiStatus statusBits =
                        midiMessage.getStatusBits();
                    const MidiChannel channel =
                        midiMessage.getChannelBits();

                    if(statusBits == IRAudio::kMidiStatus_NOTE_ON) {
                        IRProfile::engLogInfo("Midi message note on!");
                        IRAudio::insertNoteOnMessage(channel, midiMessage);
                    }
                    if(statusBits == IRAudio::kMidiStatus_NOTE_OFF) {
                        IRProfile::engLogInfo("Midi message note off!");
                        IRAudio::insertNoteOffMessage(channel, midiMessage);
                    }
                    if(statusBits == IRAudio::kMidiStatus_CONTROL_CHANGE) {
                        IRProfile::engLogDebug("Midi message control change!");
                        IRAudio::insertCCMessage(channel, midiMessage);
                    }
                }
            );
            IRECS::addSystemTag<C_MidiIn>(system);
            return system;
        }

        // EntityId createMidiDeviceIn(
        //     std::string name,
        //     MidiChannels channel
        // )
        // {
        //     MidiChannel channelValue = (MidiChannel)channel;
        //     if(m_midiChannelToDeviceMappings.contains(channelValue)) {
        //         IRProfile::engLogError("Device already exists for channel, skipping {}", channelValue);
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
        //     IRProfile::engLogInfo(
        //         "Created MIDI device {} (id: {}) on channel {} (value: {})",
        //         name,
        //         newDeviceId,
        //         static_cast<int>(channelValue) + 1,
        //         static_cast<int>(channelValue)
        //     );
        //     return device;

        // }

    };

} // namespace IRECS

#endif /* SYSTEM_AUDIO_MIDI_MESSAGE_IN_H */
