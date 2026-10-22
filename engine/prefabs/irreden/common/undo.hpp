#ifndef IR_UNDO_H
#define IR_UNDO_H

#include <irreden/common/components/component_undo_history.hpp>

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_profile.hpp>

#include <utility>

namespace IRPrefab::Undo {

using History = IRComponents::C_UndoHistory;
using ContextId = History::ContextId;
using Record = History::Record;
using Labels = History::Labels;

inline History &materialize() {
    return IREntity::singleton<History>();
}

inline History &history() {
    History *value = IREntity::singletonOrNull<History>();
    IR_ASSERT(value != nullptr, "IRPrefab::Undo must be registered before use");
    return *value;
}

inline bool push(Record record) {
    return history().push(std::move(record), IRTime::Clock::now());
}

inline bool undo() {
    return history().undo();
}

inline bool redo() {
    return history().redo();
}

inline bool canUndo() {
    return history().canUndo();
}

inline bool canRedo() {
    return history().canRedo();
}

inline Labels labels() {
    return history().labels();
}

inline void clear() {
    history().clear();
}

inline ContextId createContext() {
    return history().createContext();
}

inline bool selectContext(ContextId id) {
    return history().selectContext(id);
}

inline bool eraseContext(ContextId id) {
    return history().eraseContext(id);
}

inline void registerUndoCommands() {
    materialize();
    IRCommand::createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        []() { undo(); },
        IRInput::kModifierControl,
        IRInput::kModifierShift,
        "UNDO",
        "UNDO LAST EDIT"
    );
    IRCommand::createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonY,
        []() { redo(); },
        IRInput::kModifierControl,
        IRInput::kModifierShift,
        "REDO",
        "REDO LAST EDIT"
    );
    IRCommand::createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        []() { redo(); },
        IRInput::kModifierControl | IRInput::kModifierShift,
        IRInput::kModifierNone,
        "REDO",
        "REDO LAST EDIT"
    );
}

} // namespace IRPrefab::Undo

#endif /* IR_UNDO_H */
