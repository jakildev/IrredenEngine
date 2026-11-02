#ifndef IR_VOXEL_EDITOR_HIERARCHY_PANEL_H
#define IR_VOXEL_EDITOR_HIERARCHY_PANEL_H

#include "entity_scene.hpp"

#include <irreden/render/trixel_text.hpp>
#include <irreden/render/widgets.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kHierarchyPanelPos{254, 362};
constexpr IRMath::ivec2 kHierarchyPanelSize{120, 82};
constexpr IRMath::ivec2 kHierarchyParentPos{kHierarchyPanelPos.x + 4, kHierarchyPanelPos.y + 30};
constexpr IRMath::ivec2 kHierarchyParentSize{112, 16};
constexpr IRMath::ivec2 kHierarchyAttachPos{kHierarchyPanelPos.x + 4, kHierarchyPanelPos.y + 49};
constexpr IRMath::ivec2 kHierarchyAttachSize{112, 14};
constexpr int kHierarchyParentItemHeight = 12;

struct ReparentEdit {
    std::string partId_;
    std::string beforeParent_;
    IRComponents::C_LocalTransform beforeLocal_;
    std::string afterParent_;
    IRComponents::C_LocalTransform afterLocal_;
};

inline EntitySceneResult
applyReparentEdit(EntityScene &scene, const ReparentEdit &edit, bool redo) {
    const auto result =
        scene.reparentPart(edit.partId_, redo ? edit.afterParent_ : edit.beforeParent_, false);
    if (!result.ok_)
        return result;
    IREntity::setComponent(
        scene.findPart(edit.partId_)->entity_,
        redo ? edit.afterLocal_ : edit.beforeLocal_
    );
    return {true, {}};
}

class HierarchyPanel {
  public:
    void create() {
        m_panel = IRPrefab::Widget::makePanel(kHierarchyPanelPos, kHierarchyPanelSize, "PARENT");
        m_partLabel = IRPrefab::Widget::makeLabel(
            kHierarchyPanelPos + IRMath::ivec2(4, 19),
            "SELECT A PART",
            {},
            IRRender::TextSize::SMALL
        );
        m_parentDropdown = IRPrefab::Widget::makeDropdown(
            kHierarchyParentPos,
            kHierarchyParentSize,
            {"ROOT"},
            0,
            kHierarchyParentItemHeight,
            IRRender::TextSize::SMALL
        );
        m_attachButton = IRPrefab::Widget::makeButton(
            kHierarchyAttachPos,
            kHierarchyAttachSize,
            "ATTACH (KEEP POSE)",
            IRRender::TextSize::SMALL
        );
        m_statusLabel = IRPrefab::Widget::makeLabel(
            kHierarchyPanelPos + IRMath::ivec2(4, 68),
            "SELECT A PART",
            {},
            IRRender::TextSize::SMALL
        );
        IRPrefab::Widget::setDisabled(m_parentDropdown, true);
        IRPrefab::Widget::setDisabled(m_attachButton, true);
    }

    // Poll once after widget dispatch. The returned value owns its history
    // payload; the panel retains neither scene references nor replay callbacks.
    std::optional<ReparentEdit> update(EntityScene &scene) {
        if (m_panel == IREntity::kNullEntity)
            return std::nullopt;
        const EditorPart *selected = scene.selectedPart();
        const std::string selectedId = selected ? selected->id_ : std::string{};
        const std::string parentId = selected ? selected->parentId_ : std::string{};
        const bool selectionChanged = selectedId != m_selectedId;
        const bool parentChanged = parentId != m_observedParent;
        if (selectionChanged || parentChanged || !candidatesMatch(scene, selectedId)) {
            rebuildCandidates(scene, selectedId, parentId);
            m_selectedId = selectedId;
            m_observedParent = parentId;
            IRPrefab::Widget::setLabelText(
                m_partLabel,
                clipped(selected ? "PART: " + selectedId : "SELECT A PART", 109)
            );
            setStatus(selected ? "KEEP WORLD POSITION" : "SELECT A PART");
        }
        IRPrefab::Widget::setDisabled(m_parentDropdown, selected == nullptr);
        const int row = IRPrefab::Widget::dropdownSelectedIndex(m_parentDropdown);
        const bool validRow = row >= 0 && row < static_cast<int>(m_parentIds.size());
        const bool canAttach =
            selected && validRow && m_parentIds[static_cast<std::size_t>(row)] != parentId;
        IRPrefab::Widget::setDisabled(m_attachButton, !canAttach);
        if (!canAttach || selectionChanged || !IRPrefab::Widget::wasClicked(m_attachButton))
            return std::nullopt;

        ReparentEdit edit;
        edit.partId_ = selectedId;
        edit.beforeParent_ = parentId;
        edit.beforeLocal_ =
            IREntity::getComponent<IRComponents::C_LocalTransform>(selected->entity_);
        edit.afterParent_ = m_parentIds[static_cast<std::size_t>(row)];
        const EntitySceneResult result = scene.reparentPart(selectedId, edit.afterParent_);
        if (!result.ok_) {
            setStatus(
                parentWouldCycle(scene, selectedId, edit.afterParent_) ? "INVALID: PARENT CYCLE"
                                                                       : "FAILED: " + result.error_
            );
            return std::nullopt;
        }
        edit.afterLocal_ =
            IREntity::getComponent<IRComponents::C_LocalTransform>(selected->entity_);
        m_observedParent = edit.afterParent_;
        setStatus(edit.afterParent_.empty() ? "DETACHED TO ROOT" : "ATTACHED; POSE KEPT");
        IRPrefab::Widget::setDisabled(m_attachButton, true);
        return edit;
    }

