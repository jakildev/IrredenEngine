#include "../../creations/editors/voxel_editor/motion_preview.hpp"
#include "../../creations/editors/voxel_editor/motion_preview_panel.hpp"

#include <gtest/gtest.h>
#include <irreden/entity/entity_manager.hpp>

#include <limits>

namespace {

using namespace IRVoxelEditor;
using namespace IRComponents;

TEST(VoxelEditorSpinPreview, SeeksFromRestWithoutAccumulationAndPreservesOtherChannels) {
    C_LocalTransform rest;
    rest.translation_ = IRMath::vec3(1, 2, 3);
    rest.scale_ = IRMath::vec3(2, 3, 4);
    rest.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(1, 0, 0), 0.6f);
    const C_AutoSpin spin{IRMath::vec3(0, 0, 2), 0.01f};
    const auto at60 = sampleSpinPreview(rest, spin, 60);
    EXPECT_EQ(at60.translation_, rest.translation_);
    EXPECT_EQ(at60.scale_, rest.scale_);
    EXPECT_NE(at60.rotation_, rest.rotation_);
    EXPECT_EQ(sampleSpinPreview(rest, spin, 0).rotation_, rest.rotation_);
    EXPECT_EQ(sampleSpinPreview(rest, spin, 60).rotation_, at60.rotation_);
    auto runtime = rest;
    for (int i = 0; i < 60; ++i)
        runtime.rotation_ = IRMath::quatMul(
            IRMath::quatAxisAngle(spin.axis_, spin.radiansPerFrame_),
            runtime.rotation_
        );
    EXPECT_NEAR(IRMath::length(runtime.rotation_ - at60.rotation_), 0.0f, 0.00001f);
}

TEST(VoxelEditorSpinPreview, MissingZeroAndInvalidMotionKeepRestPose) {
    const C_LocalTransform rest;
    EXPECT_EQ(sampleSpinPreview(rest, std::nullopt, 30).rotation_, rest.rotation_);
    EXPECT_EQ(
        sampleSpinPreview(rest, C_AutoSpin{IRMath::vec3(0), 1.0f}, 30).rotation_,
        rest.rotation_
    );
    EXPECT_EQ(
        sampleSpinPreview(rest, C_AutoSpin{IRMath::vec3(1), 0.0f}, 30).rotation_,
        rest.rotation_
    );
    EXPECT_EQ(
        sampleSpinPreview(
            rest,
            C_AutoSpin{IRMath::vec3(1), std::numeric_limits<float>::infinity()},
            30
        )
            .rotation_,
        rest.rotation_
    );
    EXPECT_EQ(
        sampleSpinPreview(
            rest,
            C_AutoSpin{IRMath::vec3(std::numeric_limits<float>::quiet_NaN()), 1.0f},
            30
        )
            .rotation_,
        rest.rotation_
    );
}

class VoxelEditorMotionPreview : public testing::Test {
  protected:
    void SetUp() override {
        m_scene.begin();
    }
    void TearDown() override {
        m_preview.stop();
        m_scene.clear();
    }

    IREntity::EntityId addPart(const std::string &id, const std::string &parent = {}) {
        const auto entity = IREntity::createEntity(
            C_LocalTransform{IRMath::vec3(1, 2, 3)},
            C_ShapeDescriptor{},
            C_AutoSpin{IRMath::vec3(0, 0, 1), 0.02f}
        );
        IREntity::setParent(
            entity,
            parent.empty() ? m_scene.root() : m_scene.findPart(parent)->entity_
        );
        EditorPart part;
        part.id_ = id;
        part.parentId_ = parent;
        part.entity_ = entity;
        part.kind_ = EditorPartKind::SHAPE;
        m_scene.parts().push_back(std::move(part));
        return entity;
    }

    IREntity::EntityManager m_entities;
    EntityScene m_scene;
    MotionPreview m_preview;
};

TEST_F(VoxelEditorMotionPreview, ClonesHierarchyAndNeverMutatesAuthoredState) {
    const auto parent = addPart("parent");
    const auto child = addPart("child", "parent");
    const auto parentRest = IREntity::getComponent<C_LocalTransform>(parent);
    const auto childRest = IREntity::getComponent<C_LocalTransform>(child);
    const auto rootRest = IREntity::getComponent<C_LocalTransform>(m_scene.root());
    ASSERT_TRUE(m_preview.begin(m_scene, IRMath::vec3(1000, 0, 0)).ok_);
    const auto previewChild = m_preview.part("child");
    EXPECT_NE(previewChild, child);
    EXPECT_EQ(m_entities.getParent(previewChild), m_preview.part("parent"));
    EXPECT_TRUE(IREntity::getComponentOptional<C_EditorReference>(previewChild));
    EXPECT_FALSE(IREntity::getComponentOptional<C_AutoSpin>(previewChild));
    m_preview.seek(60);
    EXPECT_NE(
        IREntity::getComponent<C_LocalTransform>(previewChild).rotation_,
        childRest.rotation_
    );
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(parent).rotation_, parentRest.rotation_);
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(child).rotation_, childRest.rotation_);
    EXPECT_EQ(
        IREntity::getComponent<C_LocalTransform>(m_scene.root()).translation_,
        rootRest.translation_
    );
    EXPECT_EQ(m_entities.getParent(child), parent);
    m_preview.seek(0);
    EXPECT_EQ(
        IREntity::getComponent<C_LocalTransform>(previewChild).rotation_,
        childRest.rotation_
    );
    m_preview.stop();
    EXPECT_TRUE(m_entities.isMarkedForDeletion(previewChild));
    m_entities.destroyMarkedEntities();
    EXPECT_FALSE(IREntity::entityExists(previewChild));
    EXPECT_TRUE(IREntity::entityExists(child));
    EXPECT_FALSE(m_preview.active());
}

