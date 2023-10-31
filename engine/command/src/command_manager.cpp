/*
 * Project: Irreden Engine
 * File: command_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/command/command_manager.hpp>
#include <irreden/ecs/system_manager.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/systems/input/system_input_gamepad.hpp>
#include <irreden/systems/input/system_input_midi_message_in.hpp>

namespace IRCommands {

    void CommandManager::executeSystemCommands(IRSystemName systemName) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
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
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        const auto& systemEntityCommandList =
            m_systemEntityCommands[systemName];
        for(int i = 0; i < systemEntityCommandList.size(); ++i) {
            executeEntityCommand(systemEntityCommandList[i]);
        }
    }

    void CommandManager::executeEntityCommand(IRCommandNames commandName) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        const auto& command =
            m_entityCommands.at(commandName);
        if(IRECS::getSystem<INPUT_KEY_MOUSE>()->
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
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        for(auto& [device, commands] : m_midiCCDeviceCommands) {
            executeDeviceMidiCCCommands(device, commands);
        }
    }

    void CommandManager::executeDeviceMidiCCCommands(
        int device,
        std::vector<IRCommand<IR_COMMAND_MIDI_CC>>& commands
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        for(int i = 0; i < commands.size(); ++i) {
            executeDeviceMidiCCCommand(device, commands[i]);
        }
    }

    void CommandManager::executeDeviceMidiCCCommand(
        int device,
        IRCommand<IR_COMMAND_MIDI_CC>& command
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        IRCCData ccData = checkCCMessage(device, command.getCCMessage());
        if(ccData != kCCFalse) {
            command.execute(ccData);
        }
    }

    void CommandManager::executeDeviceMidiNoteCommandsAll() {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
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
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
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
            auto& notes = IRECS::getSystem<INPUT_MIDI_MESSAGE_IN>()->
                getMidiNotesOnThisFrame(device);
            for(int i = 0; i < notes.size(); ++i) {
                command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
            }
        }

        if(command.getType() == kMidiNoteReleased) {
            auto& notes = IRECS::getSystem<INPUT_MIDI_MESSAGE_IN>()->
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
            return IRECS::getSystem<INPUT_KEY_MOUSE>()->checkButton(
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
            return IRECS::getSystem<INPUT_GAMEPAD>()->checkButton(
                type,
                (IRGamepadButtons)button
            );
        }
        return false;
    }

    IRCCData CommandManager::checkCCMessage(int device, IRCCMessage ccMessage) {
        return IRECS::getSystem<INPUT_MIDI_MESSAGE_IN>()->
            checkCCMessageReceived(device, ccMessage);
    }

} // namespace IRCommands