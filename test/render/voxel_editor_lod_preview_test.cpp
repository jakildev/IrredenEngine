#include "../../creations/editors/voxel_editor/lod_preview.hpp"
#include "../../creations/editors/voxel_editor/lod_comparison_panel.hpp"
#include <gtest/gtest.h>
#include <irreden/entity/entity_manager.hpp>

namespace {
using namespace IRVoxelEditor;
using namespace IRComponents;
using IRRender::LodLevel;
IRRender::ComparisonImage solidImage(IRMath::ivec2 size) {
    return {
        size,
        std::vector<IRMath::Color>(
            static_cast<std::size_t>(size.x) * size.y,
            IRMath::Color{50, 80, 120, 255}
        )
    };
}
TEST(VoxelEditorLodCapture, FiveActualZoomsUseOneWorldApertureAndRejectWrongCaptures) {
    LodCapturePlan plan;
    EXPECT_FALSE(plan.request());
    EXPECT_FALSE(plan.begin({17, 16}));
    EXPECT_FALSE(plan.begin({1040, 16}));
    ASSERT_TRUE(plan.begin({512, 256}, true));
    EXPECT_TRUE(plan.captureClipped());
    for (int tier = 0; tier < 5; ++tier) {
        const auto next = plan.request();
        ASSERT_TRUE(next);
        EXPECT_EQ(next->tier_, static_cast<LodLevel>(tier));
        EXPECT_EQ(IRRender::computeLodLevel(next->zoom_), next->tier_);
        EXPECT_EQ(next->size_ * next->magnification_, IRMath::ivec2(512, 256));
        EXPECT_FALSE(plan.accept(solidImage({1, 1})));
        EXPECT_EQ(plan.captured(), static_cast<std::size_t>(tier));
        ASSERT_TRUE(plan.accept(solidImage(next->size_)));
    }
    EXPECT_TRUE(plan.complete());
    EXPECT_FALSE(plan.request());
    EXPECT_FALSE(plan.accept(solidImage({1, 1})));
    plan.clear();
    EXPECT_FALSE(plan.captureClipped());
    EXPECT_FALSE(plan.complete());
}
class VoxelEditorLodPreview : public testing::Test {
  protected:
    void TearDown() override {
        preview_.stop();
        scene_.clear();
    }
    IREntity::EntityId addSource() {
        std::vector<C_Voxel> cells(24, C_Voxel{IRMath::Color{11, 22, 33, 255}});
        cells[4].reserved_ = VoxelReserved::kPriorityMask;
        cells[4].material_id_ = 7;
        const auto source = IREntity::createEntity(
            C_LocalTransform{IRMath::vec3(4, 5, 6)},
            C_VoxelSetNew{
                C_VoxelSetNew::StagedInit{},
                IRMath::ivec3(2, 3, 4),
                IRMath::ivec3(0),
                std::move(cells),
                IREntity::kNullEntity,
                EntityAnchor::CENTER
            }
        );
        scene_.adoptVoxelPart(source);
        return source;
    }
    IRScript::LuaScript script_;
    IREntity::EntityManager entities_;
    EntityScene scene_;
    LodPreviewSession preview_;
};
TEST_F(VoxelEditorLodPreview, PinsCloneHierarchyWithoutChangingAuthoredBandsOrPose) {
    const auto source = addSource();
    const auto parent = scene_.addShapePart({}, C_LocalTransform{IRMath::vec3(7, 8, 9)});
    ASSERT_TRUE(scene_.reparentPart("part_0", "part_1", false).ok_);
    scene_.setPartBand(0, LodLevel::LOD_1, LodLevel::LOD_3);
    const auto rest = IREntity::getComponent<C_LocalTransform>(source);
    ASSERT_TRUE(preview_.begin(scene_, IRMath::vec3(1000, 0, 0)).ok_);
    EXPECT_FALSE(preview_.begin(scene_, IRMath::vec3(0)).ok_);
    const auto copy = preview_.part("part_0");
    const auto copyParent = preview_.part("part_1");
    EXPECT_EQ(entities_.getParent(copy), copyParent);
    EXPECT_TRUE(IREntity::getComponentOptional<C_EditorReference>(copy));
    for (int tier = 0; tier < 5; ++tier) {
        preview_.setTier(static_cast<LodLevel>(tier));
        EXPECT_EQ(
            IREntity::getComponent<C_LodTierOverride>(copy).tier_,
            static_cast<LodLevel>(tier)
        );
        EXPECT_EQ(
            IREntity::getComponent<C_LodTierOverride>(copyParent).tier_,
            static_cast<LodLevel>(tier)
        );
        EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(copy).translation_, rest.translation_);
    }
    const auto &set = IREntity::getComponent<C_VoxelSetNew>(copy);
    EXPECT_EQ(set.size_, IRMath::ivec3(2, 3, 4));
    EXPECT_EQ(set.anchor_, EntityAnchor::CENTER);
    EXPECT_EQ(set.authoredRecords()[4].reserved_, VoxelReserved::kPriorityMask);
    EXPECT_EQ(set.lodMax_, LodLevel::LOD_1);
    EXPECT_EQ(set.lodMin_, LodLevel::LOD_3);
    EXPECT_EQ(entities_.getParent(source), parent);
    EXPECT_EQ(scene_.parts().size(), 2u);
    EXPECT_FALSE(IREntity::getComponentOptional<C_LodTierOverride>(source));
    preview_.stop();
    EXPECT_TRUE(entities_.isMarkedForDeletion(copy));
    EXPECT_FALSE(entities_.isMarkedForDeletion(source));
}
TEST_F(
    VoxelEditorLodPreview, DerivedTierCopiesExtentAnchorFullRecordsAndParentWithoutStealingChildren
) {
    const auto source = addSource();
    const auto child = scene_.addShapePart({}, C_LocalTransform{IRMath::vec3(1, 2, 3)});
    ASSERT_TRUE(scene_.reparentPart("part_1", "part_0", false).ok_);
    auto &local = IREntity::getComponent<C_LocalTransform>(source);
    local.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), .4f);
    local.scale_ = IRMath::vec3(2, 3, 4);
    const auto rest = local;
    const auto derived = deriveLodPart(scene_, "part_0", LodLevel::LOD_2);
    ASSERT_TRUE(derived);
    EXPECT_TRUE(derived->overlapsSourceBand_);
    EXPECT_TRUE(derived->snapshot_.detachedChildren_.empty());
    EXPECT_EQ(derived->snapshot_.transform_.translation_, rest.translation_);
    EXPECT_EQ(derived->snapshot_.transform_.rotation_, rest.rotation_);
    EXPECT_EQ(derived->snapshot_.transform_.scale_, rest.scale_);
    const auto result = restoreDerivedLodPart(scene_, script_, *derived);
    ASSERT_TRUE(result.ok()) << result.error_;
    const auto &copy = IREntity::getComponent<C_VoxelSetNew>(result.entity_);
    EXPECT_EQ(copy.size_, IRMath::ivec3(2, 3, 4));
    EXPECT_EQ(copy.anchor_, EntityAnchor::CENTER);
    EXPECT_EQ(copy.authoredRecords()[4].reserved_, VoxelReserved::kPriorityMask);
    EXPECT_EQ(copy.authoredRecords()[4].material_id_, 7);
    EXPECT_EQ(copy.lodMin_, LodLevel::LOD_2);
    EXPECT_EQ(copy.lodMax_, LodLevel::LOD_2);
    EXPECT_EQ(entities_.getParent(child), source);
    EXPECT_EQ(scene_.findPart("part_0")->lodMin_, LodLevel::LOD_4);
    EXPECT_EQ(scene_.findPart("part_0")->lodMax_, LodLevel::LOD_0);
    EXPECT_FALSE(restoreDerivedLodPart(scene_, script_, *derived).ok());
    const auto another = deriveLodPart(scene_, "part_0", LodLevel::LOD_2);
    ASSERT_TRUE(another);
    EXPECT_NE(another->snapshot_.part_.id_, derived->snapshot_.part_.id_);
    EXPECT_FALSE(deriveLodPart(scene_, "missing", LodLevel::LOD_2));
}
TEST_F(VoxelEditorLodPreview, ComparisonPanelUsesCachedCapturesAndExplicitVisibility) {
    LodComparisonPanel panel;
    ASSERT_TRUE(panel.create({220, 180}));
    EXPECT_FALSE(panel.create({0, 0}));
    for (const auto id : panel.widgets())
        EXPECT_FALSE(IRPrefab::Widget::isVisible(id));
    LodCapturePlan plan;
    ASSERT_TRUE(plan.begin({512, 256}));
    while (const auto request = plan.request())
        ASSERT_TRUE(plan.accept(solidImage(request->size_)));
    ASSERT_TRUE(panel.setImages(plan));
    EXPECT_EQ(panel.selectedTier(), LodLevel::LOD_0);
    EXPECT_EQ(panel.referenceTier(), LodLevel::LOD_1);
    EXPECT_FLOAT_EQ(panel.opacity(), .5f);
    EXPECT_EQ(panel.poll(), LodComparisonAction::NONE);
    panel.setVisible(true);
    int images = 0;
    for (const auto id : panel.widgets()) {
        EXPECT_TRUE(IRPrefab::Widget::isVisible(id));
        if (const auto image = IREntity::getComponentOptional<C_WidgetImage>(id)) {
            ++images;
            EXPECT_TRUE((*image)->loaded_);
            EXPECT_EQ((*image)->texelSize_, IRMath::ivec2(128, 96));
            EXPECT_EQ((*image)->texels_.size(), 128u * 96u);
        }
    }
    EXPECT_EQ(images, 11);
}
} // namespace
