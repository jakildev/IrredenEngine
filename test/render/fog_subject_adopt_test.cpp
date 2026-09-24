#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_reveal_systems.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstdint>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_FogExempt;
using IRComponents::C_FogField;
using IRComponents::C_FogRevealed;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;
using IRMath::vec4;
using IRPrefab::Fog::FogSubjectClass;

constexpr std::uint32_t kBodyBit = IRComponents::VoxelReserved::kFogBody;
constexpr int kFactorShift = IRComponents::VoxelReserved::kFogBodyFactorShift;
constexpr std::uint32_t kCarrierMask = IRComponents::VoxelReserved::kFogCarrierMask;
constexpr int kSetVoxels = 8;

std::uint32_t bodyCarrier(std::uint8_t factor) {
    return kBodyBit | (static_cast<std::uint32_t>(factor) << kFactorShift);
}

// Headless: no render manager, so the canvas is named through the headless
// active-canvas seam and the fog component is the textureless form.
class FogSubjectAdoptTest : public testing::Test {
  protected:
    FogSubjectAdoptTest()
        : m_entityManager{}
        , m_systemManager{} {
        m_canvas = IREntity::createEntity(
            C_VoxelPool{ivec3(8, 8, 8)},
            C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}}
        );
        IRRender::setHeadlessActiveCanvasEntity(m_canvas);
        m_exemptId = IRSystem::createSystem<IRSystem::FOG_SUBJECT_EXEMPT>();
        m_adoptId = IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT>();
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, {m_exemptId, m_adoptId});
    }
    ~FogSubjectAdoptTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    IRSystem::System<IRSystem::FOG_SUBJECT_ADOPT> *adoptSystem() {
        return m_systemManager.getSystemParams<IRSystem::System<IRSystem::FOG_SUBJECT_ADOPT>>(
            m_adoptId
        );
    }
    IRSystem::System<IRSystem::FOG_SUBJECT_EXEMPT> *exemptSystem() {
        return m_systemManager.getSystemParams<IRSystem::System<IRSystem::FOG_SUBJECT_EXEMPT>>(
            m_exemptId
        );
    }

    C_CanvasFogOfWar &fog() {
        return IREntity::getComponent<C_CanvasFogOfWar>(m_canvas);
    }
    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(m_canvas);
    }

    template <typename... Tags> IREntity::EntityId createSet(vec3 position, Tags... tags) {
        return IREntity::createEntity(
            C_WorldTransform{position, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
            C_VoxelSetNew{ivec3(2, 2, 2), Color{200, 100, 50, 255}, true, m_canvas},
            tags...
        );
    }

    // One UPDATE pass plus the structural flush a deferred component lands
    // on.
    void runFrame() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        IREntity::flushStructuralChanges();
    }

    static bool hasRevealed(IREntity::EntityId entity) {
        return IREntity::getComponentOptional<C_FogRevealed>(entity).has_value();
    }

    // Reads the set's records through its canvas pool by index, the way the
    // reveal systems do (a canvas migration relocates the pool storage the
    // set's cached span points into).
    std::uint32_t carrierOf(IREntity::EntityId entity) {
        const auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        auto &canvasPool = IREntity::getComponent<C_VoxelPool>(set.canvasEntity_);
        const auto records = IRPrefab::Fog::poolRecords(canvasPool, set);
        EXPECT_EQ(records.size(), static_cast<std::size_t>(set.numVoxels_));
        std::uint32_t carrier = records[0].reserved_ & kCarrierMask;
        for (const IRComponents::C_Voxel &voxel : records) {
            EXPECT_EQ(voxel.reserved_ & kCarrierMask, carrier) << "every voxel carries the stamp";
        }
        return carrier;
    }

    int activeBitsOf(IREntity::EntityId entity) {
        const auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        const auto &mask = pool().getActiveMask();
        int active = 0;
        for (int i = 0; i < set.numVoxels_; ++i) {
            const std::size_t idx = set.voxelStartIdx_ + static_cast<std::size_t>(i);
            active += (mask[idx / 32] >> (idx % 32)) & 1u;
        }
        return active;
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
    IRSystem::SystemId m_exemptId{};
    IRSystem::SystemId m_adoptId{};
};

