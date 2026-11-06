#ifndef EDITOR_HELP_GUARD_H
#define EDITOR_HELP_GUARD_H

#include <irreden/render/help_overlay.hpp>
#include <irreden/render/widgets.hpp>

namespace IRVoxelEditor {
class EditorHelpGuard {
  public:
    void create() {
        m_cover = IRPrefab::Widget::makeHitRegion(
            {0, 0},
            {1, 1},
            IRComponents::kWidgetDropdownOpenZBias - 1
        );
        IRPrefab::Widget::setCoversBelow(m_cover, true);
        IRPrefab::Widget::setVisible(m_cover, false);
    }
    bool prepare() {
        const bool opening = IRPrefab::HelpOverlay::isVisible() && !m_blocked;
        m_blocked = IRPrefab::HelpOverlay::isVisible();
        if (!m_blocked) {
            for (const auto id : m_hidden)
                if (IREntity::entityExists(id))
                    IRPrefab::Widget::setVisible(id, true);
            m_hidden.clear();
            IRPrefab::Widget::setVisible(m_cover, false);
            return false;
        }
        IREntity::forEachComponent<IRComponents::C_Widget>([&](IREntity::EntityId id,
                                                               IRComponents::C_Widget &widget) {
            if (id != m_cover && widget.visible_) {
                m_hidden.push_back(id);
                IRPrefab::Widget::setVisible(id, false);
            }
        });
        const auto size = IRPrefab::Layout::guiCanvasSize();
        IREntity::getComponent<IRComponents::C_Widget>(m_cover).size_ = size;
        IREntity::getComponent<IRComponents::C_HitBox2DGui>(m_cover).size_ = size;
        IRPrefab::Widget::setVisible(m_cover, true);
        return opening;
    }
    void capture() {
        if (!m_blocked)
            return;
        IRCommand::getCommandManager().setKeyboardCaptured(true);
        for (const auto key :
             {IRInput::kKeyButtonF1, IRInput::kKeyButtonGraveAccent, IRInput::kKeyButtonEscape})
            if (IRInput::checkKeyMouseButton(key, IRInput::PRESSED))
                IRPrefab::HelpOverlay::setVisible(false);
    }
    bool blocked() const {
        return m_blocked || IRPrefab::HelpOverlay::isVisible();
    }

  private:
    IREntity::EntityId m_cover = IREntity::kNullEntity;
    std::vector<IREntity::EntityId> m_hidden;
    bool m_blocked = false;
};
} // namespace IRVoxelEditor
#endif
