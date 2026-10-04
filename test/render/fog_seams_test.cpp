#include <gtest/gtest.h>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_canvas.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_shape.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_EntityCanvas;
using IRComponents::C_FogRevealed;
using IRComponents::C_ShapeDescriptor;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::FogOverride;
using IRComponents::FrameDataFogObservers;

FrameDataFogObservers sourceWithChannels(std::uint32_t channels, int slot = 0) {
    FrameDataFogObservers observers{};
    for (int i = 0; i < slot; ++i) {
        EXPECT_EQ(
            C_CanvasFogOfWar::addVisionCircle(
                observers,
                40.0f + static_cast<float>(i),
                40.0f,
                1.0f,
                0.0f,
                0.0f,
                0.0f,
                IRComponents::kFogVisionZCostMirrorUp,
                0.0f,
                0u
            ),
            i
        );
    }
    EXPECT_EQ(
        C_CanvasFogOfWar::addVisionCircle(
            observers,
            0.0f,
            0.0f,
            10.0f,
            0.0f,
            0.0f,
            0.0f,
            IRComponents::kFogVisionZCostMirrorUp,
            0.0f,
            channels
        ),
        slot
    );
    return observers;
}

TEST(FogSeamsTest, ChannelsFilterCirclesButNotTheGridTerm) {
    const FrameDataFogObservers observers = sourceWithChannels(0b10u, 5);
    const IRMath::vec3 inside(0.0f);

    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(observers, {}, IRComponents::kFogStateUnexplored, inside, 0b01u),
        0.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(observers, {}, IRComponents::kFogStateUnexplored, inside, 0b11u),
        1.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(observers, {}, IRComponents::kFogStateVisible, inside, 0b01u),
        1.0f
    );
}

TEST(FogSeamsTest, ClearingVisionCirclesRestoresDefaultChannelLanes) {
    FrameDataFogObservers observers{};
    observers.visionCircleChannels_[0] = IRMath::uvec4(0u);
    observers.visionCircleChannels_[1] = IRMath::uvec4(0b10u);

    C_CanvasFogOfWar::clearVisionCircles(observers);

    for (int source = 0; source < IRComponents::kMaxFogVisionCircles; ++source) {
        EXPECT_EQ(observers.channels(source), IRComponents::kFogChannelDefault);
    }
}

TEST(FogSeamsTest, VoxelEvaluatorAppliesOverridesImmediatelyAndNoneUsesTheField) {
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    C_VoxelPool pool{IRMath::ivec3(2, 2, 2)};
    system.fogAttached_ = true;
    system.activePool_ = &pool;
    system.observers_ = sourceWithChannels(0b10u);
    system.settings_.staggerPeriod_ = 100;
    system.pending_.reset(4);

    C_VoxelSetNew voxelSet{};
    C_WorldTransform inside{};
    C_WorldTransform outside{};
    outside.translation_ = IRMath::vec3(40.0f, 40.0f, 0.0f);

    IREntity::EntityId forcedEntity = 1;
    C_FogRevealed forcedHidden{1.0f, true, FogOverride::FORCE_HIDDEN};
    system.tick(forcedEntity, forcedHidden, inside, voxelSet);
    EXPECT_FLOAT_EQ(forcedHidden.revealFactor_, 0.0f);
    EXPECT_FALSE(forcedHidden.shown_);

    C_FogRevealed forcedRevealed{0.0f, false, FogOverride::FORCE_REVEALED};
    system.tick(forcedEntity, forcedRevealed, outside, voxelSet);
    EXPECT_FLOAT_EQ(forcedRevealed.revealFactor_, 1.0f);
    EXPECT_TRUE(forcedRevealed.shown_);

    IREntity::EntityId scheduledEntity = 100;
    C_FogRevealed insideControl{0.0f, false, FogOverride::NONE, 0b11u};
    system.tick(scheduledEntity, insideControl, inside, voxelSet);
    EXPECT_FLOAT_EQ(insideControl.revealFactor_, 1.0f);
    EXPECT_TRUE(insideControl.shown_);

    C_FogRevealed outsideControl{1.0f, true, FogOverride::NONE, 0b10u};
    system.tick(scheduledEntity, outsideControl, outside, voxelSet);
    EXPECT_FLOAT_EQ(outsideControl.revealFactor_, 0.0f);
    EXPECT_FALSE(outsideControl.shown_);
}

