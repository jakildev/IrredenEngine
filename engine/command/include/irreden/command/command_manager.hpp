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

/// The two `IRInput` predicates keyboard dispatch reads live input through:
/// `checkKeyMouseButton` and `checkKeyMouseModifiers`. `CommandManager` only
/// ever touches input through this pair, so binding them to a deterministic
/// snapshot runs the production selection algorithm with no window, no GLFW
/// and no `InputManager` — the seam the headless dispatch fixture in
/// `test/command/keyboard_dispatch_test.cpp` drives.
///
/// Plain function pointers, not `std::function`: the probe is threaded through
/// a per-frame loop over every binding, and the production binding below is
/// two direct addresses with nothing to own or allocate. This is command-
/// internal — `ir_command.hpp` exports no counterpart, and a creation still
/// calls `executeUserKeyboardCommandsAll()`.
struct KeyMouseInputProbe {
    bool (*checkButton_)(IRInput::KeyMouseButtons button, IRInput::ButtonStatuses status);
    bool (*checkModifiers_)(
        IRInput::KeyModifierMask requiredModifiers, IRInput::KeyModifierMask blockedModifiers
    );
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
        m_bareGroupsDirty = true;
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
    /// Per-frame keyboard/mouse dispatch: reads live input and fires every
    /// binding whose trigger matches. Thin wrapper over the evaluator below,
    /// bound to the real `IRInput` predicates.
    void executeUserKeyboardCommandsAll();

    /// The dispatch algorithm itself, reading input through @p input instead of
    /// calling `IRInput` directly. Command-internal: the only production caller
    /// is `executeUserKeyboardCommandsAll()`; the only other one is the headless
    /// fixture that drives it from a snapshot.
    ///
    /// Two selection rules run here, and the second exists because the first is
    /// frame-local by construction:
    ///
    /// - **Modifier specificity.** If any binding on a button matches this frame
    ///   with a non-empty `requiredModifiers`, every bare-mask binding on that
    ///   button is skipped for the frame — a chord wins over the plain key.
    /// - **Paired admission.** Bare-mask bindings that share a button
    ///   *and* a `blockedModifiers` mask form a group; a group carrying both a
    ///   `PRESSED` and a `RELEASED` row is a start/end *pair*. A pair's
    ///   eligibility is decided once, on the frame its button is observed
    ///   pressed, and held until the button is released — so a chord that
    ///   shadows the press also shadows the matching release, and a modifier
    ///   pressed mid-hold cannot cancel a release that has a live start behind
    ///   it. Without it, specificity reaches only the press frame (nothing
    ///   modifier-specific matches on the release frame), leaving an unpaired
    ///   `_END` that accumulates into the camera's velocity forever.
    ///
    /// Unpaired bare bindings and every modifier-bearing one keep plain
    /// frame-by-frame matching.
    ///
    /// Grouping walks the same rows, with the same predicate, that dispatch
    /// does — in particular it consults `getType()` exactly as much as dispatch
    /// does, which is not at all — `isButtonBound` documents that latent
    /// divergence. One predicate for both passes is deliberate: a finer filter
    /// here would let the group set and the dispatch set disagree, and a row
    /// that fires on both edges but never latches is the defect again.
    void executeUserKeyboardCommands(const KeyMouseInputProbe &input);
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

    /// One bare-mask binding group: `(button, blockedModifiers)` packed into
    /// `key_` (the identity rows pair on), plus which trigger edges have rows.
    struct BareGroup {
        std::uint32_t key_;
        std::uint8_t edges_;
    };
    static constexpr int kNoBareGroup = -1;

    /// The bare-mask groups every binding falls into, and the group index of
    /// each row in `m_userCommands` (`kNoBareGroup` for a modifier-bearing row).
    /// Derived purely from the binding list, which is append-only, so they are
    /// rebuilt on the first dispatch after a registration rather than per frame
    /// — that rebuild is the only O(bindings x groups) work in the path, and it
    /// happens once per `createCommand` burst instead of 60 times a second.
    std::vector<BareGroup> m_bareGroups;
    std::vector<int> m_rowBareGroup;
    bool m_bareGroupsDirty = true;

    /// Per-frame scratch — the few buttons a chord matched this frame. A member
    /// so dispatch stops allocating once it reaches its high-water mark
    /// (`clear()` keeps the capacity), and a vector because a handful of entries
    /// scanned linearly beats hashing here.
    ///
    /// Measured over a ~54-row binding population (the voxel editor's), ns per
    /// `executeUserKeyboardCommands` call: **134** without paired admission at
    /// all, **1075** for a draft that rebuilt an `unordered_map` +
    /// `unordered_set` every frame, **~190** here. Caching the group table and
    /// scanning short vectors are what buy back the difference.
    std::vector<int> m_modifierSpecificButtons;

    /// Start/end pairs whose press was admitted and whose release has not been
    /// observed yet. Membership IS the admission: a pair fires neither edge
    /// while absent, both while present. Entries are written on the press frame
    /// and erased on the release frame, so this is empty whenever no paired key
    /// is down — unlike the two scratch lists above it persists across frames,
    /// because that persistence is the fix. See `executeUserKeyboardCommands`.
    std::vector<std::uint32_t> m_admittedPairedGroups;

    /// Rebuilds `m_bareGroups` / `m_rowBareGroup` from the binding list.
    void rebuildBareGroups();
    std::uint32_t m_registrationGeneration = 0;
};

} // namespace IRCommand

#endif /* COMMAND_MANAGER_H */