    bool containsCursor(IRMath::vec2 guiCursor) const {
        if (m_panel == IREntity::kNullEntity || !IRPrefab::Widget::isVisible(m_panel))
            return false;
        if (contains(guiCursor, kHierarchyPanelPos, kHierarchyPanelSize))
            return true;
        if (!IRPrefab::Widget::isDropdownOpen(m_parentDropdown))
            return false;
        const auto layout = IRPrefab::Widget::dropdownStripLayout(m_parentDropdown);
        return contains(
            guiCursor,
            kHierarchyParentPos + IRMath::ivec2(0, layout.offsetY_),
            IRMath::ivec2(kHierarchyParentSize.x, layout.rows_ * kHierarchyParentItemHeight)
        );
    }

    IREntity::EntityId panel() const {
        return m_panel;
    }

    IREntity::EntityId parentDropdown() const {
        return m_parentDropdown;
    }

    IREntity::EntityId attachButton() const {
        return m_attachButton;
    }

    const std::string &statusText() const {
        return m_status;
    }

    void showError(std::string message) {
        setStatus("FAILED: " + std::move(message));
    }

    int parentRow(const std::string &id) const {
        for (std::size_t i = 0; i < m_parentIds.size(); ++i) {
            if (m_parentIds[i] == id)
                return static_cast<int>(i);
        }
        return -1;
    }

  private:
    static std::string clipped(std::string text, int width) {
        if (IRRender::measureText(text, 0, IRRender::TextSize::SMALL).x <= width)
            return text;
        while (!text.empty() &&
               IRRender::measureText(text + "...", 0, IRRender::TextSize::SMALL).x > width)
            text.pop_back();
        return text + "...";
    }

    static bool contains(IRMath::vec2 point, IRMath::ivec2 pos, IRMath::ivec2 size) {
        return point.x >= pos.x && point.y >= pos.y && point.x < pos.x + size.x &&
               point.y < pos.y + size.y;
    }

    static bool parentWouldCycle(
        const EntityScene &scene, const std::string &partId, const std::string &parentId
    ) {
        for (const EditorPart *parent = scene.findPart(parentId); parent;
             parent = parent->parentId_.empty() ? nullptr : scene.findPart(parent->parentId_)) {
            if (parent->id_ == partId)
                return true;
        }
        return false;
    }

    bool candidatesMatch(const EntityScene &scene, const std::string &selectedId) const {
        std::size_t index = 1;
        if (m_parentIds.empty())
            return false;
        for (const EditorPart &part : scene.parts()) {
            if (part.id_ == selectedId)
                continue;
            if (index >= m_parentIds.size() || m_parentIds[index] != part.id_)
                return false;
            ++index;
        }
        return index == m_parentIds.size();
    }

    void rebuildCandidates(
        const EntityScene &scene, const std::string &selectedId, const std::string &parentId
    ) {
        m_parentIds.clear();
        m_parentIds.emplace_back();
        std::vector<std::string> labels{"ROOT (DETACH)"};
        std::size_t ordinal = 0;
        for (const EditorPart &part : scene.parts()) {
            ++ordinal;
            if (part.id_ == selectedId)
                continue;
            m_parentIds.push_back(part.id_);
            labels.push_back(clipped(std::to_string(ordinal) + ": " + part.id_, 94));
        }
        auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(m_parentDropdown);
        dropdown.isOpen_ = false;
        dropdown.scrollOffset_ = 0;
        dropdown.typeahead_.clear();
        IRPrefab::Widget::setDropdownItems(
            m_parentDropdown,
            std::move(labels),
            parentRow(parentId)
        );
    }

    void setStatus(std::string text) {
        m_status = std::move(text);
        IRPrefab::Widget::setLabelText(m_statusLabel, clipped(m_status, 109));
    }

    IREntity::EntityId m_panel = IREntity::kNullEntity;
    IREntity::EntityId m_partLabel = IREntity::kNullEntity;
    IREntity::EntityId m_parentDropdown = IREntity::kNullEntity;
    IREntity::EntityId m_attachButton = IREntity::kNullEntity;
    IREntity::EntityId m_statusLabel = IREntity::kNullEntity;
    std::vector<std::string> m_parentIds;
    std::string m_selectedId;
    std::string m_observedParent;
    std::string m_status;
};

} // namespace IRVoxelEditor

#endif
