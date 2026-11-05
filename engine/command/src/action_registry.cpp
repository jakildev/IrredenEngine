#include <irreden/ir_command.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/command/action_registry.hpp>
#include <irreden/command/command_manager.hpp>

#include <algorithm>
#include <cctype>
#include <tuple>
#include <utility>

namespace IRCommand {

namespace {

std::string toLower(std::string_view text) {
    std::string lowered(text);
    for (char &c : lowered) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lowered;
}

bool containsLower(std::string_view haystack, const std::string &needleLower) {
    return toLower(haystack).find(needleLower) != std::string::npos;
}

bool startsWithLower(std::string_view text, std::string_view prefixLower) {
    if (text.size() < prefixLower.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefixLower.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != prefixLower[i]) {
            return false;
        }
    }
    return true;
}

bool isModifierKey(IRInput::KeyMouseButtons key) {
    switch (key) {
    case IRInput::kKeyButtonLeftShift:
    case IRInput::kKeyButtonRightShift:
    case IRInput::kKeyButtonLeftControl:
    case IRInput::kKeyButtonRightControl:
    case IRInput::kKeyButtonLeftAlt:
    case IRInput::kKeyButtonRightAlt:
    case IRInput::kKeyButtonLeftSuper:
    case IRInput::kKeyButtonRightSuper:
    case IRInput::kKeyButtonMenu:
        return true;
    default:
        return false;
    }
}

/// Exact-mask matching: the chord's own modifiers are required and every other
/// one is blocked, so `CTRL+K` stays silent under `CTRL+SHIFT+K`.
IRInput::KeyModifierMask blockedFor(IRInput::KeyModifierMask modifiers) {
    return static_cast<IRInput::KeyModifierMask>(IRInput::kModifierAll & ~modifiers);
}

std::vector<KeyChord> validChords(const std::vector<KeyChord> &chords, std::string_view id) {
    std::vector<KeyChord> valid;
    valid.reserve(chords.size());
    for (const KeyChord &chord : chords) {
        if (!isAssignableChord(chord)) {
            IRE_LOG_WARN("Action '{}': dropping unassignable chord {}", id, chordToken(chord));
            continue;
        }
        if (std::find(valid.begin(), valid.end(), chord) == valid.end()) {
            valid.push_back(chord);
        }
    }
    return valid;
}

constexpr struct {
    std::string_view token_;
    IRInput::KeyModifierMask mask_;
} kModifierTokens[] = {
    {"shift+", IRInput::kModifierShift},
    {"ctrl+", IRInput::kModifierControl},
    {"control+", IRInput::kModifierControl},
    {"alt+", IRInput::kModifierAlt},
    {"option+", IRInput::kModifierAlt},
};

} // namespace

bool isAssignableChord(KeyChord chord) {
    const int key = static_cast<int>(chord.key_);
    if (key <= static_cast<int>(IRInput::kNullButton) ||
        key >= static_cast<int>(IRInput::kMouseButtonNull)) {
        return false;
    }
    if (isModifierKey(chord.key_)) {
        return false;
    }
    if ((chord.modifiers_ & ~IRInput::kModifierAll) != 0) {
        return false;
    }
    return keyButtonToString(key) != "?";
}

std::string chordLabel(KeyChord chord) {
    return modifierString(chord.modifiers_) + keyButtonToString(static_cast<int>(chord.key_));
}

std::string chordToken(KeyChord chord) {
    std::string token;
    if (chord.modifiers_ & IRInput::kModifierShift) {
        token += "shift+";
    }
    if (chord.modifiers_ & IRInput::kModifierControl) {
        token += "ctrl+";
    }
    if (chord.modifiers_ & IRInput::kModifierAlt) {
        token += "alt+";
    }
    return token + keyButtonToString(static_cast<int>(chord.key_));
}

std::optional<KeyChord> parseChordToken(std::string_view token) {
    KeyChord chord;
    bool consumed = true;
    while (consumed) {
        consumed = false;
        for (const auto &modifier : kModifierTokens) {
            if (token.size() > modifier.token_.size() && startsWithLower(token, modifier.token_)) {
                chord.modifiers_ |= modifier.mask_;
                token.remove_prefix(modifier.token_.size());
                consumed = true;
                break;
            }
        }
    }
    const std::string upper = [&] {
        std::string text(token);
        for (char &c : text) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return text;
    }();
    for (int key = static_cast<int>(IRInput::kNullButton) + 1;
         key < static_cast<int>(IRInput::kMouseButtonNull);
         ++key) {
        if (keyButtonToString(key) == upper) {
            chord.key_ = static_cast<IRInput::KeyMouseButtons>(key);
            return chord;
        }
    }
    return std::nullopt;
}

