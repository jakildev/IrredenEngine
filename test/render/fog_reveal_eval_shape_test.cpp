#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/cursor_pivot.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/gizmo.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_shape.hpp>
#include <irreden/render/systems/system_fog_subject_adopt_shape.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_FogExempt;
using IRComponents::C_FogField;
using IRComponents::C_FogRevealed;
using IRComponents::C_ShapeDescriptor;
using IRComponents::C_WorldTransform;

C_CanvasFogOfWar fogWithCircle(float radius, float edge) {
    C_CanvasFogOfWar fog{C_CanvasFogOfWar::HeadlessInit{}};
    fog.observers_.visionCircles_[0] = IRMath::vec4(0.0f, 0.0f, radius, edge);
    fog.observers_.visionCircleCount_ = 1;
    return fog;
}

TEST(FogRevealEvalShapeTest, MirrorsTheSharedCurveAndHysteresis) {
    C_CanvasFogOfWar fog = fogWithCircle(10.0f, 2.0f);
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_SHAPE> system;
    system.fog_ = &fog;
    system.settings_.showThreshold_ = 0.6f;
    system.settings_.hideThreshold_ = 0.3f;

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_ShapeDescriptor shape{};

    transform.translation_ = IRMath::vec3(0.0f);
    system.tick(entity, revealed, transform, shape);
    EXPECT_FLOAT_EQ(revealed.revealFactor_, 1.0f);
    EXPECT_TRUE(revealed.shown_);
    EXPECT_EQ(shape.fogBodyFactor_, 255u);
    EXPECT_EQ(shape.flags_ & IRRender::SHAPE_FLAG_FOG_HIDDEN, 0u);

    transform.translation_ = IRMath::vec3(10.5f, 0.0f, 0.0f);
    system.tick(entity, revealed, transform, shape);
    EXPECT_TRUE(revealed.shown_);
    EXPECT_GT(revealed.revealFactor_, system.settings_.hideThreshold_);

    transform.translation_ = IRMath::vec3(12.0f, 0.0f, 0.0f);
    system.tick(entity, revealed, transform, shape);
    EXPECT_FALSE(revealed.shown_);
    EXPECT_EQ(shape.fogBodyFactor_, 0u);
    EXPECT_NE(shape.flags_ & IRRender::SHAPE_FLAG_FOG_HIDDEN, 0u);
}

TEST(FogRevealEvalShapeTest, FogVerdictNeverReenablesAuthorVisibility) {
    C_CanvasFogOfWar fog = fogWithCircle(10.0f, 0.0f);
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_SHAPE> system;
    system.fog_ = &fog;

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_ShapeDescriptor shape{};
    shape.flags_ = IRRender::SHAPE_FLAG_FOG_BODY | IRRender::SHAPE_FLAG_FOG_HIDDEN;

    system.tick(entity, revealed, transform, shape);

    EXPECT_TRUE(revealed.shown_);
    EXPECT_EQ(shape.flags_ & IRRender::SHAPE_FLAG_VISIBLE, 0u);
    EXPECT_EQ(shape.flags_ & IRRender::SHAPE_FLAG_FOG_HIDDEN, 0u);
}

TEST(FogRevealEvalShapeTest, RasterGateSkipsBothFogAndAuthorHiddenShapes) {
    IRSystem::System<IRSystem::SHAPES_TO_TRIXEL> system;
    IREntity::EntityId entity = 1;
    C_WorldTransform transform{};
    C_ShapeDescriptor shape{};

    shape.flags_ |= IRRender::SHAPE_FLAG_FOG_HIDDEN;
    system.tick(entity, shape, transform);
    EXPECT_TRUE(system.gpuShapesByCanvas_.empty());

    shape.flags_ = IRRender::SHAPE_FLAG_FOG_BODY;
    system.tick(entity, shape, transform);
    EXPECT_TRUE(system.gpuShapesByCanvas_.empty());
}

class FogRevealEvalShapeAdoptTest : public testing::Test {
  protected:
    FogRevealEvalShapeAdoptTest()
        : m_entityManager{}
        , m_systemManager{} {
        m_canvas = IREntity::createEntity(C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}});
        IRRender::setHeadlessActiveCanvasEntity(m_canvas);
        m_adoptId = IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT_SHAPE>();
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, {m_adoptId});
    }

    ~FogRevealEvalShapeAdoptTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    IREntity::EntityId createShape(IRMath::vec3 position) {
        return IREntity::createEntity(
            C_WorldTransform{position, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f), IRMath::vec3(1.0f)},
            C_ShapeDescriptor{}
        );
    }

    void runFrame() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        IREntity::flushStructuralChanges();
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
    IRSystem::SystemId m_adoptId{};
};

TEST_F(FogRevealEvalShapeAdoptTest, AdoptsAnUntaggedShapeButNotAFieldTwin) {
    const IREntity::EntityId body = createShape(IRMath::vec3(40.0f, 40.0f, 0.0f));
    const IREntity::EntityId field = createShape(IRMath::vec3(40.0f, 40.0f, 0.0f));
    IREntity::setComponent(field, C_FogField{});

    runFrame();

    ASSERT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(body).has_value());
    const auto &bodyShape = IREntity::getComponent<C_ShapeDescriptor>(body);
    EXPECT_NE(bodyShape.flags_ & IRRender::SHAPE_FLAG_FOG_BODY, 0u);
    EXPECT_NE(bodyShape.flags_ & IRRender::SHAPE_FLAG_FOG_HIDDEN, 0u);
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(field).has_value());
}

TEST_F(FogRevealEvalShapeAdoptTest, SynchronousGovernanceStartsHiddenThenEvaluates) {
    const IREntity::EntityId body = createShape(IRMath::vec3(0.0f));

    IRPrefab::Fog::setEntityRevealGoverned(body);

    ASSERT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(body).has_value());
    const auto &shape = IREntity::getComponent<C_ShapeDescriptor>(body);
    EXPECT_EQ(shape.fogBodyFactor_, 0u);
    EXPECT_NE(shape.flags_ & IRRender::SHAPE_FLAG_FOG_HIDDEN, 0u);
}

TEST_F(FogRevealEvalShapeAdoptTest, EngineOverlaysStayExemptFromAdoption) {
    const IREntity::EntityId indicator = IRPrefab::CursorPivot::createIndicator();
    const IREntity::EntityId handle = IRPrefab::Gizmo::createJointMarker();
    const IREntity::EntityId control = createShape(IRMath::vec3(0.0f));

    runFrame();

    EXPECT_TRUE(IREntity::getComponentOptional<C_FogExempt>(indicator).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<C_FogExempt>(handle).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(indicator).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(handle).has_value());
    EXPECT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(control).has_value());
}

TEST_F(FogRevealEvalShapeAdoptTest, PickingExcludesAFogHiddenShape) {
    const IREntity::EntityId visible = createShape(IRMath::vec3(0.0f));
    const IREntity::EntityId hidden = createShape(IRMath::vec3(1.0f));
    IREntity::getComponent<C_ShapeDescriptor>(hidden).flags_ |= IRRender::SHAPE_FLAG_FOG_HIDDEN;

    const auto shapes = IRPrefab::Picking::detail::gatherVisibleShapes(
        IRMath::CardinalIndex::k0,
        IREntity::kNullEntity
    );

    ASSERT_EQ(shapes.size(), 1u);
    EXPECT_EQ(shapes.front().entity_, visible);
}

} // namespace
