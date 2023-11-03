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

#include <irreden/command/command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_audio.hpp>

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

        // template <IRECS::SystemName SystemName, InputTypes InputType>
        // void registerSystemCommand(
        //     int button,
        //     std::function<void()> command
        // )
        // {
        //     if(!m_systemCommands.contains(SystemName)) {
        //         m_systemCommands.emplace(
        //             SystemName,
        //             std::vector<Command<IR_COMMAND_SYSTEM>>{}
        //         );
        //     }
        //     m_systemCommands[SystemName].emplace_back(
        //         Command<IR_COMMAND_SYSTEM>{
        //             InputType,
        //             button,
        //             command
        //         }
        //     );
        // }

        // template <
        //     SystemName systemName,
        //     CommandNames commandName,
        //     InputTypes InputType,
        //     typename Function,
        //     typename... Args
        // >
        // void registerEntityCommand(
        //     int button,
        //     Function command,
        //     Args... fixedArgs
        // )
        // {
        //     if(!m_entityCommands.contains(commandName)) {
        //         m_entityCommands.emplace(
        //             commandName,
        //             Command<IR_COMMAND_ENTITY>{
        //                 InputType,
        //                 button,
        //                 command,
        //                 fixedArgs...
        //             }
        //         );
        //         m_systemEntityCommands[systemName].push_back(commandName);
        //     }
        // }
        template <
            typename Function
        >
        int registerCommand(
            InputTypes inputType,
            ButtonStatuses triggerStatus,
            int button,
            Function command
        )
        {
            m_userCommands.emplace_back(Command<IR_COMMAND_USER>{
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
                    std::vector<Command<IR_COMMAND_MIDI_NOTE>>{}
                );
            }
            m_midiNoteDeviceCommands[device].emplace_back(
                Command<IR_COMMAND_MIDI_NOTE>{
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
                    std::vector<Command<IR_COMMAND_MIDI_CC>>{}
                );
            }
            m_midiCCDeviceCommands[device].emplace_back(
                Command<IR_COMMAND_MIDI_CC>{
                    InputType,
                    ccMessage,
                    command
                }
            );
            return m_midiCCDeviceCommands[device].size() - 1;
        }

        // template <CommandNames CommandName>
        // void bindEntityToCommand(EntityHandle entity) {
        //     m_entitiesBoundToCommands[CommandName].push_back(entity);
        // }

        // void executeSystemCommands(SystemName systemName);

        // TODO: Still pretty slow when operating on a lot of entities,
        // because each one is fetched individually. Still need a
        // tickWithArchetype type command
        // void executeSystemEntityCommands(SystemName systemName);

        // void executeDeviceMidiCCCommandsAll();
        // void executeDeviceMidiNoteCommandsAll();
        void executeUserKeyboardCommandsAll();
        // void executeDeviceMidiCCCommands(
        //     int device,
        //     std::vector<
        //         Command<CommandTypes::IR_COMMAND_MIDI_CC>
        //     >& commands
        // );
        // void executeDeviceMidiCCCommand(
        //     int device,
        //     Command<IR_COMMAND_MIDI_CC>& command
        // );
        // void executeDeviceMidiNoteCommands(
        //     int device,
        //     std::vector<Command<IR_COMMAND_MIDI_NOTE>>& commands
        // );
        // void executeDeviceMidiNoteCommand(
        //     int device,
        //     Command<IR_COMMAND_MIDI_NOTE>& command
        // );

    private:
        // EntityHandle m_noneEntity;
        // std::unordered_map<
        //     SystemName,
        //     std::vector<Command<IR_COMMAND_SYSTEM>>
        // > m_systemCommands;
        // std::unordered_map<
        //     SystemName,
        //     std::vector<CommandNames>
        // > m_systemEntityCommands;
        // std::unordered_map<
        //     CommandNames,
        //     Command<IR_COMMAND_ENTITY>
        // > m_entityCommands;
        std::unordered_map<
            int,
            std::vector<Command<IR_COMMAND_MIDI_NOTE>>
        > m_midiNoteDeviceCommands;
        std::unordered_map<
            int,
            std::vector<Command<IR_COMMAND_MIDI_CC>>
        > m_midiCCDeviceCommands;

        std::vector<Command<IR_COMMAND_USER>> m_userCommands;
        // std::vector<std::list<EntityHandle>> m_entitiesBoundToMidiNoteCommands;
        // std::unordered_map<CommandNames, std::list<EntityHandle>>
        //     m_entitiesBoundToCommands;

        // void executeEntityCommand(CommandNames commandName);
        // bool checkButton(InputTypes inputType, int button);
        // CCData checkCCMessage(int device, CCMessage ccMessage);

    };

} // namespace IRCommand

#endif /* COMMAND_MANAGER_H */
