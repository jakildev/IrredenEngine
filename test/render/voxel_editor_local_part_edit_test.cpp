#include <gtest/gtest.h>

#include "../../creations/editors/voxel_editor/local_part_edit.hpp"

namespace {
class VoxelEditorLocalPartEdit : public testing::Test {
  protected:
    IREntity::EntityManager manager_;

    IREntity::EntityId addSource(IRVoxelEditor::EntityScene &scene) {
        const auto entity = IREntity::createEntity(
            IRComponents::C_LocalTransform{IRMath::vec3(7.0f, 3.0f, -2.0f)},
            IRComponents::C_VoxelSetNew{
                IRComponents::C_VoxelSetNew::StagedInit{},
                IRMath::ivec3(3),
                IRMath::ivec3(0),
                std::vector<IRComponents::C_Voxel>(
                    27,
                    IRComponents::C_Voxel{IRMath::Color{0, 0, 0, 0}}
                ),
                IREntity::kNullEntity,
                IRComponents::EntityAnchor::CENTER
            }
        );
        scene.adoptVoxelPart(entity);
        return entity;
    }
};

TEST_F(VoxelEditorLocalPartEdit, ProxyKeepsNestedPoseAndEachRenderModeAndNeverBecomesAPart) {
    for (const auto mode :
         {IRComponents::RotationMode::GRID,
          IRComponents::RotationMode::DETACHED,
          IRComponents::RotationMode::DETACHED_REVOXELIZE}) {
        IRVoxelEditor::EntityScene scene;
        const auto source = addSource(scene);
        IRComponents::C_LocalTransform parentLocal{IRMath::vec3(5.0f)};
        parentLocal.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(1, 0, 0), 0.7f);
        parentLocal.scale_ = IRMath::vec3(2.0f, 1.5f, 0.75f);
        const auto parent = scene.addShapePart({}, parentLocal);
        ASSERT_TRUE(scene.reparentPart("part_0", "part_1", false).ok_);
        auto &local = IREntity::getComponent<IRComponents::C_LocalTransform>(source);
        local.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0, 1, 0), 0.4f);
        const auto before = local;
        IREntity::setComponent(source, IRComponents::C_RotationMode{mode});
        scene.setPartRenderState(source, mode, IRMath::ivec2(64));
        auto &seed = IREntity::getComponent<IRComponents::C_VoxelSetNew>(source);
        seed.mutableAuthoredRecords()[4].color_ = IRMath::Color{11, 22, 33, 255};
        seed.mutableAuthoredRecords()[4].material_id_ = 7;
        seed.mutableAuthoredRecords()[4].reserved_ = IRComponents::VoxelReserved::kPriorityMask;
        seed.resyncAfterAuthoredEdits();
        IRVoxelEditor::LocalPartEditSession edit;
        ASSERT_TRUE(edit.begin(scene, "part_0", IRMath::vec3(500, 0, 0)));
        const auto proxy = edit.proxy();
        EXPECT_NE(proxy, source);
        EXPECT_EQ(scene.parts().size(), 2u);
        EXPECT_EQ(manager_.getParent(source), parent);
        EXPECT_EQ(manager_.getParent(proxy), IREntity::kNullEntity);
        const auto initial =
            IREntity::getComponent<IRComponents::C_VoxelSetNew>(proxy).authoredRecords()[4];
        EXPECT_EQ(initial.material_id_, 7);
        EXPECT_EQ(initial.reserved_, IRComponents::VoxelReserved::kPriorityMask);
        EXPECT_EQ(initial.color_.red_, 11);
        EXPECT_EQ(edit.canonical(scene, proxy), source);
        EXPECT_EQ(
            IREntity::getComponent<IRComponents::C_RotationMode>(proxy).mode_,
            IRComponents::RotationMode::GRID
        );
        auto &original = IREntity::getComponent<IRComponents::C_VoxelSetNew>(source);
        auto records = original.mutableAuthoredRecords();
        records[4].color_ = IRMath::Color{11, 22, 33, 255};
        records[4].bone_id_ = 3;
        original.resyncAfterAuthoredEdits();
        ASSERT_TRUE(edit.refresh(scene));
        const auto &preview = IREntity::getComponent<IRComponents::C_VoxelSetNew>(proxy);
        EXPECT_EQ(preview.authoredRecords()[4].color_.red_, 11);
        EXPECT_EQ(preview.authoredRecords()[4].bone_id_, 3);
        EXPECT_EQ(preview.anchor_, IRComponents::EntityAnchor::CENTER);
        edit.close();
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(scene.findPart("part_0")->parentId_, "part_1");
        const auto &after = IREntity::getComponent<IRComponents::C_LocalTransform>(source);
        EXPECT_EQ(after.translation_, before.translation_);
        EXPECT_EQ(after.rotation_, before.rotation_);
        EXPECT_EQ(after.scale_, before.scale_);
        EXPECT_EQ(IREntity::getComponent<IRComponents::C_RotationMode>(source).mode_, mode);
        EXPECT_EQ(scene.findPart("part_0")->canvasSize_, IRMath::ivec2(64));
        scene.clear();
    }
}

TEST_F(VoxelEditorLocalPartEdit, AuthoredEditsDoNotOverwriteRotatedSourceWithRasterOutput) {
    auto entity = IREntity::createEntity(IRComponents::C_VoxelSetNew{});
    auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(entity);
    std::vector<IRComponents::C_Voxel> raster(8, IRComponents::C_Voxel{IRMath::Color{0, 0, 0, 0}});
    raster[2].color_ = IRMath::Color{99, 0, 0, 255};
    set.size_ = IRMath::ivec3(2);
    set.numVoxels_ = 8;
    set.voxels_ = raster;
    set.rotationSourceVoxels_.assign(8, IRComponents::C_Voxel{IRMath::Color{0, 0, 0, 0}});
    set.rotationSourceVoxels_[1].color_ = IRMath::Color{17, 0, 0, 255};
    set.mutableAuthoredRecords()[1].color_ = IRMath::Color{31, 0, 0, 255};
    set.resyncAfterAuthoredEdits();
    EXPECT_EQ(set.authoredRecords()[1].color_.red_, 31);
    EXPECT_EQ(set.authoredRecords()[2].color_.alpha_, 0);
    EXPECT_EQ(raster[2].color_.red_, 99);
    // The synthetic span owns no pool allocation.
    set.numVoxels_ = 0;
    set.voxels_ = {};
}

TEST_F(VoxelEditorLocalPartEdit, RefreshRefusesChangedExtentAndMissingSource) {
    IRVoxelEditor::EntityScene scene;
    const auto source = addSource(scene);
    IRVoxelEditor::LocalPartEditSession edit;
    EXPECT_FALSE(edit.begin(scene, "missing", IRMath::vec3(0)));
    ASSERT_TRUE(edit.begin(scene, "part_0", IRMath::vec3(100, 0, 0)));
    auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(source);
    set.size_ = IRMath::ivec3(1);
    EXPECT_FALSE(edit.refresh(scene));
    set.size_ = IRMath::ivec3(3);
    ASSERT_TRUE(edit.refresh(scene));
    scene.removeParts({source});
    EXPECT_FALSE(edit.refresh(scene));
    edit.close();
    scene.clear();
}
} // namespace
