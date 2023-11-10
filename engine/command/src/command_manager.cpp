/*
 * Project: Irreden Engine
 * File: command_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_input.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/command/command_manager.hpp>

#include <irreden/input/systems/system_input_gamepad.hpp>
#include <irreden/audio/systems/system_audio_midi_message_in.hpp>

namespace IRCommand {

    CommandManager::CommandManager() {
        g_commandManager = this;
        IRProfile::engLogInfo("Created CommandManager");
    }


    void CommandManager::executeDeviceMidiCCCommandsAll() {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        for(auto& [device, commands] : m_midiCCDeviceCommands) {
            executeDeviceMidiCCCommands(device, commands);
        }
    }

    void CommandManager::executeDeviceMidiCCCommands(
        int device,
        std::vector<Command<IR_COMMAND_MIDI_CC>>& commands
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        for(int i = 0; i < commands.size(); ++i) {
            executeDeviceMidiCCCommand(device, commands[i]);
        }
    }

    void CommandManager::executeDeviceMidiCCCommand(
        int device,
        Command<IR_COMMAND_MIDI_CC>& command
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        CCData ccData = checkCCMessage(device, command.getCCMessage());
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
            if(IRInput::checkKeyMouseButton(
                static_cast<IRInput::KeyMouseButtons>(command.getButton()),
                command.getTriggerStatus()
            ))
            {
                command.execute();
            }
        }
    }

    void CommandManager::executeDeviceMidiNoteCommands(
        int device,
        std::vector<Command<IR_COMMAND_MIDI_NOTE>>& commands
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_COMMANDS);
        for(int i = 0; i < commands.size(); ++i) {
            executeDeviceMidiNoteCommand(device, commands[i]);
        }
    }

    void CommandManager::executeDeviceMidiNoteCommand(
        int device,
        Command<IR_COMMAND_MIDI_NOTE>& command
    )
    {
        if(command.getType() == MIDI_NOTE && command.getTriggerStatus() == PRESSED) {
            auto& notes = IRAudio::getMidiNotesOnThisFrame(device);
            for(int i = 0; i < notes.size(); ++i) {
                command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
            }
        }

        if(command.getType() == MIDI_NOTE && command.getTriggerStatus() == RELEASED) {
            auto& notes = IRAudio::getMidiNotesOffThisFrame(device);
            for(int i = 0; i < notes.size(); ++i) {
                command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
            }
        }
    }

} // namespace IRCommand