ActionRegistry::ActionRegistry(CommandManager &commands)
    : m_commands(commands) {}

ActionRegistry::Action *ActionRegistry::find(std::string_view id) {
    const auto it = m_index.find(std::string(id));
    return it == m_index.end() ? nullptr : &m_actions[it->second];
}

const ActionRegistry::Action *ActionRegistry::find(std::string_view id) const {
    const auto it = m_index.find(std::string(id));
    return it == m_index.end() ? nullptr : &m_actions[it->second];
}

bool ActionRegistry::isRegistered(std::string_view id) const {
    const Action *action = find(id);
    return action != nullptr && action->registered_;
}

bool ActionRegistry::isContextActive(std::string_view context) const {
    return context == kGlobalActionContext ||
           std::find(m_activeContexts.begin(), m_activeContexts.end(), context) !=
               m_activeContexts.end();
}

void ActionRegistry::setContextActive(std::string_view context, bool active) {
    if (context == kGlobalActionContext || isContextActive(context) == active) {
        return;
    }
    if (active) {
        m_activeContexts.emplace_back(context);
    } else {
        std::erase(m_activeContexts, context);
    }
    ++m_generation;
}

bool ActionRegistry::contextsOverlap(std::string_view a, std::string_view b) const {
    return a == b || a == kGlobalActionContext || b == kGlobalActionContext;
}

bool ActionRegistry::available(const Action &action) const {
    return action.registered_ && isContextActive(action.spec_.context_) &&
           (!action.spec_.available_ || action.spec_.available_());
}

ActionInfo ActionRegistry::info(const Action &action) const {
    return ActionInfo{
        action.spec_.id_,
        action.spec_.label_,
        action.spec_.section_,
        action.spec_.description_,
        action.spec_.context_,
        action.chords_,
        action.spec_.defaultChords_,
        available(action),
        m_overrides.contains(action.spec_.id_),
        static_cast<bool>(action.spec_.release_),
    };
}

bool ActionRegistry::registerAction(ActionSpec spec) {
    if (spec.id_.empty() || !spec.invoke_) {
        IRE_LOG_ERROR("registerAction: an action needs an id and an invoke callable");
        return false;
    }
    if (spec.context_.empty()) {
        spec.context_ = std::string(kGlobalActionContext);
    }
    if (spec.label_.empty()) {
        spec.label_ = spec.id_;
    }
    spec.defaultChords_ = validChords(spec.defaultChords_, spec.id_);

    std::size_t index = 0;
    const auto it = m_index.find(spec.id_);
    if (it == m_index.end()) {
        index = m_actions.size();
        m_index.emplace(spec.id_, index);
        m_actions.emplace_back();
    } else {
        index = it->second;
        endHold(m_actions[index]);
    }
    Action &action = m_actions[index];
    action.spec_ = std::move(spec);
    action.registered_ = true;
    const auto override = m_overrides.find(action.spec_.id_);
    action.chords_ = override == m_overrides.end()
                         ? action.spec_.defaultChords_
                         : validChords(override->second, action.spec_.id_);
    syncRows(index);
    publish();
    return true;
}

void ActionRegistry::unregisterAction(std::string_view id) {
    Action *action = find(id);
    if (action == nullptr || !action->registered_) {
        return;
    }
    endHold(*action);
    action->registered_ = false;
    action->spec_.invoke_ = nullptr;
    action->spec_.release_ = nullptr;
    action->spec_.available_ = nullptr;
    parkRows(*action);
    publish();
}

bool ActionRegistry::invokeAction(std::string_view id) {
    Action *action = find(id);
    if (action == nullptr || !available(*action)) {
        return false;
    }
    // Copies, so a body that registers more actions (and grows `m_actions`)
    // never runs out of a moved-from slot.
    const std::function<void()> invoke = action->spec_.invoke_;
    const std::function<void()> release = action->spec_.release_;
    invoke();
    if (release) {
        release();
    }
    return true;
}

const ActionRegistry::AdoptedChord *
ActionRegistry::adoptedFor(const Action &action, KeyChord chord) const {
    for (const AdoptedChord &adopted : action.adopted_) {
        if (adopted.chord_ == chord) {
            return &adopted;
        }
    }
    return nullptr;
}

