#ifndef DOCUMENT_PANEL_H
#define DOCUMENT_PANEL_H

#include "editor_widget_geometry.hpp"
#include "document_lifecycle.hpp"

#include <irreden/render/trixel_text.hpp>
#include <irreden/render/widgets.hpp>

#include <array>
#include <string>
#include <vector>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kDocumentPanelPos{4, 2};
constexpr IRMath::ivec2 kDocumentPanelSize{1176, 26};
constexpr IRMath::ivec2 kDocumentModalPos{218, 180};
constexpr IRMath::ivec2 kDocumentModalSize{626, 64};

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
        m_widgets.push_back(makeLabel({10, 12}, "NAME", {}, IRRender::TextSize::SMALL));
        m_name = makeTextInput({60, 6}, {180, 18}, initial.baseName_, 96);
        m_widgets.push_back(m_name);
        const std::array<std::string, 4> labels{"SAVE", "SAVE AS", "OPEN", "NEW"};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            m_commands[index] = makeButton(
                {248 + static_cast<int>(index) * 80, 6},
                {76, 18},
                labels[index],
                IRRender::TextSize::SMALL
            );
            m_widgets.push_back(m_commands[index]);
        }
        m_widgets.push_back(makeLabel({574, 12}, "FOLDER", {}, IRRender::TextSize::SMALL));
        m_directory = makeTextInput({630, 6}, {540, 18}, initial.directory_, 1024);
        m_widgets.push_back(m_directory);
        m_status = makeLabel({474, 88}, "", {}, IRRender::TextSize::SMALL);
        m_widgets.push_back(m_status);
        m_modal = makePanel(kDocumentModalPos, kDocumentModalSize, "DOCUMENT");
        m_widgets.push_back(m_modal);
        setVisible(m_modal, false);
        m_prompt = makeLabel({224, 202}, "", {}, IRRender::TextSize::SMALL);
        m_widgets.push_back(m_prompt);
        const std::array<std::string, 4>
            decisions{"SAVE CHANGES", "DISCARD", "CANCEL", "OVERWRITE"};
        for (std::size_t index = 0; index < decisions.size(); ++index) {
            m_decisions[index] = makeButton(
                {224 + static_cast<int>(index) * 124, 220},
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

    void update(const DocumentLifecycle &document, bool overwrite = false) {
        using namespace IRPrefab::Widget;
        const bool pending = document.pendingAction() != DocumentAction::NONE;
        setVisible(m_modal, pending || overwrite);
        setVisible(m_prompt, pending || overwrite);
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
        const std::string &message = document.error();
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

    const auto &commandButtons() const {
        return m_commands;
    }

    const std::vector<IREntity::EntityId> &widgets() const {
        return m_widgets;
    }

    bool containsCursor(IRMath::vec2 cursor) const {
        return !m_widgets.empty() && (widgetContainsCursor(m_widgets.front(), cursor) ||
                                      widgetContainsCursor(m_modal, cursor));
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
    IREntity::EntityId m_modal = IREntity::kNullEntity;
    IREntity::EntityId m_prompt = IREntity::kNullEntity;
};

} // namespace IRVoxelEditor

#endif