TEST_F(VoxelEditorMotionPreview, RefusesMissingParentAndRepeatedBegin) {
    EXPECT_FALSE(m_preview.begin(m_scene, IRMath::vec3(0)).ok_);
    addPart("part");
    m_scene.parts()[0].parentId_ = "absent";
    EXPECT_FALSE(m_preview.begin(m_scene, IRMath::vec3(0)).ok_);
    EXPECT_FALSE(m_preview.active());
    m_scene.parts()[0].parentId_.clear();
    ASSERT_TRUE(m_preview.begin(m_scene, IRMath::vec3(0)).ok_);
    EXPECT_FALSE(m_preview.begin(m_scene, IRMath::vec3(0)).ok_);
    m_preview.stop();
    ASSERT_TRUE(m_preview.begin(m_scene, IRMath::vec3(0)).ok_);
    EXPECT_EQ(m_preview.tick(), 0);
}

TEST_F(VoxelEditorMotionPreview, PanelVisibilityAndSeekStateAreExplicit) {
    MotionPreviewPanel panel;
    panel.create();
    for (const auto widget : panel.widgets())
        EXPECT_FALSE(IRPrefab::Widget::isVisible(widget));
    panel.setVisible(true);
    panel.update(42, false);
    EXPECT_EQ(panel.requestedTick(), 42);
    EXPECT_EQ(panel.poll(), MotionPreviewAction::NONE);
    panel.setVisible(false);
    for (const auto widget : panel.widgets())
        EXPECT_FALSE(IRPrefab::Widget::isVisible(widget));
}

TEST_F(VoxelEditorMotionPreview, VoxelCopiesPreserveAuthoredPayloadAndOwnTheirStorage) {
    std::vector<C_Voxel> cells(27, C_Voxel{IRMath::Color{11, 22, 33, 255}});
    cells[4].reserved_ = VoxelReserved::kPriorityMask;
    cells[4].material_id_ = 7;
    const auto source = IREntity::createEntity(
        C_VoxelSetNew{
            C_VoxelSetNew::StagedInit{},
            IRMath::ivec3(3),
            IRMath::ivec3(0),
            std::move(cells),
            IREntity::kNullEntity,
            EntityAnchor::CENTER
        }
    );
    ASSERT_NE(m_scene.adoptVoxelPart(source), IREntity::kNullEntity);
    m_scene.setPartBand(0, IRRender::LodLevel::LOD_1, IRRender::LodLevel::LOD_3);
    ASSERT_TRUE(m_preview.begin(m_scene, IRMath::vec3(1000, 0, 0)).ok_);
    const auto copy = m_preview.part("part_0");
    auto &previewSet = IREntity::getComponent<C_VoxelSetNew>(copy);
    EXPECT_EQ(previewSet.anchor_, EntityAnchor::CENTER);
    EXPECT_EQ(previewSet.lodMax_, IRRender::LodLevel::LOD_1);
    EXPECT_EQ(previewSet.lodMin_, IRRender::LodLevel::LOD_3);
    EXPECT_EQ(previewSet.authoredRecords()[4].reserved_, VoxelReserved::kPriorityMask);
    EXPECT_EQ(previewSet.authoredRecords()[4].material_id_, 7);
    previewSet.mutableAuthoredRecords()[4].color_ = IRMath::Color{255, 0, 0, 255};
    EXPECT_EQ(IREntity::getComponent<C_VoxelSetNew>(source).authoredRecords()[4].color_.red_, 11);
    m_preview.stop();
    EXPECT_TRUE(m_entities.isMarkedForDeletion(copy));
    m_entities.destroyMarkedEntities();
    EXPECT_FALSE(IREntity::entityExists(copy));
    EXPECT_TRUE(IREntity::entityExists(source));
}

TEST_F(VoxelEditorMotionPreview, StoppedPreviewSkipsPendingDetachedCanvasAllocation) {
    const auto source = IREntity::createEntity(
        C_VoxelSetNew{
            C_VoxelSetNew::StagedInit{},
            IRMath::ivec3(2),
            IRMath::ivec3(0),
            std::vector<C_Voxel>(8, C_Voxel{IRMath::Color{11, 22, 33, 255}}),
            IREntity::kNullEntity,
            EntityAnchor::CENTER
        }
    );
    ASSERT_NE(m_scene.adoptVoxelPart(source), IREntity::kNullEntity);
    m_scene.setPartRenderState(source, RotationMode::DETACHED, IRMath::ivec2(32));
    ASSERT_TRUE(m_preview.begin(m_scene, IRMath::vec3(1000, 0, 0)).ok_);
    const auto copy = m_preview.part("part_0");
    m_preview.stop();
    ASSERT_TRUE(m_entities.isMarkedForDeletion(copy));
    // The pending callback must not allocate a renderer canvas for a stopped preview.
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(IREntity::getComponentOptional<C_EntityCanvas>(copy));
    m_entities.destroyMarkedEntities();
    EXPECT_FALSE(IREntity::entityExists(copy));
    EXPECT_TRUE(IREntity::entityExists(source));
}

} // namespace
