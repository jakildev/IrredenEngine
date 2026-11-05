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
            IREntity::forEachComponent<IRComponents::C_Widget>([&](IREntity::EntityId id, IRComponents::C_Widget &widget) {
                if (widget.visible_) m_hidden.push_back(id);
            });
        }
        for (const auto id : m_hidden)
            if (IREntity::entityExists(id)) IRPrefab::Widget::setVisible(id, !open);
        if (!open) m_hidden.clear();
        m_open = open;
        return opening;
    }
    void capture() {
        if (!IRPrefab::ActionsWindow::isOpen()) return;
        IRCommand::getCommandManager().setKeyboardCaptured(true);
    }
  private:
    std::vector<IREntity::EntityId> m_hidden;
    bool m_open = false;
};
} // namespace IRVoxelEditor
#endif
