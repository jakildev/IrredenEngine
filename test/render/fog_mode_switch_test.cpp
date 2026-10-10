#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/canvas_part.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_reveal_systems.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

#include <cstddef>
#include <cstdint>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_DetachedCanvas;
using IRComponents::C_EntityCanvas;
using IRComponents::C_FogRevealed;
using IRComponents::C_FogRevealSettings;
using IRComponents::C_RotationMode;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::RotationMode;

constexpr IRMath::ivec3 kSetSize{2, 2, 2};
constexpr int kSetVoxels = 8;
constexpr IRMath::Color kSetColor{180, 100, 40, 255};
constexpr IRMath::vec3 kHiddenPosition{40.0f, 40.0f, 0.0f};

class FogModeSwitchTest : public testing::Test {
  protected:
    FogModeSwitchTest()
        : m_entityManager{}
        , m_systemManager{} {
        m_worldCanvas = IREntity::createEntity(
            C_VoxelPool{IRMath::ivec3(32, 32, 8)},
            C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}}
        );
        IRRender::setHeadlessActiveCanvasEntity(m_worldCanvas);
        fog().setCell(0, 0, IRComponents::kFogStateVisible);
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, IRPrefab::Fog::revealSystems());
    }

    ~FogModeSwitchTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    C_CanvasFogOfWar &fog() {
        return IREntity::getComponent<C_CanvasFogOfWar>(m_worldCanvas);
    }

    IREntity::EntityId createWorldBody(IRMath::vec3 position = kHiddenPosition) {
        return IREntity::createEntity(
            C_WorldTransform{position, IRMath::vec4(0, 0, 0, 1), IRMath::vec3(1.0f)},
            C_VoxelSetNew{kSetSize, kSetColor, true, m_worldCanvas},
            C_RotationMode{RotationMode::GRID}
        );
    }

    IREntity::EntityId createPrivateCanvas() {
        return IREntity::createEntity(
            C_VoxelPool{IRMath::ivec3(8, 8, 8)},
            C_DetachedCanvas{},
            C_CanvasLocalRotation{}
        );
    }

    void promoteByHand(IREntity::EntityId body, IREntity::EntityId privateCanvas) {
        auto &set = IREntity::getComponent<C_VoxelSetNew>(body);
        IRPrefab::VoxelPool::restageSet(set);
        ASSERT_TRUE(set.attachToCanvas(privateCanvas));
        IREntity::setComponent(body, C_EntityCanvas{privateCanvas, IRMath::ivec2(64)});
        IREntity::setComponent(body, C_RotationMode{RotationMode::DETACHED_REVOXELIZE});
    }

    void runFrame() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        IREntity::flushStructuralChanges();
    }

    static int activeBits(IREntity::EntityId entity) {
        const auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        const auto &mask = IREntity::getComponent<C_VoxelPool>(set.canvasEntity_).getActiveMask();
        int active = 0;
        for (int i = 0; i < set.numVoxels_; ++i) {
            const std::size_t index = set.voxelStartIdx_ + static_cast<std::size_t>(i);
            active += (mask[index / IRComponents::kVoxelActiveMaskBits] >>
                       (index % IRComponents::kVoxelActiveMaskBits)) &
                      1u;
        }
        return active;
    }

    static void moveTo(IREntity::EntityId entity, IRMath::vec3 position) {
        IREntity::getComponent<C_WorldTransform>(entity).translation_ = position;
    }

    static std::uint32_t bodyFactor(IREntity::EntityId entity) {
        const auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        auto &pool = IREntity::getComponent<C_VoxelPool>(set.canvasEntity_);
        return (IRPrefab::Fog::bodyCarrierBits(pool, set) &
                IRComponents::VoxelReserved::kFogBodyFactorMask) >>
               IRComponents::VoxelReserved::kFogBodyFactorShift;
    }

    void adoptHidden(IREntity::EntityId body) {
        runFrame();
        ASSERT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(body).has_value());
        ASSERT_FALSE(IREntity::getComponent<C_FogRevealed>(body).shown_);
        ASSERT_FALSE(IREntity::getComponent<C_VoxelSetNew>(body).visible_);
        ASSERT_EQ(activeBits(body), 0);
    }

    void setLongStagger() {
        IREntity::singleton<C_FogRevealSettings>().staggerPeriod_ = 1000;
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_worldCanvas = IREntity::kNullEntity;
};

TEST_F(FogModeSwitchTest, HiddenWorldBodyRepairsItsPrivatePoolAndCanvasOnPromotion) {
    const IREntity::EntityId body = createWorldBody();
    adoptHidden(body);
    const IREntity::EntityId privateCanvas = createPrivateCanvas();
    promoteByHand(body, privateCanvas);
    ASSERT_EQ(activeBits(body), 0);
    ASSERT_FALSE(IREntity::getComponent<C_EntityCanvas>(body).fogHidden_);

    runFrame();

    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(body).visible_);
    EXPECT_EQ(activeBits(body), kSetVoxels);
    EXPECT_TRUE(IREntity::getComponent<C_EntityCanvas>(body).fogHidden_);

    moveTo(body, IRMath::vec3(0.0f));
    runFrame();

    EXPECT_FALSE(IREntity::getComponent<C_EntityCanvas>(body).fogHidden_);
    EXPECT_EQ(activeBits(body), kSetVoxels);
}

