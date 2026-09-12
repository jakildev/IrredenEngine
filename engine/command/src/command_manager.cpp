#include <irreden/ir_input.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/command/command_manager.hpp>

#include <irreden/input/systems/system_input_gamepad.hpp>
#include <irreden/audio/systems/system_audio_midi_message_in.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace IRCommand {

namespace {

/// Identity of a bare-mask binding group: the button it fires on, packed with
/// the `blockedModifiers` mask it carries. Both halves matter — two rows on the
/// same key with different blocked masks are deliberately *different* groups,
/// since a mask that rejects one of them would silently unpair the other.
/// Bindings with a non-empty `requiredModifiers` never form a group.
std::uint32_t bareGroupKey(int button, IRInput::KeyModifierMask blockedModifiers) {
    return (static_cast<std::uint32_t>(button) << 8) | static_cast<std::uint32_t>(blockedModifiers);
}

int groupButton(std::uint32_t group) {
    return static_cast<int>(group >> 8);
}

IRInput::KeyModifierMask groupBlockedModifiers(std::uint32_t group) {
    return static_cast<IRInput::KeyModifierMask>(group & 0xFFu);
}

/// Membership test for the short lists dispatch keeps — a handful of entries
/// each, so a scan is cheaper than a hash and allocates nothing.
template <typename T> bool containsValue(const std::vector<T> &values, T value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

/// Which edges a bare group has a row for. A group holding both is a start/end
/// pair and gets the admission latch; anything else keeps frame-by-frame
/// matching. `HELD` rows join whichever group they share a button and mask
/// with, but never make one a pair on their own.
constexpr std::uint8_t kGroupEdgePressed = 1 << 0;
constexpr std::uint8_t kGroupEdgeReleased = 1 << 1;
constexpr std::uint8_t kGroupEdgeBoth = kGroupEdgePressed | kGroupEdgeReleased;

} // namespace

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
    executeUserKeyboardCommands(
        KeyMouseInputProbe{&IRInput::checkKeyMouseButton, &IRInput::checkKeyMouseModifiers}
    );
}

void CommandManager::rebuildBareGroups() {
    m_bareGroups.clear();
    m_rowBareGroup.assign(m_userCommands.size(), kNoBareGroup);
    for (std::size_t row = 0; row < m_userCommands.size(); ++row) {
        const CommandStruct<COMMAND_BUTTON> &command = m_userCommands[row];
        if (command.getRequiredModifiers() != kModifierNone) {
            continue;
        }
        std::uint8_t edge = 0;
        if (command.getTriggerStatus() == PRESSED) {
            edge = kGroupEdgePressed;
        } else if (command.getTriggerStatus() == RELEASED) {
            edge = kGroupEdgeReleased;
        }
        const std::uint32_t key = bareGroupKey(command.getButton(), command.getBlockedModifiers());
        int index = kNoBareGroup;
        for (std::size_t i = 0; i < m_bareGroups.size(); ++i) {
            if (m_bareGroups[i].key_ == key) {
                index = static_cast<int>(i);
                break;
            }
        }
        if (index == kNoBareGroup) {
            index = static_cast<int>(m_bareGroups.size());
            m_bareGroups.push_back({key, edge});
        } else {
            m_bareGroups[static_cast<std::size_t>(index)].edges_ |= edge;
        }
        m_rowBareGroup[row] = index;
    }
}

