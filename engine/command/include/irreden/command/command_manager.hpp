/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\commands\command_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <irreden/command/command_manager.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/ecs/ir_system_virtual.hpp>

#include <unordered_map>
#include <memory>
#include <list>
#include <functional>

using namespace IRInput;
using namespace IRAudio;

namespace IRCommands {

    class CommandManager {
    public:
        CommandManager()
        {

        }

        template <IRECS::IRSystemName SystemName, IRInputTypes InputType>
        void registerSystemCommand(
            int button,
            std::function<void()> command
        )
        {
            if(!m_systemCommands.contains(SystemName)) {
                m_systemCommands.emplace(
                    SystemName,
                    std::vector<IRCommand<IR_COMMAND_SYSTEM>>{}
                );
            }
            m_systemCommands[SystemName].emplace_back(
                IRCommand<IR_COMMAND_SYSTEM>{
                    InputType,
                    button,
                    command
                }
            );
        }

        template <
            IRSystemName systemName,
            IRCommandNames commandName,
            IRInputTypes InputType,
            typename Function,
            typename... Args
        >
        void registerEntityCommand(
            int button,
            Function command,
            Args... fixedArgs
        )
        {
            if(!m_entityCommands.contains(commandName)) {
                m_entityCommands.emplace(
                    commandName,
                    IRCommand<IR_COMMAND_ENTITY>{
                        InputType,
                        button,
                        command,
                        fixedArgs...
                    }
                );
                m_systemEntityCommands[systemName].push_back(commandName);
            }
        }

        template <
            typename Function,
            typename... Args
        >
        int registerMidiNoteCommand(
            int device,
            IRInputTypes InputType,
            Function command,
            Args... fixedArgs
        )
        {
            if(!m_midiCCDeviceCommands.contains(device)) {
                m_midiNoteDeviceCommands.emplace(
                    device,
                    std::vector<IRCommand<IR_COMMAND_MIDI_NOTE>>{}
                );
            }
            m_midiNoteDeviceCommands[device].emplace_back(
                IRCommand<IR_COMMAND_MIDI_NOTE>{
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
            IRInputTypes InputType,
            unsigned char ccMessage,
            Function command
        )
        {
            if(!m_midiCCDeviceCommands.contains(device)) {
                m_midiCCDeviceCommands.emplace(
                    device,
                    std::vector<IRCommand<IR_COMMAND_MIDI_CC>>{}
                );
            }
            m_midiCCDeviceCommands[device].emplace_back(
                IRCommand<IR_COMMAND_MIDI_CC>{
                    InputType,
                    ccMessage,
                    command
                }
            );
            return m_midiCCDeviceCommands[device].size() - 1;
        }

        template <typename Function>
        int registerUserCommand(
            IRInputTypes inputType,
            int button,
            Function command
        )
        {
            m_userCommands.emplace_back(IRCommand<IR_COMMAND_USER>{
                inputType,
                button,
                command
            });

            return m_userCommands.size() - 1;
        }

        template <IRCommandNames CommandName>
        void bindEntityToCommand(EntityHandle entity) {
            m_entitiesBoundToCommands[CommandName].push_back(entity);
        }

        void executeSystemCommands(IRSystemName systemName);

        // TODO: Still pretty slow when operating on a lot of entities,
        // because each one is fetched individually. Still need a
        // tickWithArchetype type command
        void executeSystemEntityCommands(IRSystemName systemName);

        void executeDeviceMidiCCCommandsAll();
        void executeDeviceMidiNoteCommandsAll();
        void executeUserKeyboardCommandsAll();
        void executeDeviceMidiCCCommands(
            int device,
            std::vector<IRCommand<IR_COMMAND_MIDI_CC>>& commands
        );
        void executeDeviceMidiCCCommand(
            int device,
            IRCommand<IR_COMMAND_MIDI_CC>& command
        );
        void executeDeviceMidiNoteCommands(
            int device,
            std::vector<IRCommand<IR_COMMAND_MIDI_NOTE>>& commands
        );
        void executeDeviceMidiNoteCommand(
            int device,
            IRCommand<IR_COMMAND_MIDI_NOTE>& command
        );

    private:
        EntityHandle m_noneEntity;
        std::unordered_map<
            IRSystemName,
            std::vector<IRCommand<IR_COMMAND_SYSTEM>>
        > m_systemCommands;
        std::unordered_map<
            IRSystemName,
            std::vector<IRCommandNames>
        > m_systemEntityCommands;
        std::unordered_map<
            IRCommandNames,
            IRCommand<IR_COMMAND_ENTITY>
        > m_entityCommands;
        std::unordered_map<
            int,
            std::vector<IRCommand<IR_COMMAND_MIDI_NOTE>>
        > m_midiNoteDeviceCommands;
        std::unordered_map<
            int,
            std::vector<IRCommand<IR_COMMAND_MIDI_CC>>
        > m_midiCCDeviceCommands;

        std::vector<IRCommand<IR_COMMAND_USER>> m_userCommands;
        std::vector<std::list<EntityHandle>> m_entitiesBoundToMidiNoteCommands;
        std::unordered_map<IRCommandNames, std::list<EntityHandle>>
            m_entitiesBoundToCommands;

        void executeEntityCommand(IRCommandNames commandName);
        bool checkButton(IRInputTypes inputType, int button);
        IRCCData checkCCMessage(int device, IRCCMessage ccMessage);

    };

} // namespace IRCommands

#endif /* COMMAND_MANAGER_H */