TEST_F(FogSubjectAdoptTest, UntaggedSetOnAVisibleCellIsAShownBodyAfterOneFrame) {
    fog().setCell(0, 0, IRComponents::kFogStateVisible);
    const IREntity::EntityId body = createSet(vec3(0.0f, 0.0f, 0.0f));
    auto &set = IREntity::getComponent<C_VoxelSetNew>(body);
    const auto records = IRPrefab::Fog::poolRecords(pool(), set);
    set.rotationSourceVoxels_.assign(records.begin(), records.end());
    ASSERT_FALSE(hasRevealed(body));
    ASSERT_EQ(carrierOf(body), 0u);

    runFrame();

    ASSERT_TRUE(hasRevealed(body)) << "C_FogRevealed lands at the structural flush";
    const auto &revealed = IREntity::getComponent<C_FogRevealed>(body);
    EXPECT_FLOAT_EQ(revealed.revealFactor_, 1.0f);
    EXPECT_TRUE(revealed.shown_);
    EXPECT_EQ(carrierOf(body), bodyCarrier(255));
    for (const IRComponents::C_Voxel &voxel :
         IREntity::getComponent<C_VoxelSetNew>(body).rotationSourceVoxels_) {
        EXPECT_EQ(voxel.reserved_ & kCarrierMask, bodyCarrier(255)) << "the mirror is stamped too";
    }
    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(body).visible_);
    EXPECT_EQ(activeBitsOf(body), kSetVoxels);
}

TEST_F(FogSubjectAdoptTest, UntaggedSetOnAnUnexploredCellIsAHiddenBodyWithItsMaskCleared) {
    const IREntity::EntityId body = createSet(vec3(40.0f, 40.0f, 0.0f));
    ASSERT_EQ(activeBitsOf(body), kSetVoxels);

    runFrame();

    ASSERT_TRUE(hasRevealed(body));
    const auto &revealed = IREntity::getComponent<C_FogRevealed>(body);
    EXPECT_FLOAT_EQ(revealed.revealFactor_, 0.0f);
    EXPECT_FALSE(revealed.shown_);
    EXPECT_EQ(carrierOf(body), bodyCarrier(0));
    EXPECT_FALSE(IREntity::getComponent<C_VoxelSetNew>(body).visible_);
    EXPECT_EQ(activeBitsOf(body), 0);
}

TEST_F(FogSubjectAdoptTest, AdoptionRunsOnceAndEvalOwnsTheBodyAfterwards) {
    const IREntity::EntityId body = createSet(vec3(40.0f, 40.0f, 0.0f));
    runFrame();
    ASSERT_TRUE(hasRevealed(body));
    IREntity::getComponent<C_FogRevealed>(body).revealFactor_ = 0.25f;

    runFrame();

    EXPECT_FLOAT_EQ(IREntity::getComponent<C_FogRevealed>(body).revealFactor_, 0.25f)
        << "an adopted body is outside the adopt query (Exclude<C_FogRevealed>)";
}

TEST_F(FogSubjectAdoptTest, FieldTaggedSetIsNeverVisited) {
    fog().setCell(0, 0, IRComponents::kFogStateVisible);
    const IREntity::EntityId field = createSet(vec3(0.0f, 0.0f, 0.0f), C_FogField{});
    const IREntity::EntityId twin = createSet(vec3(0.0f, 0.0f, 0.0f));

    runFrame();

    EXPECT_FALSE(hasRevealed(field));
    EXPECT_EQ(carrierOf(field), 0u);
    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(field).visible_);
    EXPECT_EQ(IRPrefab::Fog::subjectClass(field), FogSubjectClass::FIELD);
    EXPECT_TRUE(hasRevealed(twin)) << "the untagged twin is the control";
}

TEST_F(FogSubjectAdoptTest, ConstructionTimeExemptMarkerIsRealizedAt255WithoutBodyState) {
    const IREntity::EntityId exempt = createSet(vec3(40.0f, 40.0f, 0.0f), C_FogExempt{});
    const IREntity::EntityId twin = createSet(vec3(40.0f, 40.0f, 0.0f));
    auto &set = IREntity::getComponent<C_VoxelSetNew>(exempt);
    const auto records = IRPrefab::Fog::poolRecords(pool(), set);
    set.rotationSourceVoxels_.assign(records.begin(), records.end());

    runFrame();

    EXPECT_FALSE(hasRevealed(exempt));
    EXPECT_EQ(carrierOf(exempt), bodyCarrier(255));
    for (const IRComponents::C_Voxel &voxel :
         IREntity::getComponent<C_VoxelSetNew>(exempt).rotationSourceVoxels_) {
        EXPECT_EQ(voxel.reserved_ & kCarrierMask, bodyCarrier(255));
    }
    EXPECT_EQ(activeBitsOf(exempt), kSetVoxels) << "an EXEMPT set is never hidden";
    EXPECT_EQ(IRPrefab::Fog::subjectClass(exempt), FogSubjectClass::EXEMPT);
    EXPECT_TRUE(hasRevealed(twin)) << "the untagged twin is adopted and hidden";
    EXPECT_EQ(activeBitsOf(twin), 0);
}