void CommandManager::executeUserKeyboardCommands(const KeyMouseInputProbe &input) {
    if (m_bareGroupsDirty) {
        rebuildBareGroups();
        m_bareGroupsDirty = false;
    }

    m_modifierSpecificButtons.clear();
    for (auto &command : m_userCommands) {
        if (command.getRequiredModifiers() == kModifierNone) {
            continue;
        }
        if (input.checkButton_(
                static_cast<IRInput::KeyMouseButtons>(command.getButton()),
                command.getTriggerStatus()
            ) &&
            input.checkModifiers_(command.getRequiredModifiers(), command.getBlockedModifiers()) &&
            !containsValue(m_modifierSpecificButtons, command.getButton())) {
            m_modifierSpecificButtons.push_back(command.getButton());
        }
    }

    // Admit or reject each pair on the frame its button is observed pressed.
    // This is the only frame the decision is made on: it reads the same masks
    // and the same specificity list the per-row pass below would, then stops
    // asking until the button comes back up.
    for (const BareGroup &group : m_bareGroups) {
        if (group.edges_ != kGroupEdgeBoth) {
            continue;
        }
        const int button = groupButton(group.key_);
        if (!input.checkButton_(static_cast<IRInput::KeyMouseButtons>(button), PRESSED)) {
            continue;
        }
        const bool admitted =
            input.checkModifiers_(kModifierNone, groupBlockedModifiers(group.key_)) &&
            !containsValue(m_modifierSpecificButtons, button);
        const bool wasAdmitted = containsValue(m_admittedPairedGroups, group.key_);
        if (admitted && !wasAdmitted) {
            m_admittedPairedGroups.push_back(group.key_);
        } else if (!admitted && wasAdmitted) {
            std::erase(m_admittedPairedGroups, group.key_);
        }
    }

    for (std::size_t row = 0; row < m_userCommands.size(); ++row) {
        CommandStruct<COMMAND_BUTTON> &command = m_userCommands[row];
        if (!input.checkButton_(
                static_cast<IRInput::KeyMouseButtons>(command.getButton()),
                command.getTriggerStatus()
            )) {
            continue;
        }
        const int groupIndex = m_rowBareGroup[row];
        const bool isPaired =
            groupIndex != kNoBareGroup &&
            m_bareGroups[static_cast<std::size_t>(groupIndex)].edges_ == kGroupEdgeBoth;
        if (isPaired) {
            // A pair's masks and shadowing were settled at the press; re-reading
            // them here is exactly what let a mid-hold modifier change fire one
            // half without the other.
            if (containsValue(
                    m_admittedPairedGroups,
                    m_bareGroups[static_cast<std::size_t>(groupIndex)].key_
                )) {
                command.execute();
            }
            continue;
        }
        if (!input.checkModifiers_(command.getRequiredModifiers(), command.getBlockedModifiers())) {
            continue;
        }
        if (command.getRequiredModifiers() == kModifierNone &&
            containsValue(m_modifierSpecificButtons, command.getButton())) {
            continue;
        }
        command.execute();
    }

    // Retire after dispatch, so a press and release seen in the same frame
    // (`PRESSED_AND_RELEASED`) still fires both halves under one admission.
    std::erase_if(m_admittedPairedGroups, [&input](std::uint32_t key) {
        return input.checkButton_(
            static_cast<IRInput::KeyMouseButtons>(groupButton(key)),
            RELEASED
        );
    });
}

void CommandManager::executeDeviceMidiNoteCommands(
    int device, std::vector<CommandStruct<COMMAND_MIDI_NOTE>> &commands
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_COMMANDS);
    for (int i = 0; i < commands.size(); ++i) {
        executeDeviceMidiNoteCommand(device, commands[i]);
    }
}

void CommandManager::fireUserCommand(CommandId id) {
    if (id >= m_userCommands.size()) {
        IRE_LOG_ERROR(
            "CommandManager::fireUserCommand: id {} out of range (size {})",
            id,
            m_userCommands.size()
        );
        return;
    }
    m_userCommands[id].execute();
}

bool CommandManager::isButtonBound(
    InputTypes inputType, ButtonStatuses triggerStatus, int button
) const {
    for (const CommandStruct<COMMAND_BUTTON> &command : m_userCommands) {
        if (command.getType() == inputType && command.getTriggerStatus() == triggerStatus &&
            command.getButton() == button) {
            return true;
        }
    }
    return false;
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