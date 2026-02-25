#include <irreden/ir_input.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/command/command_manager.hpp>

#include <irreden/input/systems/system_input_gamepad.hpp>
#include <irreden/audio/systems/system_audio_midi_message_in.hpp>
#include <unordered_set>

namespace IRCommand {

CommandManager::CommandManager() {
    g_commandManager = this;
    IRE_LOG_INFO("Created CommandManager");
}

CommandManager::~CommandManager() {
    if (g_commandManager == this) {
        g_commandManager = nullptr;
    }
}

void CommandManager::executeDeviceMidiCCCommandsAll() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
    for (auto &[device, commands] : m_midiCCDeviceCommands) {
        executeDeviceMidiCCCommands(device, commands);
    }
}

void CommandManager::executeDeviceMidiCCCommands(
    int device, std::vector<CommandStruct<COMMAND_MIDI_CC>> &commands
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
    for (int i = 0; i < commands.size(); ++i) {
        executeDeviceMidiCCCommand(device, commands[i]);
    }
}

void CommandManager::executeDeviceMidiCCCommand(
    int device, CommandStruct<COMMAND_MIDI_CC> &command
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
    CCData ccData = checkCCMessage(device, command.getCCMessage());
    if (ccData != kCCFalse) {
        command.execute(ccData);
    }
}

void CommandManager::executeDeviceMidiNoteCommandsAll() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
    for (auto &[device, commands] : m_midiNoteDeviceCommands) {
        executeDeviceMidiNoteCommands(device, commands);
    }
}

void CommandManager::executeUserKeyboardCommandsAll() {
    std::unordered_set<int> buttonsWithModifierSpecificMatch;
    for (auto &command : m_userCommands) {
        if (command.getRequiredModifiers() == kModifierNone) {
            continue;
        }
        if (IRInput::checkKeyMouseButton(
                static_cast<IRInput::KeyMouseButtons>(command.getButton()),
                command.getTriggerStatus()
            ) &&
            IRInput::checkKeyMouseModifiers(
                command.getRequiredModifiers(),
                command.getBlockedModifiers()
            )) {
            buttonsWithModifierSpecificMatch.insert(command.getButton());
        }
    }

    for (auto &command : m_userCommands) {
        if (IRInput::checkKeyMouseButton(
                static_cast<IRInput::KeyMouseButtons>(command.getButton()),
                command.getTriggerStatus()
            ) &&
            IRInput::checkKeyMouseModifiers(
                command.getRequiredModifiers(),
                command.getBlockedModifiers()
            )) {
            if (command.getRequiredModifiers() == kModifierNone &&
                buttonsWithModifierSpecificMatch.contains(command.getButton())) {
                continue;
            }
            command.execute();
        }
    }
}

void CommandManager::executeDeviceMidiNoteCommands(
    int device, std::vector<CommandStruct<COMMAND_MIDI_NOTE>> &commands
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
    for (int i = 0; i < commands.size(); ++i) {
        executeDeviceMidiNoteCommand(device, commands[i]);
    }
}

void CommandManager::executeDeviceMidiNoteCommand(
    int device, CommandStruct<COMMAND_MIDI_NOTE> &command
) {
    if (command.getType() == MIDI_NOTE && command.getTriggerStatus() == PRESSED) {
        auto &notes = IRAudio::getMidiNotesOnThisFrame(device);
        for (int i = 0; i < notes.size(); ++i) {
            command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
        }
    }

    if (command.getType() == MIDI_NOTE && command.getTriggerStatus() == RELEASED) {
        auto &notes = IRAudio::getMidiNotesOffThisFrame(device);
        for (int i = 0; i < notes.size(); ++i) {
            command.execute(notes[i].getMidiNoteNumber(), notes[i].getMidiNoteVelocity());
        }
    }
}

} // namespace IRCommand