void ActionRegistry::onPress(std::size_t index, std::size_t slot) {
    Action &action = m_actions[index];
    if (!available(action) || slot >= action.chords_.size()) {
        return;
    }
    if (const AdoptedChord *adopted = adoptedFor(action, action.chords_[slot])) {
        m_commands.fireUserCommand(adopted->row_);
        return;
    }
    if (action.spec_.release_) {
        const bool live = !action.heldSlots_.empty();
        if (std::find(action.heldSlots_.begin(), action.heldSlots_.end(), slot) ==
            action.heldSlots_.end()) {
            action.heldSlots_.push_back(slot);
        }
        if (live) {
            return;
        }
    }
    const std::function<void()> invoke = action.spec_.invoke_;
    invoke();
}

std::size_t ActionRegistry::adoptNamedCommands(
    std::string_view idPrefix, const std::function<std::string(std::string_view name)> &sectionFor
) {
    std::vector<CommandRegistration> registrations = m_commands.takeAdoptableRegistrations();
    for (const CommandRegistration &registration : registrations) {
        std::string slug;
        for (const char c : registration.name) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            } else if (!slug.empty() && slug.back() != '_') {
                slug += '_';
            }
        }
        while (!slug.empty() && slug.back() == '_') {
            slug.pop_back();
        }
        const std::string id = std::string(idPrefix) + "." + slug;
        const KeyChord chord{
            static_cast<IRInput::KeyMouseButtons>(registration.button),
            registration.requiredModifiers
        };
        const AdoptedChord adopted{
            chord,
            registration.id,
            m_commands.userCommand(registration.id).getBlockedModifiers()
        };
        m_commands.setUserCommandActive(registration.id, false);

        Action *existing = find(id);
        if (existing != nullptr && existing->registered_ && !existing->adopted_.empty()) {
            if (adoptedFor(*existing, chord) == nullptr) {
                existing->adopted_.push_back(adopted);
                existing->spec_.defaultChords_.push_back(chord);
                const auto override = m_overrides.find(id);
                if (override == m_overrides.end()) {
                    existing->chords_ = existing->spec_.defaultChords_;
                }
                syncRows(m_index.at(id));
            }
            continue;
        }
        ActionSpec spec;
        spec.id_ = id;
        spec.label_ = registration.name;
        spec.description_ = registration.description;
        if (sectionFor) {
            spec.section_ = sectionFor(registration.name);
        }
        if (spec.section_.empty()) {
            for (const char c : idPrefix) {
                spec.section_ += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        spec.defaultChords_ = {chord};
        CommandManager *commands = &m_commands;
        const CommandId row = registration.id;
        spec.invoke_ = [commands, row]() { commands->fireUserCommand(row); };
        registerAction(std::move(spec));
        Action *action = find(id);
        action->adopted_ = {adopted};
        syncRows(m_index.at(id));
    }
    publish();
    return registrations.size();
}

void ActionRegistry::onRelease(std::size_t index, std::size_t slot) {
    Action &action = m_actions[index];
    const auto it = std::find(action.heldSlots_.begin(), action.heldSlots_.end(), slot);
    if (it == action.heldSlots_.end()) {
        return;
    }
    if (action.heldSlots_.size() > 1) {
        action.heldSlots_.erase(it);
        return;
    }
    endHold(action);
}

void ActionRegistry::endHold(Action &action) {
    if (action.heldSlots_.empty()) {
        return;
    }
    action.heldSlots_.clear();
    const std::function<void()> release = action.spec_.release_;
    if (release) {
        release();
    }
}

void ActionRegistry::parkRows(Action &action, std::size_t firstSlot) {
    for (std::size_t slot = firstSlot; slot < action.rows_.size(); ++slot) {
        ChordRows &rows = action.rows_[slot];
        if (rows.active_) {
            m_commands.setUserCommandActive(rows.press_, false);
            if (rows.hasRelease_) {
                m_commands.setUserCommandActive(rows.release_, false);
            }
            rows.active_ = false;
        }
    }
}

void ActionRegistry::syncRows(std::size_t index) {
    Action &action = m_actions[index];
    endHold(action);
    if (!action.registered_) {
        parkRows(action);
        return;
    }
    const bool hold = static_cast<bool>(action.spec_.release_);
    for (std::size_t slot = 0; slot < action.chords_.size(); ++slot) {
        if (slot == action.rows_.size()) {
            ChordRows rows;
            rows.press_ =
                m_commands.createActionRow([this, index, slot]() { onPress(index, slot); }, false);
            m_actions[index].rows_.push_back(rows);
        }
        ChordRows &rows = m_actions[index].rows_[slot];
        if (hold && !rows.hasRelease_) {
            rows.release_ =
                m_commands.createActionRow([this, index, slot]() { onRelease(index, slot); }, true);
            rows.hasRelease_ = true;
        }
        const KeyChord chord = m_actions[index].chords_[slot];
        const AdoptedChord *adopted = adoptedFor(m_actions[index], chord);
        m_commands.rebindUserCommand(
            rows.press_,
            static_cast<int>(chord.key_),
            chord.modifiers_,
            adopted != nullptr ? adopted->blocked_ : blockedFor(chord.modifiers_)
        );
        m_commands.setUserCommandActive(rows.press_, true);
        if (rows.hasRelease_) {
            m_commands.rebindUserCommand(
                rows.release_,
                static_cast<int>(chord.key_),
                IRInput::kModifierNone,
                IRInput::kModifierNone
            );
            m_commands.setUserCommandActive(rows.release_, hold);
        }
        rows.active_ = true;
    }
    Action &synced = m_actions[index];
    parkRows(synced, synced.chords_.size());
}

