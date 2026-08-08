#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <irreden/ir_input.hpp>
#include <irreden/ir_audio.hpp>

#include <irreden/command/command.hpp>
#include <irreden/command/ir_command_types.hpp>
#include <irreden/common/components/component_tags_all.hpp>

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <list>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace IRInput;
using namespace IRAudio;

namespace IRCommand {

/// One row of the introspectable command registry: what a binding is called,
/// what it does, and which key/modifier combination fires it. Populated only
/// for named `PRESSED` bindings (see `createCommand`). Consumed by the help
/// overlay (`System<HELP_OVERLAY>`) and `IRCommand::buildCommandListText()`.
struct CommandRegistration {
    std::string name;
    /// Short human-readable clause describing the effect ("ZOOM THE CAMERA
    /// IN"). Empty when the registering call site supplied none — the
    /// overlay renders the binding + name alone in that case.
    std::string description;
    int button;
    ButtonStatuses triggerStatus;
    KeyModifierMask requiredModifiers;
};

class CommandManager {
  public:
    CommandManager();
    ~CommandManager();

    template <typename Function>
    CommandId createCommand(
        InputTypes inputType,
        ButtonStatuses triggerStatus,
        int button,
        Function command,
        KeyModifierMask requiredModifiers = kModifierNone,
        KeyModifierMask blockedModifiers = kModifierNone,
        std::string name = "",
        std::string description = ""
    ) {
        m_userCommands.emplace_back(
            CommandStruct<COMMAND_BUTTON>{
                inputType,
                triggerStatus,
                button,
                command,
                requiredModifiers,
                blockedModifiers
            }
        );
        if (!name.empty() && triggerStatus == PRESSED) {
            m_commandRegistrations.push_back(
                {std::move(name), std::move(description), button, triggerStatus, requiredModifiers}
            );
            ++m_registrationGeneration;
        }
        return static_cast<CommandId>(m_userCommands.size() - 1);
    }

    /// Bounds-checked execute of the user command at @p id. Out-of-range
    /// ids log an error and return without firing — there is no exception.
    /// `id` is the value returned by `createCommand` (or `IRCommand.bindPrefab`
    /// / `IRCommand.createCommand` on the Lua side). Used by
    /// `IRCommand::fire` to invoke a registered command imperatively from
    /// C++ or Lua, outside the regular input-driven dispatch loop.
    void fireUserCommand(CommandId id);

    /// Reports whether a binding already exists for the (@p inputType,
    /// @p triggerStatus, @p button) triple. Scans `m_userCommands`, not
    /// `m_commandRegistrations` — the registry omits unnamed bindings and every
    /// non-`PRESSED` one, so the camera suite's RELEASED `MOVE_CAMERA_*_END`
    /// rows are invisible there but visible here.
    ///
    /// Modifier masks are deliberately ignored: a row carrying
    /// `requiredModifiers` / `blockedModifiers` still counts as bound, so a
    /// caller guarding an ad-hoc bind sees the key as taken whatever
    /// combination sits behind it. Key-level granularity is the whole
    /// contract — a modifier-aware overload can come later if a caller needs
    /// one. This reports the *data*; collision **policy** stays with the
    /// caller, and `createCommand` still appends unconditionally.
    ///
    /// The match is *type-exact*, which is finer than what actually
    /// dispatches: `executeUserKeyboardCommandsAll` — the only tick-path
    /// reader of `m_userCommands` — never consults `getType()`, it runs
    /// `IRInput::checkKeyMouseButton` over every row. A row created with a
    /// non-`KEY_MOUSE` @p inputType would therefore fire on the matching
    /// keyboard press while this query calls it unbound. Latent, not live:
    /// every button binding in the tree is registered `KEY_MOUSE` and there is
    /// no gamepad dispatch loop at all, so no row can currently misreport. The
    /// type check stays because it is the contract a `GAMEPAD` dispatch loop
    /// would need; the missing filter is on the dispatcher's side.
    ///
    /// MIDI note/CC bindings are registered through `registerMidiNoteCommand`
    /// / `registerMidiCCCommand` into their own per-device maps, never into
    /// `m_userCommands`, so `MIDI_NOTE` / `MIDI_CC` report false. That holds
    /// by *population*, not by construction — `createCommand(MIDI_NOTE, …)`
    /// would land a button row like any other — but nothing makes one.
    ///
    /// Cost: O(bindings) linear scan — an init/registration-time guard query,
    /// not a per-tick call.
    bool isButtonBound(InputTypes inputType, ButtonStatuses triggerStatus, int button) const;

    const std::vector<CommandRegistration> &getCommandRegistrations() const {
        return m_commandRegistrations;
    }

    /// Monotonic counter bumped every time `m_commandRegistrations` actually
    /// grows. A consumer that caches text built from the registry compares
    /// this against its own snapshot to know when to rebuild — commands
    /// registered after the first rendered frame would otherwise never
    /// appear. Bumped on append only, so a cache keyed on it costs one
    /// integer compare per frame in the steady state.
    std::uint32_t getRegistrationGeneration() const {
        return m_registrationGeneration;
    }

    template <typename Function, typename... Args>
    int
    registerMidiNoteCommand(int device, InputTypes InputType, Function command, Args... fixedArgs) {
        if (!m_midiCCDeviceCommands.contains(device)) {
            m_midiNoteDeviceCommands.emplace(
                device,
                std::vector<CommandStruct<COMMAND_MIDI_NOTE>>{}
            );
        }
        m_midiNoteDeviceCommands[device].emplace_back(
            CommandStruct<COMMAND_MIDI_NOTE>{InputType, command, fixedArgs...}
        );
        return m_midiNoteDeviceCommands[device].size() - 1;
    }

    template <typename Function>
    int registerMidiCCCommand(
        int device, InputTypes InputType, unsigned char ccMessage, Function command
    ) {
        if (!m_midiCCDeviceCommands.contains(device)) {
            m_midiCCDeviceCommands.emplace(device, std::vector<CommandStruct<COMMAND_MIDI_CC>>{});
        }
        m_midiCCDeviceCommands[device].emplace_back(
            CommandStruct<COMMAND_MIDI_CC>{InputType, ccMessage, command}
        );
        return m_midiCCDeviceCommands[device].size() - 1;
    }

    void executeDeviceMidiCCCommandsAll();
    void executeDeviceMidiNoteCommandsAll();
    void executeUserKeyboardCommandsAll();
    void executeDeviceMidiCCCommands(
        int device, std::vector<CommandStruct<CommandTypes::COMMAND_MIDI_CC>> &commands
    );
    void executeDeviceMidiCCCommand(int device, CommandStruct<COMMAND_MIDI_CC> &command);
    void executeDeviceMidiNoteCommands(
        int device, std::vector<CommandStruct<COMMAND_MIDI_NOTE>> &commands
    );
    void executeDeviceMidiNoteCommand(int device, CommandStruct<COMMAND_MIDI_NOTE> &command);

  private:
    std::unordered_map<int, std::vector<CommandStruct<COMMAND_MIDI_NOTE>>> m_midiNoteDeviceCommands;
    std::unordered_map<int, std::vector<CommandStruct<COMMAND_MIDI_CC>>> m_midiCCDeviceCommands;
    std::vector<CommandStruct<COMMAND_BUTTON>> m_userCommands;
    std::vector<CommandRegistration> m_commandRegistrations;
    std::uint32_t m_registrationGeneration = 0;
};

} // namespace IRCommand

#endif /* COMMAND_MANAGER_H */
