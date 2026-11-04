#include "../../creations/editors/voxel_editor/part_edit_history.hpp"

#include <gtest/gtest.h>

#include <irreden/entity/entity_manager.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <utility>

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_ShapeDescriptor;
using IRMath::vec3;
using IRMath::vec4;
using IRRender::LodLevel;
using namespace IRVoxelEditor;

TEST(VoxelEditorPartEditGesture, KeepsPressStateAndFinishesOnlyOnce) {
    PartTransformGesture gesture;
    const C_LocalTransform before{vec3(1.0f, 2.0f, 3.0f)};
    C_LocalTransform after = before;
    after.translation_.z = 9.0f;
    EXPECT_TRUE(gesture.begin("part_a", before));
    EXPECT_FALSE(gesture.begin("part_b", after));
    ASSERT_TRUE(gesture.active());
    ASSERT_NE(gesture.pending(), nullptr);
    EXPECT_EQ(gesture.pending()->partId_, "part_a");

    const auto edit = gesture.finish(after);
    ASSERT_TRUE(edit);
    EXPECT_EQ(edit->partId_, "part_a");
    EXPECT_EQ(edit->before_.translation_, before.translation_);
    EXPECT_EQ(edit->after_.translation_, after.translation_);
    EXPECT_FALSE(gesture.active());
    EXPECT_EQ(gesture.pending(), nullptr);
    EXPECT_FALSE(gesture.finish(after));
}

TEST(VoxelEditorPartEditGesture, NoOpCancellationAndMissingTargetProduceNoRecord) {
    PartTransformGesture gesture;
    const C_LocalTransform before{vec3(2.0f)};
    EXPECT_TRUE(gesture.begin("part", before));
    EXPECT_FALSE(gesture.finish(before));

    EXPECT_TRUE(gesture.begin("part", before));
    gesture.cancel();
    EXPECT_FALSE(gesture.finish(C_LocalTransform{vec3(9.0f)}));

    EXPECT_TRUE(gesture.begin("part", before));
    EXPECT_FALSE(gesture.finish(std::nullopt));
    EXPECT_FALSE(gesture.active());
}

TEST(VoxelEditorPartEditGesture, EveryTransformChannelBelongsToTheRecord) {
    const C_LocalTransform before;
    for (int channel = 0; channel < 4; ++channel) {
        SCOPED_TRACE(channel);
        C_LocalTransform after = before;
        switch (channel) {
        case 0:
            after.translation_ = vec3(1.0f, 2.0f, 3.0f);
            break;
        case 1:
            after.rotation_ = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.5f);
            break;
        case 2:
            after.scale_ = vec3(2.0f, 3.0f, 4.0f);
            break;
        default:
            after.unbounded_ = true;
            break;
        }
        PartTransformGesture gesture;
        ASSERT_TRUE(gesture.begin("part", before));
        const auto edit = gesture.finish(after);
        ASSERT_TRUE(edit);
        EXPECT_EQ(edit->after_.translation_, after.translation_);
        EXPECT_EQ(edit->after_.rotation_, after.rotation_);
        EXPECT_EQ(edit->after_.scale_, after.scale_);
        EXPECT_EQ(edit->after_.unbounded_, after.unbounded_);
    }
}

class VoxelEditorPartEditHistory : public testing::Test {
  protected:
    IREntity::EntityId addShape(const std::string &id) {
        const auto entity = IREntity::createEntity(C_LocalTransform{}, C_ShapeDescriptor{});
        EditorPart part;
        part.entity_ = entity;
        part.id_ = id;
        part.kind_ = EditorPartKind::SHAPE;
        m_scene.parts().push_back(std::move(part));
        return entity;
    }

    IREntity::EntityManager m_entityManager;
    EntityScene m_scene;
};