void ActionRegistry::storeOverride(const Action &action) {
    if (action.chords_ == action.spec_.defaultChords_) {
        m_overrides.erase(action.spec_.id_);
    } else {
        m_overrides[action.spec_.id_] = action.chords_;
    }
}

void ActionRegistry::publish() {
    std::vector<CommandRegistration> registrations;
    for (const Action &action : m_actions) {
        if (!action.registered_) {
            continue;
        }
        for (const KeyChord &chord : action.chords_) {
            registrations.push_back(
                {action.spec_.label_,
                 action.spec_.description_,
                 static_cast<int>(chord.key_),
                 IRInput::PRESSED,
                 chord.modifiers_}
            );
        }
    }
    m_commands.setActionRegistrations(std::move(registrations));
    ++m_generation;
}

void ActionRegistry::notifyChanged() {
    if (m_onBindingsChanged) {
        m_onBindingsChanged();
    }
}

std::vector<ActionConflict> ActionRegistry::findConflicts(
    KeyChord chord, std::string_view context, std::string_view excludeId
) const {
    std::vector<ActionConflict> conflicts;
    for (const Action &action : m_actions) {
        if (!action.registered_ || action.spec_.id_ == excludeId ||
            !contextsOverlap(context, action.spec_.context_)) {
            continue;
        }
        if (std::find(action.chords_.begin(), action.chords_.end(), chord) !=
            action.chords_.end()) {
            conflicts.push_back({action.spec_.id_, action.spec_.label_});
        }
    }
    if (m_commands.plainRowClaimsChord(static_cast<int>(chord.key_), chord.modifiers_)) {
        conflicts.push_back({"", "COMMAND"});
    }
    return conflicts;
}

BindResult ActionRegistry::applyChords(
    std::size_t index, std::vector<KeyChord> chords, ConflictPolicy policy, KeyChord probe
) {
    Action &action = m_actions[index];
    if (!isAssignableChord(probe)) {
        return {BindStatus::INVALID_CHORD, {}};
    }
    std::vector<ActionConflict> conflicts =
        findConflicts(probe, action.spec_.context_, action.spec_.id_);
    if (!conflicts.empty()) {
        const bool plainRow =
            std::any_of(conflicts.begin(), conflicts.end(), [](const ActionConflict &conflict) {
                return conflict.actionId_.empty();
            });
        if (policy == ConflictPolicy::REJECT || plainRow) {
            return {BindStatus::CONFLICT, std::move(conflicts)};
        }
        for (const ActionConflict &conflict : conflicts) {
            const std::size_t other = m_index.at(conflict.actionId_);
            std::erase(m_actions[other].chords_, probe);
            storeOverride(m_actions[other]);
            syncRows(other);
        }
    }
    Action &target = m_actions[index];
    target.chords_ = std::move(chords);
    storeOverride(target);
    syncRows(index);
    publish();
    notifyChanged();
    return {BindStatus::BOUND, std::move(conflicts)};
}

BindResult ActionRegistry::addChord(std::string_view id, KeyChord chord, ConflictPolicy policy) {
    Action *action = find(id);
    if (action == nullptr || !action->registered_) {
        return {BindStatus::UNKNOWN_ACTION, {}};
    }
    if (std::find(action->chords_.begin(), action->chords_.end(), chord) != action->chords_.end()) {
        return {BindStatus::ALREADY_BOUND, {}};
    }
    std::vector<KeyChord> chords = action->chords_;
    chords.push_back(chord);
    return applyChords(m_index.at(std::string(id)), std::move(chords), policy, chord);
}

