#ifndef EDITOR_ACTIONS_GUARD_H
#define EDITOR_ACTIONS_GUARD_H

#include <irreden/render/actions_window.hpp>
#include <irreden/render/widgets.hpp>

namespace IRVoxelEditor {
class EditorActionsGuard {
  public:
    bool prepare() {
        const bool open = IRPrefab::ActionsWindow::isOpen();
        const bool opening = open && !m_open;
        if (opening) {
            IREntity::forEachComponent<IRComponents::C_Widget>([&](IREntity::EntityId id,
                                                                   IRComponents::C_Widget &widget) {
                if (widget.visible_)
                    m_hidden.push_back(id);
            });
        }
        for (const auto id : m_hidden)
            if (IREntity::entityExists(id))
                IRPrefab::Widget::setVisible(id, !open);
        if (!open)
            m_hidden.clear();
        m_open = open;
        return opening;
    }
    void capture() {
        if (!IRPrefab::ActionsWindow::isOpen())
            return;
        const bool textCaptured = IRCommand::getCommandManager().isKeyboardCaptured();
        IRCommand::getCommandManager().setKeyboardCaptured(true);
        const auto *window = IRPrefab::ActionsWindow::systemOrNull();
        if (!window || !window->built_ ||
            window->mode_ != IRSystem::System<IRSystem::ACTIONS_WINDOW>::Mode::BROWSE)
            return;
        if (IRPrefab::Widget::wasClicked(window->button(IRPrefab::ActionsWindow::Button::RUN)) &&
            !window->selectedId_.empty()) {
            const std::string actionId = window->selectedId_;
            IRPrefab::ActionsWindow::setOpen(false);
            prepare();
            m_invoking = true;
            if (actionId != IRPrefab::ActionsWindow::kToggleActionId)
                IRCommand::invokeAction(actionId);
            m_invoking = false;
            return;
        }
        bool close = IRInput::checkKeyMouseButton(IRInput::kKeyButtonEscape, IRInput::PRESSED);
        if (!textCaptured)
            if (const auto toggle =
                    IRCommand::actions().findAction(IRPrefab::ActionsWindow::kToggleActionId))
                for (const auto &chord : toggle->chords_)
                    close = close || (IRInput::heldKeyModifiers() == chord.modifiers_ &&
                                      IRInput::checkKeyMouseButton(chord.key_, IRInput::PRESSED));
        if (close)
            IRPrefab::ActionsWindow::setOpen(false);
    }

    bool invoking() const {
        return m_invoking;
    }

  private:
    std::vector<IREntity::EntityId> m_hidden;
    bool m_open = false;
    bool m_invoking = false;
};
} // namespace IRVoxelEditor
#endif
