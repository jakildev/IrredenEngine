/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\commands\command_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "command_manager.hpp"
#include "../entity/system_manager.hpp"

#include "../game_systems/system_input_key_mouse.hpp"
#include "../game_systems/system_input_gamepad.hpp"
#include "../game_systems/system_input_midi_message_in.hpp"

namespace IRCommands {

    void CommandManager::executeSystemCommands(IRSystemName systemName) {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        const auto& systemCommandList =
            m_systemCommands[systemName];
        for(int i = 0; i < systemCommandList.size(); ++i) {
            const auto& command = systemCommandList[i];
            if(checkButton(command.getType(), command.getButton()))
            {
                command.execute();
            }
        }
    }

    void CommandManager::executeSystemEntityCommands(IRSystemName systemName) {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        const auto& systemEntityCommandList =
            m_systemEntityCommands[systemName];
        for(int i = 0; i < systemEntityCommandList.size(); ++i) {
            executeEntityCommand(systemEntityCommandList[i]);
        }
    }

    void CommandManager::executeEntityCommand(IRCommandNames commandName) {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        const auto& command =
            m_entityCommands.at(commandName);
        if(global.systemManager_->get<INPUT_KEY_MOUSE>()->
            checkButton(
                command.getType(),
                (IRKeyMouseButtons)command.getButton()
            )
        )
        {
            const auto& boundEntities = m_entitiesBoundToCommands[commandName];
            for(auto const& entity : boundEntities) {
                command.execute(entity);
            }
        }
    }

    void CommandManager::executeDeviceMidiCCCommandsAll() {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        for(auto& [device, commands] : m_midiCCDeviceCommands) {
            executeDeviceMidiCCCommands(device, commands);
        }
    }

    void CommandManager::executeDeviceMidiCCCommands(
        int device,
        std::vector<IRCommand<IR_COMMAND_MIDI_CC>>& commands
    )
    {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        for(int i = 0; i < commands.size(); ++i) {
            executeDeviceMidiCCCommand(device, commands[i]);
        }
    }

    void CommandManager::executeDeviceMidiCCCommand(
        int device,
        IRCommand<IR_COMMAND_MIDI_CC>& command
    )
    {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        IRCCData ccData = checkCCMessage(device, command.getCCMessage());
        if(ccData != kCCFalse) {
            command.execute(ccData);
        }
    }

    void CommandManager::executeDeviceMidiNoteCommandsAll() {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        for(auto& [device, commands] : m_midiNoteDeviceCommands) {
            executeDeviceMidiNoteCommands(device, commands);
        }
    }

    void CommandManager::executeUserKeyboardCommandsAll() {
        for(auto& command : m_userCommands) {
            if(checkButton(command.getType(), command.getButton()))
            {
                command.execute();
            }
        }
    }

    void CommandManager::executeDeviceMidiNoteCommands(
        int device,
        std::vector<IRCommand<IR_COMMAND_MIDI_NOTE>>& commands
    )
    {
        EASY_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
        for(int i = 0; i < commands.size(); ++i) {
            executeDeviceMidiNoteCommand(device, commands[i]);
        }
    }

    void CommandManager::executeDeviceMidiNoteCommand(
        int device,
        IRCommand<IR_COMMAND_MIDI_NOTE>& command
    )
    {
        if(command.getType() == kMidiNotePressed) {
            auto& notes = global.systemManager_->get<INPUT_MIDI_MESSAGE_IN>()->
                getMidiNotesOnThisFrame(device);
            for(int i = 0; i < notes.size(); ++i) {
                command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
            }
        }

        if(command.getType() == kMidiNoteReleased) {
            auto& notes = global.systemManager_->get<INPUT_MIDI_MESSAGE_IN>()->
                getMidiNotesOffThisFrame(device);
            for(int i = 0; i < notes.size(); ++i) {
                command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
            }
        }
    }

    bool CommandManager::checkButton(IRInputTypes type, int button) {
        if(
            type == IRInputTypes::kKeyMouseButtonPressed ||
            type == IRInputTypes::kKeyMouseButtonReleased ||
            type == IRInputTypes::kKeyMouseButtonDown
        )
        {
            return global.systemManager_->get<INPUT_KEY_MOUSE>()->checkButton(
                type,
                (IRKeyMouseButtons)button
            );
        }
        if(
            type == IRInputTypes::kGamepadButtonPressed ||
            type == IRInputTypes::kGamepadButtonReleased ||
            type == IRInputTypes::kGamepadButtonDown
        )
        {
            return global.systemManager_->get<INPUT_GAMEPAD>()->checkButton(
                type,
                (IRGamepadButtons)button
            );
        }
        return false;
    }

    IRCCData CommandManager::checkCCMessage(int device, IRCCMessage ccMessage) {
        return global.systemManager_->get<INPUT_MIDI_MESSAGE_IN>()->
            checkCCMessageReceived(device, ccMessage);
    }

} // namespace IRCommands