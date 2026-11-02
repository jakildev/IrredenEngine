#include "../../creations/editors/voxel_editor/document_lifecycle.hpp"
#include "../../creations/editors/voxel_editor/document_panel.hpp"

#include <gtest/gtest.h>
#include <irreden/entity/entity_manager.hpp>

namespace {
using namespace IRVoxelEditor;

TEST(VoxelEditorDocumentLifecycle, CancelAndFailedOpenPreserveModifiedCurrentDocument) {
    DocumentLifecycle document;
    const DocumentLocation original{"data/original", "tree"};
    const DocumentLocation target{"data/another", "flower"};
    document.opened(original);
    document.markEdited();
    EXPECT_FALSE(document.request(DocumentAction::OPEN, target));
    EXPECT_EQ(document.pendingLocation(), target);
    document.cancelPending();
    EXPECT_EQ(document.pendingAction(), DocumentAction::NONE);
    EXPECT_EQ(document.location(), original);
    EXPECT_TRUE(document.dirty());
    EXPECT_FALSE(document.request(DocumentAction::OPEN, target));
    document.discardPendingChanges();
    EXPECT_TRUE(document.pendingReady());
    EXPECT_TRUE(document.dirty());
    document.failed("bad frame");
    EXPECT_EQ(document.location(), original);
    EXPECT_TRUE(document.dirty());
    EXPECT_FALSE(document.pendingReady());
    EXPECT_EQ(document.pendingAction(), DocumentAction::OPEN);
    EXPECT_EQ(document.pendingLocation(), target);
    EXPECT_EQ(document.error(), "bad frame");
    document.discardPendingChanges();
    document.opened(target);
    EXPECT_EQ(document.location(), target);
    EXPECT_FALSE(document.dirty());
    EXPECT_EQ(document.pendingAction(), DocumentAction::NONE);
}

TEST(VoxelEditorDocumentLifecycle, SaveAsCheckpointsOnlySuccessAndRetainsPendingClose) {
    DocumentLifecycle document;
    const DocumentLocation original{"data", "original"};
    const DocumentLocation renamed{"other", "named"};
    document.opened(original);
    document.markEdited();
    EXPECT_FALSE(document.request(DocumentAction::CLOSE));
    document.failed("disk full");
    EXPECT_EQ(document.location(), original);
    EXPECT_TRUE(document.dirty());
    EXPECT_FALSE(document.pendingReady());
    document.saved(renamed);
    EXPECT_EQ(document.location(), renamed);
    EXPECT_FALSE(document.dirty());
    EXPECT_TRUE(document.pendingReady());
    EXPECT_EQ(document.pendingAction(), DocumentAction::CLOSE);
    EXPECT_TRUE(document.error().empty());
}

TEST(VoxelEditorDocumentLifecycle, EditingAfterDiscardRequiresAnotherDecision) {
    DocumentLifecycle document;
    document.markEdited();
    EXPECT_FALSE(document.request(DocumentAction::NEW_DOCUMENT));
    document.discardPendingChanges();
    EXPECT_TRUE(document.pendingReady());
    document.markEdited();
    EXPECT_FALSE(document.pendingReady());
    document.created();
    EXPECT_FALSE(document.dirty());
    EXPECT_TRUE(document.location().baseName_.empty());
}

TEST(VoxelEditorDocumentLifecycle, CleanReloadCanProceedAndUndoStillMarksModified) {
    DocumentLifecycle document;
    document.opened({"data", "named"});
    EXPECT_TRUE(document.request(DocumentAction::RELOAD, document.location()));
    document.cancelPending();
    document.markEdited();
    document.saved(document.location());
    document.markEdited();
    EXPECT_TRUE(document.dirty());
    EXPECT_FALSE(document.request(DocumentAction::RELOAD, document.location()));
}

TEST(VoxelEditorDocumentLifecycle, NamedTargetsCannotEscapeTheirDirectory) {
    for (const std::string name :
         {"", ".", "..", "../other", "..\\other", "C:other", "name.", "name "}) {
        EXPECT_FALSE((DocumentLocation{"data", name}.valid())) << name;
    }
    EXPECT_FALSE((DocumentLocation{"", "tree"}.valid()));
    EXPECT_TRUE((DocumentLocation{"data/assets", "Tree 02"}.valid()));
}

TEST(VoxelEditorDocumentPanel, ConfirmationControlsAndTargetsSurviveModalDecisions) {
    IREntity::EntityManager entities;
    DocumentPanel panel;
    const DocumentLocation original{"data", "original"};
    const DocumentLocation target{"other", "new name"};
    panel.create(original);
    DocumentLifecycle document;
    document.opened(original);
    panel.update(document);
    EXPECT_EQ(panel.target(), original);
    panel.setTarget(target);
    document.markEdited();
    document.request(DocumentAction::OPEN, target);
    panel.update(document);
    EXPECT_EQ(panel.target(), target);
    int decisions = 0;
    for (const auto widget : panel.widgets()) {
        const auto button = IREntity::getComponentOptional<IRComponents::C_WidgetButton>(widget);
        if (!button)
            continue;
        const auto &base = IREntity::getComponent<IRComponents::C_Widget>(widget);
        if ((*button)->label_ == "SAVE CHANGES" || (*button)->label_ == "DISCARD" ||
            (*button)->label_ == "CANCEL") {
            EXPECT_TRUE(base.visible_);
            ++decisions;
        } else if ((*button)->label_ == "OVERWRITE") {
            EXPECT_FALSE(base.visible_);
        } else {
            EXPECT_TRUE(base.disabled_);
        }
    }
    EXPECT_EQ(decisions, 3);
    panel.update(document, true);
    for (const auto widget : panel.widgets()) {
        const auto button = IREntity::getComponentOptional<IRComponents::C_WidgetButton>(widget);
        if (button && (*button)->label_ == "OVERWRITE") {
            EXPECT_TRUE(IRPrefab::Widget::isVisible(widget));
            IREntity::getComponent<IRComponents::C_WidgetState>(widget).fireAction_ = true;
        }
    }
    EXPECT_EQ(panel.poll(), DocumentPanelAction::CONFIRM_OVERWRITE);
    EXPECT_TRUE(panel.containsCursor({20, 120}));
    EXPECT_FALSE(panel.containsCursor({20, 240}));
    document.cancelPending();
    panel.update(document);
    EXPECT_EQ(panel.poll(), DocumentPanelAction::NONE);
    EXPECT_EQ(panel.target(), target);
}

TEST(VoxelEditorDocumentPanel, UntitledPendingActionCanSupplySaveAsTarget) {
    IREntity::EntityManager entities;
    DocumentPanel panel;
    panel.create({"data", ""});
    DocumentLifecycle document;
    document.markEdited();
    document.request(DocumentAction::NEW_DOCUMENT);
    document.failed("Enter a name");
    panel.update(document);
    for (const auto widget : panel.widgets()) {
        if (IREntity::getComponentOptional<IRComponents::C_WidgetTextInput>(widget))
            EXPECT_FALSE(IREntity::getComponent<IRComponents::C_Widget>(widget).disabled_);
    }
    panel.setTarget({"data", "rescue"});
    document.saved(panel.target());
    EXPECT_TRUE(document.pendingReady());
    EXPECT_FALSE(document.dirty());
    EXPECT_EQ(document.location().baseName_, "rescue");
}

} // namespace
