#ifndef IR_COMMAND_ACTION_REGISTRY_H
#define IR_COMMAND_ACTION_REGISTRY_H

#include <irreden/input/ir_input_types.hpp>

#include <irreden/command/ir_command_types.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace IRCommand {

class CommandManager;

/// One keyboard trigger: a key plus the exact modifier set that must be held.
/// A chord matches only its own mask, so `CTRL+K` and `CTRL+SHIFT+K` are
/// distinct chords and a bare `K` does not fire while a modifier is down.
struct KeyChord {
    IRInput::KeyMouseButtons key_ = IRInput::kNullButton;
    IRInput::KeyModifierMask modifiers_ = IRInput::kModifierNone;

    bool operator==(const KeyChord &) const = default;
};

/// The context every action belongs to unless it names another. A global
/// action is live everywhere and collides with a chord in any context.
inline constexpr std::string_view kGlobalActionContext = "global";

/// What a creation, a widget or a script registers. `id_` is the stable key the
/// keymap file stores: namespaced and lowercase (`editor.tool.paint`), never a
/// display string, so labels can be reworded without orphaning user bindings.
struct ActionSpec {
    std::string id_;
    std::string label_;
    std::string section_;
    std::string description_;
    std::string context_ = std::string(kGlobalActionContext);
    std::vector<KeyChord> defaultChords_;
    /// The operation. Keyboard dispatch, `invokeAction` and widget buttons all
    /// call this one callable.
    std::function<void()> invoke_;
    /// Non-null makes the action a hold: `invoke_` starts it on the press and
    /// `release_` ends it once, when the last of its chords pressed during the
    /// hold is released or when its binding changes underneath the hold. A
    /// second chord pressed mid-hold joins it rather than restarting it, and a
    /// chord whose press the hold never admitted cannot end it.
    std::function<void()> release_;
    /// Null means always available. An unavailable action neither fires from a
    /// key nor from `invokeAction`.
    std::function<bool()> available_;
};

/// A read-only view of one registered action with its resolved chords.
struct ActionInfo {
    std::string id_;
    std::string label_;
    std::string section_;
    std::string description_;
    std::string context_;
    std::vector<KeyChord> chords_;
    std::vector<KeyChord> defaultChords_;
    bool available_ = true;
    bool customized_ = false;
    bool held_ = false;
};

enum class ConflictPolicy {
    /// Leave every binding untouched and report the collision.
    REJECT,
    /// Take the chord from each conflicting action first. A plain command row
    /// (`createCommand`) cannot be displaced, so it still rejects.
    REPLACE,
};

enum class BindStatus {
    BOUND,
    ALREADY_BOUND,
    CONFLICT,
    INVALID_CHORD,
    UNKNOWN_ACTION,
    NOT_BOUND,
};

struct ActionConflict {
    /// The action holding the chord; empty when a plain command row does.
    std::string actionId_;
    std::string label_;
};

struct BindResult {
    BindStatus status_ = BindStatus::BOUND;
    std::vector<ActionConflict> conflicts_;

    bool ok() const {
        return status_ == BindStatus::BOUND;
    }
};

/// Effective chords for each customized action id. An id absent from the map
/// uses its defaults; an empty vector means deliberately unbound. Ids with no
/// registered action are kept, so a keymap round-trips actions from an
/// extension that is not loaded right now.
using ActionOverrides = std::map<std::string, std::vector<KeyChord>>;

/// The searchable action catalog behind runtime key assignment.
///
/// Owned by `CommandManager`; every chord occupies ordinary rows in its button
/// list, so text capture, modifier specificity and paired admission apply to
/// actions exactly as to `createCommand` bindings. Rebinding mutates those rows
/// in place and unbinding parks them, so no `CommandId` is invalidated and the
/// per-frame dispatch loop carries no tombstones. All mutators are
/// registration- and UI-time operations, O(actions × chords); none runs per
/// frame.
class ActionRegistry {
  public:
    explicit ActionRegistry(CommandManager &commands);

    ActionRegistry(const ActionRegistry &) = delete;
    ActionRegistry &operator=(const ActionRegistry &) = delete;

    /// Registers @p spec, or re-registers an id that was registered before:
    /// the callables, labels and defaults are replaced while a user override
    /// for the id is kept, so a recreated panel or a reloaded script keeps its
    /// bindings. Returns false for an empty id or a null `invoke_`.
    bool registerAction(ActionSpec spec);

    /// Turns each named `createCommand` binding registered so far into an
    /// action, so a creation's existing key commands become searchable and
    /// assignable without touching their call sites. The id is
    /// `<idPrefix>.<slug of the name>`; bindings sharing a name become one
    /// action with one default chord each, and each default chord keeps firing
    /// its own body with its original blocked mask. @p sectionFor names the
    /// section per display name (null: `idPrefix` uppercased). Start/end pairs,
    /// mouse rows and unnamed rows stay plain commands; their `CommandId`s, and
    /// every adopted one, keep working with `fire`. Returns how many bindings
    /// were adopted.
    std::size_t adoptNamedCommands(
        std::string_view idPrefix,
        const std::function<std::string(std::string_view name)> &sectionFor = {}
    );

    /// Parks every chord row of @p id and drops its callables. Its override, if
    /// any, stays in `overrides()` and returns with the next registration.
    void unregisterAction(std::string_view id);

    bool isRegistered(std::string_view id) const;