TEST_F(VoxelEditorPartEditHistory, TransformReplayResolvesReorderedAndRecreatedParts) {
    const auto original = addShape("target");
    const auto other = addShape("other");
    const auto before = snapshotPartTransform(m_scene, "target");
    ASSERT_TRUE(before);
    C_LocalTransform after{vec3(4.0f, -3.0f, 2.0f), vec4(0.0f, 0.0f, 1.0f, 0.0f)};
    after.scale_ = vec3(2.0f);
    after.unbounded_ = true;
    PartTransformGesture gesture;
    ASSERT_TRUE(gesture.begin("target", *before));
    ASSERT_TRUE(applyPartTransform(m_scene, "target", after));
    const auto edit = gesture.finish(snapshotPartTransform(m_scene, "target"));
    ASSERT_TRUE(edit);

    std::swap(m_scene.parts()[0], m_scene.parts()[1]);
    ASSERT_TRUE(applyPartTransform(m_scene, edit->partId_, edit->before_));
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(original).translation_, vec3(0.0f));

    m_entityManager.destroyEntity(original);
    const auto replacement = IREntity::createEntity(C_LocalTransform{}, C_ShapeDescriptor{});
    m_scene.findPart("target")->entity_ = replacement;
    ASSERT_TRUE(applyPartTransform(m_scene, edit->partId_, edit->after_));
    const auto replayed = snapshotPartTransform(m_scene, "target");
    ASSERT_TRUE(replayed);
    EXPECT_EQ(replayed->translation_, after.translation_);
    EXPECT_EQ(replayed->rotation_, after.rotation_);
    EXPECT_EQ(replayed->scale_, after.scale_);
    EXPECT_EQ(replayed->unbounded_, after.unbounded_);
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(other).translation_, vec3(0.0f));
}

TEST_F(VoxelEditorPartEditHistory, LodReplayRestoresTheBandAndRenderComponent) {
    const auto target = addShape("target");
    addShape("other");
    const auto before = snapshotPartLodBand(m_scene, "target");
    ASSERT_TRUE(before);
    PartLodBandGesture gesture;
    ASSERT_TRUE(gesture.begin("target", *before));
    ASSERT_TRUE(applyPartLodBand(m_scene, "target", {LodLevel::LOD_1, LodLevel::LOD_2}));
    const auto edit = gesture.finish(snapshotPartLodBand(m_scene, "target"));
    ASSERT_TRUE(edit);
    std::swap(m_scene.parts()[0], m_scene.parts()[1]);

    ASSERT_TRUE(applyPartLodBand(m_scene, edit->partId_, edit->before_));
    EXPECT_EQ(m_scene.findPart("target")->lodMax_, LodLevel::LOD_0);
    EXPECT_EQ(m_scene.findPart("target")->lodMin_, LodLevel::LOD_4);
    EXPECT_EQ(IREntity::getComponent<C_ShapeDescriptor>(target).lodMax_, LodLevel::LOD_0);
    EXPECT_EQ(IREntity::getComponent<C_ShapeDescriptor>(target).lodMin_, LodLevel::LOD_4);

    ASSERT_TRUE(applyPartLodBand(m_scene, edit->partId_, edit->after_));
    EXPECT_EQ(IREntity::getComponent<C_ShapeDescriptor>(target).lodMax_, LodLevel::LOD_1);
    EXPECT_EQ(IREntity::getComponent<C_ShapeDescriptor>(target).lodMin_, LodLevel::LOD_2);
    EXPECT_EQ(m_scene.findPart("other")->lodMax_, LodLevel::LOD_0);
    EXPECT_EQ(m_scene.findPart("other")->lodMin_, LodLevel::LOD_4);

    ASSERT_TRUE(gesture.begin("target", edit->after_));
    EXPECT_FALSE(gesture.finish(snapshotPartLodBand(m_scene, "target")));
}

TEST_F(VoxelEditorPartEditHistory, MissingPartsCannotBeSnapshottedOrReplayed) {
    addShape("present");
    EXPECT_FALSE(snapshotPartTransform(m_scene, "missing"));
    EXPECT_FALSE(snapshotPartLodBand(m_scene, "missing"));
    EXPECT_FALSE(applyPartTransform(m_scene, "missing", C_LocalTransform{}));
    EXPECT_FALSE(applyPartLodBand(m_scene, "missing", PartLodBand{}));
}

} // namespace