TEST_F(FogSubjectAdoptTest, SetterExemptStampsSynchronouslyAndStaysUnadopted) {
    const IREntity::EntityId exempt = createSet(vec3(40.0f, 40.0f, 0.0f));
    const IREntity::EntityId twin = createSet(vec3(40.0f, 40.0f, 0.0f));

    IRPrefab::Fog::setSubjectClass(exempt, FogSubjectClass::EXEMPT);
    EXPECT_EQ(carrierOf(exempt), bodyCarrier(255));
    EXPECT_EQ(IRPrefab::Fog::subjectClass(exempt), FogSubjectClass::EXEMPT);

    runFrame();

    EXPECT_FALSE(hasRevealed(exempt));
    EXPECT_EQ(carrierOf(exempt), bodyCarrier(255));
    EXPECT_TRUE(hasRevealed(twin));
}

TEST_F(FogSubjectAdoptTest, ReclassifyingAHiddenBodyToFieldClearsItsCarrierAndRestoresItsMask) {
    const IREntity::EntityId body = createSet(vec3(40.0f, 40.0f, 0.0f));
    runFrame();
    ASSERT_TRUE(hasRevealed(body));
    ASSERT_EQ(activeBitsOf(body), 0);

    IRPrefab::Fog::setSubjectClass(body, FogSubjectClass::FIELD);

    EXPECT_FALSE(hasRevealed(body));
    EXPECT_EQ(carrierOf(body), 0u);
    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(body).visible_);
    EXPECT_EQ(activeBitsOf(body), kSetVoxels);
    EXPECT_EQ(IRPrefab::Fog::subjectClass(body), FogSubjectClass::FIELD);

    runFrame();
    EXPECT_FALSE(hasRevealed(body)) << "a FIELD set is not re-adopted";
}

TEST_F(FogSubjectAdoptTest, SynchronousBodyAdoptionThroughTheSetterStartsHidden) {
    fog().setCell(0, 0, IRComponents::kFogStateVisible);
    const IREntity::EntityId body = createSet(vec3(0.0f, 0.0f, 0.0f));

    IRPrefab::Fog::setEntityRevealGoverned(body);

    EXPECT_TRUE(hasRevealed(body));
    EXPECT_EQ(carrierOf(body), bodyCarrier(0));
    EXPECT_EQ(activeBitsOf(body), 0) << "governed starts hidden until its first eval";
    EXPECT_EQ(IRPrefab::Fog::subjectClass(body), FogSubjectClass::BODY);
}

TEST_F(FogSubjectAdoptTest, SetsOffTheFogCanvasAreLeftAlone) {
    const IREntity::EntityId otherCanvas = IREntity::createEntity(C_VoxelPool{ivec3(4, 4, 4)});
    const IREntity::EntityId elsewhere = IREntity::createEntity(
        C_WorldTransform{vec3(0.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
        C_VoxelSetNew{ivec3(2, 2, 2), Color{200, 100, 50, 255}, true, otherCanvas}
    );

    runFrame();

    EXPECT_FALSE(hasRevealed(elsewhere));
    EXPECT_EQ(carrierOf(elsewhere), 0u);
}

TEST_F(FogSubjectAdoptTest, NothingIsAdoptedWhileTheCanvasCarriesNoFog) {
    IREntity::removeComponent<C_CanvasFogOfWar>(m_canvas);
    const IREntity::EntityId body = createSet(vec3(0.0f, 0.0f, 0.0f));
    const IREntity::EntityId exempt = createSet(vec3(0.0f, 0.0f, 0.0f), C_FogExempt{});

    runFrame();

    EXPECT_FALSE(hasRevealed(body));
    EXPECT_EQ(carrierOf(body), 0u);
    EXPECT_EQ(carrierOf(exempt), 0u);
}

} // namespace