TEST_F(FogModeSwitchTest, HiddenDetachedOwnerRepairsItsWorldGateOnDemotion) {
    const IREntity::EntityId privateCanvas = createPrivateCanvas();
    const IREntity::EntityId owner = IREntity::createEntity(
        C_WorldTransform{kHiddenPosition, IRMath::vec4(0, 0, 0, 1), IRMath::vec3(1.0f)},
        C_VoxelSetNew{kSetSize, kSetColor, true, privateCanvas},
        C_EntityCanvas{privateCanvas, IRMath::ivec2(64)},
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
        C_FogRevealed{1.0f, true}
    );

    runFrame();
    ASSERT_FALSE(IREntity::getComponent<C_FogRevealed>(owner).shown_);
    ASSERT_TRUE(IREntity::getComponent<C_EntityCanvas>(owner).fogHidden_);
    ASSERT_TRUE(IREntity::getComponent<C_VoxelSetNew>(owner).visible_);

    IRPrefab::RotationMode::setMode(owner, RotationMode::GRID);
    runFrame();

    EXPECT_FALSE(IREntity::getComponent<C_FogRevealed>(owner).shown_);
    EXPECT_FALSE(IREntity::getComponent<C_VoxelSetNew>(owner).visible_);
    EXPECT_EQ(activeBits(owner), 0);
}

TEST_F(FogModeSwitchTest, ShownPromotedBodyRepairsItsWorldGateOnDemotion) {
    const IREntity::EntityId body = createWorldBody();
    adoptHidden(body);
    const IREntity::EntityId privateCanvas = createPrivateCanvas();
    promoteByHand(body, privateCanvas);
    moveTo(body, IRMath::vec3(0.0f));
    runFrame();
    ASSERT_TRUE(IREntity::getComponent<C_FogRevealed>(body).shown_);

    IRPrefab::RotationMode::setMode(body, RotationMode::GRID);
    runFrame();

    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(body).visible_);
    EXPECT_EQ(activeBits(body), kSetVoxels);
    EXPECT_EQ(bodyFactor(body), 255u);
}

TEST_F(FogModeSwitchTest, HostedPartRepairsItsGateInTheHostAndAfterRelease) {
    const IREntity::EntityId part = createWorldBody();
    adoptHidden(part);
    const IREntity::EntityId privateCanvas = createPrivateCanvas();
    const IREntity::EntityId host = IREntity::createEntity(
        C_WorldTransform{},
        C_EntityCanvas{privateCanvas, IRMath::ivec2(64)},
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
        C_FogRevealed{1.0f, true}
    );

    ASSERT_TRUE(IRPrefab::CanvasPart::attach(part, host));
    ASSERT_EQ(activeBits(part), 0);
    runFrame();

    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(part).visible_);
    EXPECT_EQ(activeBits(part), kSetVoxels);

    IRPrefab::RotationMode::setMode(host, RotationMode::GRID);
    runFrame();

    EXPECT_FALSE(IREntity::getComponent<C_VoxelSetNew>(part).visible_);
    EXPECT_EQ(activeBits(part), 0);
}

TEST_F(FogModeSwitchTest, GateMismatchesBypassTheStagger) {
    setLongStagger();
    const IREntity::EntityId promoted = createWorldBody();
    adoptHidden(promoted);
    const IREntity::EntityId promotedCanvas = createPrivateCanvas();
    promoteByHand(promoted, promotedCanvas);

    const IREntity::EntityId hiddenCanvas = createPrivateCanvas();
    const IREntity::EntityId demoted = IREntity::createEntity(
        C_WorldTransform{kHiddenPosition, IRMath::vec4(0, 0, 0, 1), IRMath::vec3(1.0f)},
        C_VoxelSetNew{kSetSize, kSetColor, true, hiddenCanvas},
        C_EntityCanvas{hiddenCanvas, IRMath::ivec2(64)},
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
        C_FogRevealed{0.0f, false}
    );
    IREntity::getComponent<C_EntityCanvas>(demoted).fogHidden_ = true;
    IRPrefab::RotationMode::setMode(demoted, RotationMode::GRID);

    runFrame();

    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(promoted).visible_);
    EXPECT_EQ(activeBits(promoted), kSetVoxels);
    EXPECT_TRUE(IREntity::getComponent<C_EntityCanvas>(promoted).fogHidden_);
    EXPECT_FALSE(IREntity::getComponent<C_VoxelSetNew>(demoted).visible_);
    EXPECT_EQ(activeBits(demoted), 0);
}

TEST_F(FogModeSwitchTest, MatchingWorldGateKeepsTheStaggerAndSameCanvasReseedStaysHidden) {
    setLongStagger();
    const IREntity::EntityId reseeded = createWorldBody();
    adoptHidden(reseeded);
    auto &reseededSet = IREntity::getComponent<C_VoxelSetNew>(reseeded);
    IRPrefab::VoxelPool::restageSet(reseededSet);
    ASSERT_TRUE(reseededSet.attachToCanvas(m_worldCanvas));
    ASSERT_EQ(activeBits(reseeded), 0);

    const IREntity::EntityId skipped = createWorldBody();
    auto &skippedSet = IREntity::getComponent<C_VoxelSetNew>(skipped);
    skippedSet.visible_ = false;
    IREntity::getComponent<C_VoxelPool>(m_worldCanvas)
        .clearActiveMaskRange(skippedSet.voxelStartIdx_, skippedSet.numVoxels_);
    IREntity::setComponent(skipped, C_FogRevealed{0.25f, false});

    runFrame();

    EXPECT_FALSE(IREntity::getComponent<C_VoxelSetNew>(reseeded).visible_);
    EXPECT_EQ(activeBits(reseeded), 0);
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_FogRevealed>(skipped).revealFactor_, 0.25f);
    EXPECT_FALSE(IREntity::getComponent<C_VoxelSetNew>(skipped).visible_);
    EXPECT_EQ(activeBits(skipped), 0);
}

} // namespace