    /// Runs the action once, as a key press would. A hold runs its start and its
    /// end back to back. Returns false when the id is unknown or unavailable.
    bool invokeAction(std::string_view id);

    /// Turns a named context on or off. An action whose context is off is
    /// unavailable; the global context is always on.
    void setContextActive(std::string_view context, bool active);
    bool isContextActive(std::string_view context) const;

    BindResult
    addChord(std::string_view id, KeyChord chord, ConflictPolicy policy = ConflictPolicy::REJECT);
    /// Swaps @p previous for @p chord at the same position in the action's list.
    /// When @p chord is already one of the action's chords, @p previous is
    /// removed instead, so the action never holds a chord twice.
    BindResult replaceChord(
        std::string_view id,
        KeyChord previous,
        KeyChord chord,
        ConflictPolicy policy = ConflictPolicy::REJECT
    );
    BindResult removeChord(std::string_view id, KeyChord chord);
    BindResult clearChords(std::string_view id);
    BindResult restoreDefaults(std::string_view id);
    void restoreAllDefaults();

    /// Who already holds @p chord in a context overlapping @p context, other
    /// than @p excludeId. Plain command rows are reported with an empty id.
    std::vector<ActionConflict>
    findConflicts(KeyChord chord, std::string_view context, std::string_view excludeId = {}) const;

    std::optional<ActionInfo> findAction(std::string_view id) const;
    /// The first resolved chord of a registered action, without copying its
    /// description; nullopt when it is unbound or unknown. What a badge shows.
    std::optional<KeyChord> firstChord(std::string_view id) const;
    /// Every registered action, ordered by section, then label, then id.
    std::vector<ActionInfo> listActions() const;
    /// Registered actions whose label, id, section, description or a chord
    /// contains @p query, case-insensitively, in `listActions` order. A query
    /// that parses as a chord (`ctrl+k`) also matches that exact chord. An
    /// empty query lists everything.
    std::vector<ActionInfo> searchActions(std::string_view query) const;

    const ActionOverrides &overrides() const {
        return m_overrides;
    }

    /// Replaces every override with @p overrides and re-resolves each
    /// registered action. Chords failing validation are dropped per action.
    void setOverrides(ActionOverrides overrides);

    /// Bumped by every change to an action's chords, registration or
    /// availability context. Hint caches compare it against a snapshot.
    std::uint32_t generation() const {
        return m_generation;
    }

    /// Called after each binding change a user can make (add, replace, remove,
    /// clear, restore). The keymap store installs a writer here.
    void setOnBindingsChanged(std::function<void()> callback) {
        m_onBindingsChanged = std::move(callback);
    }

  private:
    struct ChordRows {
        CommandId press_ = 0;
        CommandId release_ = 0;
        bool hasRelease_ = false;
        bool active_ = false;
    };

    /// A default chord that came from `adoptNamedCommands`: the plain row whose
    /// body it fires and the blocked mask that row matched with.
    struct AdoptedChord {
        KeyChord chord_;
        CommandId row_ = 0;
        IRInput::KeyModifierMask blocked_ = IRInput::kModifierNone;
    };

    struct Action {
        ActionSpec spec_;
        std::vector<KeyChord> chords_;
        std::vector<ChordRows> rows_;
        std::vector<AdoptedChord> adopted_;
        /// Chord slots whose press the live hold admitted; empty when no hold
        /// is live.
        std::vector<std::size_t> heldSlots_;
        bool registered_ = false;
    };

    Action *find(std::string_view id);
    const Action *find(std::string_view id) const;
    bool available(const Action &action) const;
    bool contextsOverlap(std::string_view a, std::string_view b) const;
    ActionInfo info(const Action &action) const;

    void onPress(std::size_t index, std::size_t slot);
    const AdoptedChord *adoptedFor(const Action &action, KeyChord chord) const;
    void onRelease(std::size_t index, std::size_t slot);
    void endHold(Action &action);

    /// Points the action's rows at `chords_`, reusing parked rows before
    /// appending new ones, and ends a live hold first.
    void syncRows(std::size_t index);
    /// Parks the rows of every chord slot from @p firstSlot on.
    void parkRows(Action &action, std::size_t firstSlot = 0);
    void storeOverride(const Action &action);
    void publish();
    void notifyChanged();
    BindResult applyChords(
        std::size_t index, std::vector<KeyChord> chords, ConflictPolicy policy, KeyChord probe
    );

    CommandManager &m_commands;
    std::vector<Action> m_actions;
    std::unordered_map<std::string, std::size_t> m_index;
    ActionOverrides m_overrides;
    std::vector<std::string> m_activeContexts;
    std::function<void()> m_onBindingsChanged;
    std::uint32_t m_generation = 0;
};

/// A chord can be assigned when its key is a named keyboard key other than a
/// modifier, and its mask names only Shift / Control / Alt.
bool isAssignableChord(KeyChord chord);

/// Display text for @p chord, e.g. `CTRL+SHIFT+K`.
std::string chordLabel(KeyChord chord);

/// Stable, platform-independent spelling for files and scripts, e.g.
/// `ctrl+shift+K`. Modifier tokens are lowercase; the key is its display name.
std::string chordToken(KeyChord chord);

/// Inverse of `chordToken`; also accepts uppercase modifiers. Returns nullopt
/// for a token that names no key or carries an unknown modifier.
std::optional<KeyChord> parseChordToken(std::string_view token);

} // namespace IRCommand

#endif /* IR_COMMAND_ACTION_REGISTRY_H */
