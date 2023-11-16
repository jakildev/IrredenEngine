/*
 * Project: Irreden Engine
 * File: system_input_midi_message_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// TODO: MOVE TO AUDIO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#ifndef SYSTEM_AUDIO_MIDI_MESSAGE_OUT_H
#define SYSTEM_AUDIO_MIDI_MESSAGE_OUT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/audio/components/component_midi_device.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRAudio;

namespace IRECS {

    template<>
    class System<OUTPUT_MIDI_MESSAGE_OUT> : public SystemBase<
        OUTPUT_MIDI_MESSAGE_OUT,
        C_MidiMessage,
        C_MidiOut
    >   {

    public:
        System()
        :   m_nextDeviceId{0}
        ,   m_midiOutDevices{}
        ,   m_midiOutDeviceChannels{}
        {
            IRProfile::engLogInfo("Creating system OUTPUT_MIDI_MESSAGE_OUT");
        }
        virtual ~System() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            const std::vector<C_MidiMessage>& midiMessages,
            const std::vector<C_MidiOut>& midiOuts
        )
        {
            for(int i=0; i < entities.size(); i++) {
                IRProfile::engLogInfo("Sending midi message (type 0x{:02X}, channel 0x{:02X}, data1 {}, data2 {})",
                    midiMessages[i].getStatusBits(),
                    midiMessages[i].getChannelBits(),
                    midiMessages[i].data1_,
                    midiMessages[i].data2_
                );
                IRAudio::sendMidiMessage(midiMessages[i].toRtMidiMessage());
            }
        }

        // TODO: This is just a prefab
        EntityId createMidiDeviceOut(
            std::string name,
            MidiChannels channel
        )
        {
            MidiChannel channelValue = (MidiChannel)channel;

            int newDeviceId = m_nextDeviceId++;
            EntityId device = IRECS::createEntity(
                C_Name{name},
                C_MidiChannel{channelValue},
                C_MidiOut{},
                C_MidiDevice{newDeviceId}
            );
            m_midiOutDeviceChannels.push_back(channelValue);
            m_midiOutDevices.push_back(device);
            IRProfile::engLogInfo(
                "Created MIDI device OUT {} (id: {}) on channel {} (value: {})",
                name,
                newDeviceId,
                static_cast<int>(channelValue) + 1,
                static_cast<int>(channelValue)
            );
            return device;
        }

        inline MidiChannel getDeviceChannel(const C_MidiDevice& device) const {
            return m_midiOutDeviceChannels[device.id_];
        }

        // TODO: This is just a prefab
        EntityId createMidiMessageOut(
            C_MidiDevice& device,
            MidiStatus status,
            unsigned char data1,
            unsigned char data2 = 0
        )
        {
            return IRECS::createEntity(
                C_MidiMessage{
                    static_cast<unsigned char>(
                        status | getDeviceChannel(device)
                    ),
                    data1,
                    data2
                },
                C_MidiOut{},
                C_Lifetime{1}
            );
        }

    private:
        int m_nextDeviceId;
        std::vector<EntityId> m_midiOutDevices;
        std::vector<MidiChannel> m_midiOutDeviceChannels;

        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };

} // namespace IRECS

#endif /* SYSTEM_AUDIO_MIDI_MESSAGE_OUT_H */
