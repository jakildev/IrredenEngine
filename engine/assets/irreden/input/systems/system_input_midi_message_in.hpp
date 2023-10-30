/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_input_midi_message_in.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// TODO: MOVE TO AUDIO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#ifndef SYSTEM_INPUT_MIDI_MESSAGE_IN_H
#define SYSTEM_INPUT_MIDI_MESSAGE_IN_H

#include <irreden/ecs/ir_system_base.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/audio/midi_in.hpp>

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
    class IRSystem<INPUT_MIDI_MESSAGE_IN> : public IRSystemBase<
        INPUT_MIDI_MESSAGE_IN,
        C_MidiMessage,
        C_MidiIn
    > {
    public:
        IRSystem(IRMidiIn& midiIn)
        :   m_midiIn{midiIn}
        ,   m_ccMessagesReceivedThisFrame{}
        ,   m_nextDeviceId{0}
        {
            for(unsigned char i = 0; i < kNumMidiChannels; ++i) {
                m_ccMessagesReceivedThisFrame.insert({
                    i,
                    std::unordered_map<IRCCMessage, IRCCData>{}
                });
                m_midiNoteOffMessagesThisFrame.insert({
                    i,
                    std::vector<C_MidiMessage>{}
                });
                m_midiNoteOnMessagesThisFrame.insert({
                    i,
                    std::vector<C_MidiMessage>{}
                });
            }
            IRProfile::engLogInfo("Created system INPUT_MIDI_MESSAGE_IN");
        }
        virtual ~IRSystem() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            const std::vector<C_MidiMessage>& midiMessages,
            const std::vector<C_MidiIn>& midiIns
        )
        {
            for(int i=0; i < entities.size(); i++) {
                const auto& midiMessage = midiMessages[i];
                const IRMidiStatus statusBits =
                    midiMessage.getStatusBits();
                const IRMidiChannel channel =
                    midiMessage.getChannelBits();

                if(!m_midiChannelToDeviceMappings.contains(channel)) {
                    continue;
                }

                if(statusBits == IRAudio::kMidiStatus_NOTE_ON) {
                    IRProfile::engLogInfo("Midi message note on!");
                    m_midiNoteOnMessagesThisFrame[channel].push_back(midiMessage);
                }
                if(statusBits == IRAudio::kMidiStatus_NOTE_OFF) {
                    IRProfile::engLogInfo("Midi message note off!");
                    m_midiNoteOffMessagesThisFrame[channel].push_back(midiMessage);
                }
                if(statusBits == IRAudio::kMidiStatus_CONTROL_CHANGE) {
                    IRProfile::engLogDebug("Midi message control change!");
                    m_ccMessagesReceivedThisFrame[channel][midiMessage.getCCNumber()] =
                        midiMessage.getCCValue();
                }
            }
        }

        EntityHandle createMidiDeviceIn(
            std::string name,
            MidiChannels channel
        )
        {
            IRMidiChannel channelValue = (IRMidiChannel)channel;
            if(m_midiChannelToDeviceMappings.contains(channelValue)) {
                IRProfile::engLogError("Device already exists for channel, skipping {}", channelValue);
                return m_midiInDevices[m_midiChannelToDeviceMappings[channelValue]];
            }

            EntityHandle device{};
            device.set(C_Name{name});
            device.set(C_MidiChannel{channelValue});
            device.set(C_MidiIn{});

            int newDeviceId = m_nextDeviceId++;
            device.set(C_MidiDevice{newDeviceId});
            m_midiChannelToDeviceMappings.insert({channel, newDeviceId});
            m_midiDeviceToChannelMappings.insert({newDeviceId, channel});
            m_midiInDevices.push_back(device);
            IRProfile::engLogInfo(
                "Created MIDI device {} (id: {}) on channel {} (value: {})",
                name,
                newDeviceId,
                static_cast<int>(channelValue) + 1,
                static_cast<int>(channelValue)
            );
            return device;

        }

        IRCCData checkCCMessageReceived(
            int device,
            IRCCMessage ccNumber
        )
        {
            IRMidiChannel channel = m_midiDeviceToChannelMappings.at(device);
            if(!m_ccMessagesReceivedThisFrame[channel].contains(ccNumber)) {
                return kCCFalse;
            }
            return m_ccMessagesReceivedThisFrame[channel][ccNumber];
        }

        const std::vector<C_MidiMessage>& getMidiNotesOnThisFrame(int device) {
            return m_midiNoteOnMessagesThisFrame.at(
                m_midiDeviceToChannelMappings.at(device)
            );
        }
        const std::vector<C_MidiMessage>& getMidiNotesOffThisFrame(int device) {
            return m_midiNoteOffMessagesThisFrame.at(
                m_midiDeviceToChannelMappings.at(device)
            );
        }

    private:
        IRMidiIn& m_midiIn;

        int m_nextDeviceId;
        std::vector<EntityHandle> m_midiInDevices;

        // Condense these two into one vector where
        // index is the device id
        std::unordered_map<IRMidiChannel, int> m_midiChannelToDeviceMappings;
        std::unordered_map<int, IRMidiChannel> m_midiDeviceToChannelMappings;

        std::unordered_map<
            IRMidiChannel,
            std::unordered_map<
                IRCCMessage,
                // std::vector<IRCCData> // TODO: Do i need to worry about multiple of the same message in one frame?
                IRCCData
            >
        > m_ccMessagesReceivedThisFrame;
        std::unordered_map<
            IRMidiChannel,
            std::vector<C_MidiMessage>
        > m_midiNoteOnMessagesThisFrame;

        std::unordered_map<
            IRMidiChannel,
            std::vector<C_MidiMessage>
        > m_midiNoteOffMessagesThisFrame;

        virtual void beginExecute() override {
            for(auto& channelMap : m_ccMessagesReceivedThisFrame) {
                channelMap.second.clear();
            }
            for(auto& channelMap : m_midiNoteOnMessagesThisFrame) {
                channelMap.second.clear();
            }
            for(auto& channelMap : m_midiNoteOffMessagesThisFrame) {
                channelMap.second.clear();
            }
        }
        virtual void endExecute() override {}
    };

} // namespace IRECS

#endif /* SYSTEM_INPUT_MIDI_MESSAGE_IN_H */
