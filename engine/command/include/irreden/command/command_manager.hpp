/*
 * Project: Irreden Engine
 * File: command_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <irreden/ir_input.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/command/command.hpp>
#include <irreden/common/components/component_tags_all.hpp>

#include <unordered_map>
#include <memory>
#include <list>
#include <functional>

using namespace IRInput;
using namespace IRAudio;

namespace IRCommand {

    class CommandManager {
    public:
        CommandManager();

        template <
            typename Function
        >
        int createCommand(
            InputTypes inputType,
            ButtonStatuses triggerStatus,
            int button,
            Function command
        )
        {
            m_userCommands.emplace_back(CommandStruct<COMMAND_BUTTON>{
                inputType,
                triggerStatus,
                button,
                command
            });
            return m_userCommands.size() - 1;
        }

        template <
            typename Function,
            typename... Args
        >
        int registerMidiNoteCommand(
            int device,
            InputTypes InputType,
            Function command,
            Args... fixedArgs
        )
        {
            if(!m_midiCCDeviceCommands.contains(device)) {
                m_midiNoteDeviceCommands.emplace(
                    device,
                    std::vector<CommandStruct<COMMAND_MIDI_NOTE>>{}
                );
            }
            m_midiNoteDeviceCommands[device].emplace_back(
                CommandStruct<COMMAND_MIDI_NOTE>{
                    InputType,
                    command,
                    fixedArgs...
                }
            );
            return m_midiNoteDeviceCommands[device].size() - 1;
        }

        template <typename Function>
        int registerMidiCCCommand(
            int device,
            InputTypes InputType,
            unsigned char ccMessage,
            Function command
        )
        {
            if(!m_midiCCDeviceCommands.contains(device)) {
                m_midiCCDeviceCommands.emplace(
                    device,
                    std::vector<CommandStruct<COMMAND_MIDI_CC>>{}
                );
            }
            m_midiCCDeviceCommands[device].emplace_back(
                CommandStruct<COMMAND_MIDI_CC>{
                    InputType,
                    ccMessage,
                    command
                }
            );
            return m_midiCCDeviceCommands[device].size() - 1;
        }

        void executeDeviceMidiCCCommandsAll();
        void executeDeviceMidiNoteCommandsAll();
        void executeUserKeyboardCommandsAll();
        void executeDeviceMidiCCCommands(
            int device,
            std::vector<
                CommandStruct<CommandTypes::COMMAND_MIDI_CC>
            >& commands
        );
        void executeDeviceMidiCCCommand(
            int device,
            CommandStruct<COMMAND_MIDI_CC>& command
        );
        void executeDeviceMidiNoteCommands(
            int device,
            std::vector<CommandStruct<COMMAND_MIDI_NOTE>>& commands
        );
        void executeDeviceMidiNoteCommand(
            int device,
            CommandStruct<COMMAND_MIDI_NOTE>& command
        );

    private:
        std::unordered_map<
            int,
            std::vector<CommandStruct<COMMAND_MIDI_NOTE>>
        > m_midiNoteDeviceCommands;
        std::unordered_map<
            int,
            std::vector<CommandStruct<COMMAND_MIDI_CC>>
        > m_midiCCDeviceCommands;
        std::vector<CommandStruct<COMMAND_BUTTON>> m_userCommands;
    };

} // namespace IRCommand

#endif /* COMMAND_MANAGER_H */
