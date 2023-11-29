/*
 * Project: Irreden Engine
 * File: system_input_midi_message_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

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

namespace IRECS {

    template<>
    struct System<OUTPUT_MIDI_MESSAGE_OUT> {
        static SystemId create() {
            SystemId system = createSystem<C_MidiMessage>(
                "OutputMidiMessageOut",
                [](
                    const C_MidiMessage& midiMessage
                )
                {
                    IRE_LOG_INFO("Sending midi message (type 0x{:02X}, channel 0x{:02X}, data1 {}, data2 {})",
                        midiMessage.getStatusBits(),
                        midiMessage.getChannelBits(),
                        midiMessage.data1_,
                        midiMessage.data2_
                    );
                    IRAudio::sendMidiMessage(midiMessage.toRtMidiMessage());
                }
            );
            IRECS::addSystemTag<C_MidiOut>(system);
            return system;
        }

        // // TODO: This is just a prefab
        // EntityId createMidiDeviceOut(
        //     std::string name,
        //     MidiChannels channel
        // )
        // {
        //     MidiChannel channelValue = (MidiChannel)channel;

        //     int newDeviceId = m_nextDeviceId++;
        //     EntityId device = IRECS::createEntity(
        //         C_Name{name},
        //         C_MidiChannel{channelValue},
        //         C_MidiOut{},
        //         C_MidiDevice{newDeviceId}
        //     );
        //     m_midiOutDeviceChannels.push_back(channelValue);
        //     m_midiOutDevices.push_back(device);
        //     IRE_LOG_INFO("
        //         "Created MIDI device OUT {} (id: {}) on channel {} (value: {})",
        //         name,
        //         newDeviceId,
        //         static_cast<int>(channelValue) + 1,
        //         static_cast<int>(channelValue)
        //     );
        //     return device;
        // }

    };

} // namespace IRECS

#endif /* SYSTEM_AUDIO_MIDI_MESSAGE_OUT_H */