TEST(FogSeamsTest, ShapeEvaluatorHonoursOverridesAndChannels) {
    C_CanvasFogOfWar fog{C_CanvasFogOfWar::HeadlessInit{}};
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_SHAPE> system;
    system.fog_ = &fog;
    system.observers_ = sourceWithChannels(0b10u);
    system.settings_.staggerPeriod_ = 1;

    IREntity::EntityId entity = 1;
    C_WorldTransform inside{};
    C_WorldTransform outside{};
    outside.translation_ = IRMath::vec3(40.0f, 40.0f, 0.0f);
    C_ShapeDescriptor shape{};

    C_FogRevealed forcedHidden{1.0f, true, FogOverride::FORCE_HIDDEN, 0b10u};
    system.tick(entity, forcedHidden, inside, shape);
    EXPECT_FLOAT_EQ(forcedHidden.revealFactor_, 0.0f);
    EXPECT_FALSE(forcedHidden.shown_);

    C_FogRevealed forcedRevealed{0.0f, false, FogOverride::FORCE_REVEALED, 0b01u};
    system.tick(entity, forcedRevealed, outside, shape);
    EXPECT_FLOAT_EQ(forcedRevealed.revealFactor_, 1.0f);
    EXPECT_TRUE(forcedRevealed.shown_);

    C_FogRevealed disjoint{0.0f, false, FogOverride::NONE, 0b01u};
    system.tick(entity, disjoint, inside, shape);
    EXPECT_FLOAT_EQ(disjoint.revealFactor_, 0.0f);
    EXPECT_FALSE(disjoint.shown_);

    C_FogRevealed intersecting{0.0f, false, FogOverride::NONE, 0b11u};
    system.tick(entity, intersecting, inside, shape);
    EXPECT_FLOAT_EQ(intersecting.revealFactor_, 1.0f);
    EXPECT_TRUE(intersecting.shown_);

    C_FogRevealed outsideControl{1.0f, true, FogOverride::NONE, 0b10u};
    system.tick(entity, outsideControl, outside, shape);
    EXPECT_FLOAT_EQ(outsideControl.revealFactor_, 0.0f);
    EXPECT_FALSE(outsideControl.shown_);
}

TEST(FogSeamsTest, CanvasEvaluatorHonoursOverridesAndChannels) {
    IREntity::EntityManager entityManager;
    C_CanvasFogOfWar fog{C_CanvasFogOfWar::HeadlessInit{}};
    fog.observers_ = sourceWithChannels(0b10u);
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS> system;
    system.fog_ = &fog;
    system.settings_.staggerPeriod_ = 100;

    const IREntity::EntityId privateCanvas =
        IREntity::createEntity(IRComponents::C_DetachedCanvas{});
    IREntity::EntityId entity = 1;
    C_WorldTransform inside{};
    C_WorldTransform outside{};
    outside.translation_ = IRMath::vec3(40.0f, 40.0f, 0.0f);
    C_EntityCanvas canvas{};
    canvas.canvasEntity_ = privateCanvas;

    C_FogRevealed forcedHidden{1.0f, true, FogOverride::FORCE_HIDDEN, 0b10u};
    system.tick(entity, forcedHidden, inside, canvas);
    EXPECT_FLOAT_EQ(forcedHidden.revealFactor_, 0.0f);
    EXPECT_FALSE(forcedHidden.shown_);
    EXPECT_TRUE(canvas.fogHidden_);

    C_FogRevealed forcedRevealed{0.0f, false, FogOverride::FORCE_REVEALED, 0b01u};
    system.tick(entity, forcedRevealed, outside, canvas);
    EXPECT_FLOAT_EQ(forcedRevealed.revealFactor_, 1.0f);
    EXPECT_TRUE(forcedRevealed.shown_);
    EXPECT_FALSE(canvas.fogHidden_);

    entity = 100;
    C_FogRevealed disjoint{0.0f, false, FogOverride::NONE, 0b01u};
    system.tick(entity, disjoint, inside, canvas);
    EXPECT_FLOAT_EQ(disjoint.revealFactor_, 0.0f);
    EXPECT_FALSE(disjoint.shown_);

    C_FogRevealed intersecting{0.0f, false, FogOverride::NONE, 0b11u};
    system.tick(entity, intersecting, inside, canvas);
    EXPECT_FLOAT_EQ(intersecting.revealFactor_, 1.0f);
    EXPECT_TRUE(intersecting.shown_);
}

} // namespace