BindResult ActionRegistry::replaceChord(
    std::string_view id, KeyChord previous, KeyChord chord, ConflictPolicy policy
) {
    Action *action = find(id);
    if (action == nullptr || !action->registered_) {
        return {BindStatus::UNKNOWN_ACTION, {}};
    }
    if (previous == chord) {
        return {BindStatus::ALREADY_BOUND, {}};
    }
    std::vector<KeyChord> chords = action->chords_;
    const auto it = std::find(chords.begin(), chords.end(), previous);
    if (it == chords.end()) {
        return {BindStatus::NOT_BOUND, {}};
    }
    if (std::find(chords.begin(), chords.end(), chord) != chords.end()) {
        chords.erase(it);
    } else {
        *it = chord;
    }
    return applyChords(m_index.at(std::string(id)), std::move(chords), policy, chord);
}

BindResult ActionRegistry::removeChord(std::string_view id, KeyChord chord) {
    Action *action = find(id);
    if (action == nullptr || !action->registered_) {
        return {BindStatus::UNKNOWN_ACTION, {}};
    }
    if (std::erase(action->chords_, chord) == 0) {
        return {BindStatus::NOT_BOUND, {}};
    }
    storeOverride(*action);
    syncRows(m_index.at(std::string(id)));
    publish();
    notifyChanged();
    return {};
}

BindResult ActionRegistry::clearChords(std::string_view id) {
    Action *action = find(id);
    if (action == nullptr || !action->registered_) {
        return {BindStatus::UNKNOWN_ACTION, {}};
    }
    action->chords_.clear();
    storeOverride(*action);
    syncRows(m_index.at(std::string(id)));
    publish();
    notifyChanged();
    return {};
}

BindResult ActionRegistry::restoreDefaults(std::string_view id) {
    Action *action = find(id);
    if (action == nullptr || !action->registered_) {
        return {BindStatus::UNKNOWN_ACTION, {}};
    }
    action->chords_ = action->spec_.defaultChords_;
    m_overrides.erase(action->spec_.id_);
    syncRows(m_index.at(std::string(id)));
    publish();
    notifyChanged();
    return {};
}

void ActionRegistry::restoreAllDefaults() {
    m_overrides.clear();
    for (std::size_t index = 0; index < m_actions.size(); ++index) {
        m_actions[index].chords_ = m_actions[index].spec_.defaultChords_;
        syncRows(index);
    }
    publish();
    notifyChanged();
}

void ActionRegistry::setOverrides(ActionOverrides overrides) {
    m_overrides.clear();
    for (auto &[id, chords] : overrides) {
        m_overrides.emplace(id, validChords(chords, id));
    }
    for (std::size_t index = 0; index < m_actions.size(); ++index) {
        Action &action = m_actions[index];
        if (!action.registered_) {
            continue;
        }
        const auto override = m_overrides.find(action.spec_.id_);
        action.chords_ =
            override == m_overrides.end() ? action.spec_.defaultChords_ : override->second;
        storeOverride(action);
        syncRows(index);
    }
    publish();
}

std::optional<ActionInfo> ActionRegistry::findAction(std::string_view id) const {
    const Action *action = find(id);
    if (action == nullptr || !action->registered_) {
        return std::nullopt;
    }
    return info(*action);
}

std::optional<KeyChord> ActionRegistry::firstChord(std::string_view id) const {
    const Action *action = find(id);
    if (action == nullptr || !action->registered_ || action->chords_.empty()) {
        return std::nullopt;
    }
    return action->chords_.front();
}

std::vector<ActionInfo> ActionRegistry::listActions() const {
    std::vector<ActionInfo> infos;
    infos.reserve(m_actions.size());
    for (const Action &action : m_actions) {
        if (action.registered_) {
            infos.push_back(info(action));
        }
    }
    std::sort(infos.begin(), infos.end(), [](const ActionInfo &a, const ActionInfo &b) {
        return std::tie(a.section_, a.label_, a.id_) < std::tie(b.section_, b.label_, b.id_);
    });
    return infos;
}

std::vector<ActionInfo> ActionRegistry::searchActions(std::string_view query) const {
    std::vector<ActionInfo> all = listActions();
    if (query.empty()) {
        return all;
    }
    const std::string needle = toLower(query);
    const std::optional<KeyChord> asChord = parseChordToken(query);
    std::vector<ActionInfo> matches;
    for (ActionInfo &action : all) {
        bool match = containsLower(action.label_, needle) || containsLower(action.id_, needle) ||
                     containsLower(action.section_, needle) ||
                     containsLower(action.description_, needle);
        for (const KeyChord &chord : action.chords_) {
            if (match) {
                break;
            }
            match = (asChord && *asChord == chord) || containsLower(chordLabel(chord), needle) ||
                    containsLower(chordToken(chord), needle);
        }
        if (match) {
            matches.push_back(std::move(action));
        }
    }
    return matches;
}

} // namespace IRCommand
