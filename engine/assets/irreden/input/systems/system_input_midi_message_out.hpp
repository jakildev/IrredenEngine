/*
 * Project: Irreden Engine
 * File: system_input_midi_message_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// TODO: MOVE TO AUDIO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#ifndef SYSTEM_INPUT_MIDI_MESSAGE_OUT_H
#define SYSTEM_INPUT_MIDI_MESSAGE_OUT_H

#include <irreden/system/ir_system_base.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/audio/midi_out.hpp>

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
        System(IRMidiOut& midiOut)
        :   m_midiOut{midiOut}
        ,   m_nextDeviceId{0}
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
                m_midiOut.sendMessage(midiMessages[i].toRtMidiMessage());
            }
        }

        EntityHandle createMidiDeviceOut(
            std::string name,
            MidiChannels channel
        )
        {
            IRMidiChannel channelValue = (IRMidiChannel)channel;

            EntityHandle device{};
            device.set(C_Name{name});
            device.set(C_MidiChannel{channelValue});
            device.set(C_MidiOut{});

            int newDeviceId = m_nextDeviceId++;
            device.set(C_MidiDevice{newDeviceId});
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

        inline IRMidiChannel getDeviceChannel(const C_MidiDevice& device) const {
            return m_midiOutDeviceChannels[device.id_];
        }

        EntityHandle createMidiMessageOut(
            C_MidiDevice& device,
            IRMidiStatus status,
            unsigned char data1,
            unsigned char data2 = 0
        )
        {
            EntityHandle message{};
            message.set(C_MidiMessage{
                static_cast<unsigned char>(
                    status | getDeviceChannel(device)
                ),
                data1,
                data2
            });
            message.set(C_MidiOut{});
            message.set(C_Lifetime{1});
            return message;
        }

    private:
        IRMidiOut& m_midiOut;

        int m_nextDeviceId;
        std::vector<EntityHandle> m_midiOutDevices;
        std::vector<IRMidiChannel> m_midiOutDeviceChannels;

        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };

} // namespace IRECS

#endif /* SYSTEM_INPUT_MIDI_MESSAGE_OUT_H */
