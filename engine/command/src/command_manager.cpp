/*
 * Project: Irreden Engine
 * File: command_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_input.hpp>
#include <irreden/ir_command.hpp> // To set pointer to manager (prob a better way and I do this in a few places)
#include <irreden/command/command_manager.hpp>

#include <irreden/input/systems/system_input_gamepad.hpp>
#include <irreden/input/systems/system_input_midi_message_in.hpp>

namespace IRCommand {

    CommandManager::CommandManager() {
        g_commandManager = this;
        IRProfile::engLogInfo("Created CommandManager");
    }

    // void CommandManager::executeSystemCommands(SystemName systemName) {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     const auto& systemCommandList =
    //         m_systemCommands[systemName];
    //     for(int i = 0; i < systemCommandList.size(); ++i) {
    //         const auto& command = systemCommandList[i];
    //         if(checkButton(command.getType(), command.getButton()))
    //         {
    //             command.execute();
    //         }
    //     }
    // }

    // void CommandManager::executeSystemEntityCommands(SystemName systemName) {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     const auto& systemEntityCommandList =
    //         m_systemEntityCommands[systemName];
    //     for(int i = 0; i < systemEntityCommandList.size(); ++i) {
    //         executeEntityCommand(systemEntityCommandList[i]);
    //     }
    // }

    // void CommandManager::executeEntityCommand(CommandNames commandName) {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     const auto& command =
    //         m_entityCommands.at(commandName);
    //     if(IRECS::getSystem<INPUT_KEY_MOUSE>()->
    //         checkButton(
    //             command.getType(),
    //             (KeyMouseButtons)command.getButton()
    //         )
    //     )
    //     {
    //         const auto& boundEntities = m_entitiesBoundToCommands[commandName];
    //         for(auto const& entity : boundEntities) {
    //             command.execute(entity);
    //         }
    //     }
    // }

    // void CommandManager::executeDeviceMidiCCCommandsAll() {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     for(auto& [device, commands] : m_midiCCDeviceCommands) {
    //         executeDeviceMidiCCCommands(device, commands);
    //     }
    // }

    // void CommandManager::executeDeviceMidiCCCommands(
    //     int device,
    //     std::vector<Command<IR_COMMAND_MIDI_CC>>& commands
    // )
    // {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     for(int i = 0; i < commands.size(); ++i) {
    //         executeDeviceMidiCCCommand(device, commands[i]);
    //     }
    // }

    // void CommandManager::executeDeviceMidiCCCommand(
    //     int device,
    //     Command<IR_COMMAND_MIDI_CC>& command
    // )
    // {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     IRCCData ccData = checkCCMessage(device, command.getCCMessage());
    //     if(ccData != kCCFalse) {
    //         command.execute(ccData);
    //     }
    // }

    // void CommandManager::executeDeviceMidiNoteCommandsAll() {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     for(auto& [device, commands] : m_midiNoteDeviceCommands) {
    //         executeDeviceMidiNoteCommands(device, commands);
    //     }
    // }

    void CommandManager::executeUserKeyboardCommandsAll() {
        for(auto& command : m_userCommands) {
            if(IRInput::checkKeyMouseButton(
                static_cast<IRInput::KeyMouseButtons>(command.getButton()),
                command.getTriggerStatus()
            ))
            {
                command.execute();
            }
        }
    }

    // void CommandManager::executeDeviceMidiNoteCommands(
    //     int device,
    //     std::vector<Command<IR_COMMAND_MIDI_NOTE>>& commands
    // )
    // {
    //     IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
    //     for(int i = 0; i < commands.size(); ++i) {
    //         executeDeviceMidiNoteCommand(device, commands[i]);
    //     }
    // }

    // void CommandManager::executeDeviceMidiNoteCommand(
    //     int device,
    //     Command<IR_COMMAND_MIDI_NOTE>& command
    // )
    // {
    //     if(command.getType() == MIDI_NOTE) {
    //         auto& notes = IRECS::getSystem<INPUT_MIDI_MESSAGE_IN>()->
    //             getMidiNotesOnThisFrame(device);
    //             // TODO: Check statuses here...
    //         for(int i = 0; i < notes.size(); ++i) {
    //             command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
    //         }
    //     }

    //     if(command.getType() == kMidiNoteReleased) {
    //         auto& notes = IRECS::getSystem<INPUT_MIDI_MESSAGE_IN>()->
    //             getMidiNotesOffThisFrame(device);
    //         for(int i = 0; i < notes.size(); ++i) {
    //             command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
    //         }
    //     }
    // }


    // IRCCData CommandManager::checkCCMessage(int device, IRCCMessage ccMessage) {
    //     return IRECS::getSystem<INPUT_MIDI_MESSAGE_IN>()->
    //         checkCCMessageReceived(device, ccMessage);
    // }

} // namespace IRCommand