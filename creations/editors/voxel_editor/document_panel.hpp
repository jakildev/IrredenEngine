#ifndef DOCUMENT_PANEL_H
#define DOCUMENT_PANEL_H

#include "document_lifecycle.hpp"

#include <irreden/render/trixel_text.hpp>
#include <irreden/render/widgets.hpp>

#include <array>
#include <string>
#include <vector>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kDocumentPanelPos{4, 106};
constexpr IRMath::ivec2 kDocumentPanelSize{626, 112};

enum class DocumentPanelAction {
    NONE,
    SAVE,
    SAVE_AS,
    OPEN,
    NEW_DOCUMENT,
    SAVE_CHANGES,
    DISCARD,
    CANCEL,
    CONFIRM_OVERWRITE
};

class DocumentPanel {
  public:
    void create(const DocumentLocation &initial) {
        using namespace IRPrefab::Widget;
        m_widgets.push_back(makePanel(kDocumentPanelPos, kDocumentPanelSize, ""));
        m_widgets.push_back(makeLabel({10, 114}, "NAME", {}, IRRender::TextSize::SMALL));
        m_name = makeTextInput({76, 110}, {180, 18}, initial.baseName_, 96);
        m_widgets.push_back(m_name);
        const std::array<std::string, 4> labels{"SAVE", "SAVE AS", "OPEN", "NEW"};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            m_commands[index] = makeButton(
                {264 + static_cast<int>(index) * 88, 110},
                {84, 18},
                labels[index],
                IRRender::TextSize::SMALL
            );
            m_widgets.push_back(m_commands[index]);
        }
        m_widgets.push_back(makeLabel({10, 138}, "DIRECTORY", {}, IRRender::TextSize::SMALL));
        m_directory = makeTextInput({76, 134}, {540, 18}, initial.directory_, 1024);
        m_widgets.push_back(m_directory);
        m_status = makeLabel({10, 158}, "", {}, IRRender::TextSize::SMALL);
        m_widgets.push_back(m_status);
        m_prompt = makeLabel({10, 174}, "", {}, IRRender::TextSize::SMALL);
        m_widgets.push_back(m_prompt);
        const std::array<std::string, 4>
            decisions{"SAVE CHANGES", "DISCARD", "CANCEL", "OVERWRITE"};
        for (std::size_t index = 0; index < decisions.size(); ++index) {
            m_decisions[index] = makeButton(
                {10 + static_cast<int>(index) * 124, 190},
                {118, 18},
                decisions[index],
                IRRender::TextSize::SMALL
            );
            m_widgets.push_back(m_decisions[index]);
            setVisible(m_decisions[index], false);
        }
    }

    DocumentLocation target() const {
        return {
            IRPrefab::Widget::textInputValue(m_directory),
            IRPrefab::Widget::textInputValue(m_name)
        };
    }

    void setTarget(const DocumentLocation &location) {
        IRPrefab::Widget::setTextInputValue(m_directory, location.directory_);
        IRPrefab::Widget::setTextInputValue(m_name, location.baseName_);
    }

    void
    update(const DocumentLifecycle &document, bool overwrite = false, std::string status = {}) {
        using namespace IRPrefab::Widget;
        const bool pending = document.pendingAction() != DocumentAction::NONE;
        for (const auto command : m_commands)
            setDisabled(command, pending || overwrite);
        const bool lockTarget = overwrite || (pending && document.location().valid());
        setDisabled(m_name, lockTarget);
        setDisabled(m_directory, lockTarget);
        setVisible(m_decisions[0], pending && !overwrite);
        setVisible(m_decisions[1], pending && !overwrite);
        setVisible(m_decisions[2], pending || overwrite);
        setVisible(m_decisions[3], overwrite);
        setLabelText(
            m_prompt,
            overwrite ? "REPLACE THE EXISTING DOCUMENT AT THIS TARGET?"
            : pending ? "UNSAVED CHANGES: SAVE BEFORE CONTINUING?"
                      : ""
        );
        const auto &location = document.location();
        const std::string identity = location.baseName_.empty()
                                         ? "UNTITLED"
                                         : location.directory_ + "/" + location.baseName_;
        const std::string message =
            !document.error().empty() ? document.error() : std::move(status);
        const std::string state = document.dirty()             ? "MODIFIED: "
                                  : location.baseName_.empty() ? ""
                                                               : "SAVED: ";
        setLabelText(
            m_status,
            clipped((message.empty() ? "" : message + " | ") + state + identity)
        );
    }

    DocumentPanelAction poll() const {
        constexpr std::array commands{
            DocumentPanelAction::SAVE,
            DocumentPanelAction::SAVE_AS,
            DocumentPanelAction::OPEN,
            DocumentPanelAction::NEW_DOCUMENT
        };
        constexpr std::array decisions{
            DocumentPanelAction::SAVE_CHANGES,
            DocumentPanelAction::DISCARD,
            DocumentPanelAction::CANCEL,
            DocumentPanelAction::CONFIRM_OVERWRITE
        };
        for (std::size_t index = 0; index < commands.size(); ++index) {
            if (IRPrefab::Widget::wasClicked(m_commands[index]))
                return commands[index];
            if (IRPrefab::Widget::wasClicked(m_decisions[index]))
                return decisions[index];
        }
        return DocumentPanelAction::NONE;
    }

    const std::vector<IREntity::EntityId> &widgets() const {
        return m_widgets;
    }

    bool containsCursor(IRMath::vec2 cursor) const {
        return !m_widgets.empty() && IRPrefab::Widget::isVisible(m_widgets.front()) &&
               cursor.x >= kDocumentPanelPos.x && cursor.y >= kDocumentPanelPos.y &&
               cursor.x < kDocumentPanelPos.x + kDocumentPanelSize.x &&
               cursor.y < kDocumentPanelPos.y + kDocumentPanelSize.y;
    }

  private:
    static std::string clipped(std::string text) {
        if (IRRender::measureText(text, 0, IRRender::TextSize::SMALL).x <= 606)
            return text;
        while (!text.empty() &&
               IRRender::measureText(text + "...", 0, IRRender::TextSize::SMALL).x > 606)
            text.pop_back();
        return text + "...";
    }

    std::vector<IREntity::EntityId> m_widgets;
    std::array<IREntity::EntityId, 4> m_commands{};
    std::array<IREntity::EntityId, 4> m_decisions{};
    IREntity::EntityId m_name = IREntity::kNullEntity;
    IREntity::EntityId m_directory = IREntity::kNullEntity;
    IREntity::EntityId m_status = IREntity::kNullEntity;
    IREntity::EntityId m_prompt = IREntity::kNullEntity;
};

} // namespace IRVoxelEditor

#endif
