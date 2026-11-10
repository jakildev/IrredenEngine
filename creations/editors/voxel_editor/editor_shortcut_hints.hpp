#ifndef EDITOR_SHORTCUT_HINTS_H
#define EDITOR_SHORTCUT_HINTS_H

#include <irreden/ir_command.hpp>
#include <irreden/render/widget_actions.hpp>
#include <irreden/ir_platform.hpp>
#include <irreden/render/help_overlay.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/render/widget_keycap.hpp>

namespace IRVoxelEditor {

class EditorShortcutHints {
  public:
    void add(IREntity::EntityId id, std::string_view action) {
        for (const auto &entry : m_entries)
            if (entry.widget_ == id)
                return;
        m_entries.push_back({id, action, {}, {}});
        m_resolved = false;
    }

    int lastBadgeCount() const {
        return m_lastCount;
    }

    std::optional<IRPrefab::GuiRect> lastBadge(IREntity::EntityId id) const {
        for (const auto &entry : m_entries) {
            if (entry.widget_ == id && !IRPrefab::isEmpty(entry.last_))
                return entry.last_;
        }
        return std::nullopt;
    }

    void draw(IRComponents::C_TriangleCanvasTextures &canvas, bool blocked) {
        m_lastCount = 0;
        const auto *input = IREntity::singletonOrNull<IRComponents::C_WidgetInputFrame>();
        const bool show = input && input->hintsVisible_ && !blocked &&
                          !IRCommand::getCommandManager().isKeyboardCaptured();
        if (show && (!m_resolved ||
                     m_generation != IRCommand::getCommandManager().getRegistrationGeneration()))
            resolve();
        const auto &theme = IRPrefab::Widget::defaultTheme();
        for (auto &entry : m_entries) {
            entry.last_ = {};
            if (!show)
                continue;
            if (const auto hotkey =
                    IREntity::getComponentOptional<IRComponents::C_WidgetHotkey>(entry.widget_))
                entry.shortcut_ = (*hotkey)->hint_;
            if (entry.shortcut_.empty())
                continue;
            const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(entry.widget_);
            if (!widget.visible_ || widget.disabled_)
                continue;
            const auto pos =
                IREntity::getComponent<IRComponents::C_GuiPosition>(entry.widget_).pos_;
            const auto size = IRPrefab::Widget::keycapSize(entry.shortcut_);
            const int y = pos.y >= size.y ? pos.y - size.y : pos.y + widget.size_.y;
            const auto corner = IRMath::clamp(
                IRMath::ivec2(pos.x + widget.size_.x - size.x, y),
                IRMath::ivec2(0),
                IRMath::max(IRMath::ivec2(0), canvas.size_ - size)
            );
            IRPrefab::Widget::drawKeycap(
                canvas,
                m_scratch,
                m_glyphs,
                corner,
                entry.shortcut_,
                theme.hintBackground_,
                theme.hintText_
            );
            entry.last_ = {corner, size};
            ++m_lastCount;
        }
        if (!m_glyphs.empty())
            IRPrefab::GuiText::dispatchGuiText(m_glyphs);
    }

  private:
    struct Entry {
        IREntity::EntityId widget_;
        std::string_view action_;
        std::string shortcut_;
        IRPrefab::GuiRect last_;
    };
    void resolve() {
        const auto &manager = IRCommand::getCommandManager();
        m_generation = manager.getRegistrationGeneration();
        for (auto &entry : m_entries) {
            entry.shortcut_.clear();
            const auto width =
                IREntity::getComponent<IRComponents::C_Widget>(entry.widget_).size_.x;
            int best = -1;
            for (const auto &row : manager.getCommandRegistrations()) {
                if (row.name != entry.action_)
                    continue;
                auto text = IRCommand::modifierString(row.requiredModifiers) +
                            IRCommand::keyButtonToString(row.button);
                const int score =
                    (IRPrefab::Widget::keycapSize(text).x <= width ? 4 : 0) +
                    (IRPlatform::kIsMacOS && (row.requiredModifiers & IRInput::kModifierSuper) ? 2
                                                                                               : 0);
                if (score > best || (score == best && text.size() < entry.shortcut_.size())) {
                    entry.shortcut_ = std::move(text);
                    best = score;
                }
            }
        }
        m_resolved = true;
    }
    std::vector<Entry> m_entries;
    IRRender::RectFillScratch m_scratch;
    std::vector<IRRender::GlyphDrawCommand> m_glyphs;
    std::uint32_t m_generation = 0;
    int m_lastCount = 0;
    bool m_resolved = false;
};
} // namespace IRVoxelEditor
#endif
