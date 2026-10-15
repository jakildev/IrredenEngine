#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/update/systems/system_velocity.hpp>

// COLLISION_NOTE_PLATFORM sweeps each pair over the frame: a collider with a
// C_Velocity3D is tested over its world displacement since the system's last
// tick, and the pair touches if the boxes met at any moment in between.
// Covered twice: the pure slab test (`IRMath::sweptAabbOverlap`) and the
// system end to end behind VELOCITY_3D and PROPAGATE_TRANSFORM, where a block
// fast enough to clear a one-voxel platform between two ticks still lands on
// it.

namespace {

using IRComponents::C_ColliderIso3DAABB;
using IRComponents::C_CollisionLayer;
using IRComponents::C_ContactEvent;
using IRComponents::C_LocalTransform;
using IRComponents::C_Velocity3D;
using IRComponents::C_WorldTransform;
using IRMath::sweptAabbOverlap;
using IRMath::vec3;
using IRMath::vec4;

// ---- Pure slab test ----------------------------------------------------------

TEST(SweptAabbOverlap, ZeroMotionIsTheStaticTest) {
    EXPECT_TRUE(sweptAabbOverlap(vec3(0.0f), vec3(1.0f), vec3(1.0f), vec3(2.0f), vec3(0.0f)));
    EXPECT_FALSE(sweptAabbOverlap(vec3(0.0f), vec3(1.0f), vec3(1.5f), vec3(2.0f), vec3(0.0f)));
}

TEST(SweptAabbOverlap, PassingClearThroughAThinBoxTouches) {
    // A unit box ends 5 below a 1-thick slab (+Z down) after moving 10 down:
    // it started 4 above it, so it crossed the slab mid-frame.
    const vec3 minA(0.0f, 0.0f, 6.0f);
    const vec3 maxA(1.0f, 1.0f, 7.0f);
    const vec3 slabMin(-2.0f, -2.0f, 0.0f);
    const vec3 slabMax(3.0f, 3.0f, 1.0f);
    EXPECT_FALSE(sweptAabbOverlap(minA, maxA, slabMin, slabMax, vec3(0.0f)));
    EXPECT_TRUE(sweptAabbOverlap(minA, maxA, slabMin, slabMax, vec3(0.0f, 0.0f, 10.0f)));
}

TEST(SweptAabbOverlap, StoppingShortDoesNotTouch) {
    // Moved 2 down and ends 1 above the slab: it never reached it.
    EXPECT_FALSE(sweptAabbOverlap(
        vec3(0.0f, 0.0f, -2.0f),
        vec3(1.0f, 1.0f, -1.0f),
        vec3(-2.0f, -2.0f, 0.0f),
        vec3(3.0f, 3.0f, 1.0f),
        vec3(0.0f, 0.0f, 2.0f)
    ));
}

TEST(SweptAabbOverlap, DiagonalPathBesideTheBoxDoesNotTouch) {
    // From x [0,1], z [0,1] to x [4,5], z [4,5]; the target sits at x [0,1],
    // z [4,5]. The swept bounding box covers it, but X has left it (t > 0.25)
    // before Z reaches it (t >= 0.75).
    EXPECT_FALSE(sweptAabbOverlap(
        vec3(4.0f, 0.0f, 4.0f),
        vec3(5.0f, 1.0f, 5.0f),
        vec3(0.0f, 0.0f, 4.0f),
        vec3(1.0f, 1.0f, 5.0f),
        vec3(4.0f, 0.0f, 4.0f)
    ));
}

// ---- The system end to end ---------------------------------------------------

constexpr std::uint32_t kBlock = IRComponents::COLLISION_LAYER_NOTE_BLOCK;
constexpr std::uint32_t kPlatform = IRComponents::COLLISION_LAYER_NOTE_PLATFORM;

class CollisionSweptTest : public testing::Test {
  protected:
    CollisionSweptTest()
        : m_time_manager{}
        , m_entity_manager{}
        , m_system_manager{}
        , m_propagate{IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
        , m_collision_clear{IRSystem::createSystem<IRSystem::COLLISION_EVENT_CLEAR>()}
        , m_collision{IRSystem::createSystem<IRSystem::COLLISION_NOTE_PLATFORM>()} {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::VELOCITY_3D>(),
             m_propagate,
             m_collision_clear,
             m_collision}
        );
        m_dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    static const C_ContactEvent &contact(IREntity::EntityId entity) {
        return IREntity::getComponent<C_ContactEvent>(entity);
    }

    // A static platform box: `halfExtents` around `center`.
    static IREntity::EntityId makePlatform(vec3 center, vec3 halfExtents) {
        return IREntity::createEntity(
            C_LocalTransform{center},
            C_ColliderIso3DAABB{halfExtents},
            C_CollisionLayer{kPlatform, kBlock, true},
            C_ContactEvent{}
        );
    }

    // A 5 x 5 x 1 platform whose top face is z = 0 (+Z down).
    static IREntity::EntityId makePlatform() {
        return makePlatform(vec3(2.5f, 2.5f, 0.5f), vec3(2.5f, 2.5f, 0.5f));
    }

    // A 4-voxel block whose box spans [z - 4, z], falling at `speed` voxels a
    // second from `z`.
    static IREntity::EntityId makeFallingBlock(float z, float speed) {
        return IREntity::createEntity(
            C_LocalTransform{vec3(0.5f, 0.5f, z)},
            C_ColliderIso3DAABB{vec3(2.0f), vec3(2.0f, 2.0f, -2.0f)},
            C_CollisionLayer{kBlock, kPlatform, true},
            C_ContactEvent{},
            C_Velocity3D{vec3(0.0f, 0.0f, speed)}
        );
    }

    IRTime::TimeManager m_time_manager;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRSystem::SystemId m_propagate;
    IRSystem::SystemId m_collision_clear;
    IRSystem::SystemId m_collision;
    float m_dt = 0.0f;
};

TEST_F(CollisionSweptTest, FastBlockLandsInsteadOfPassingThrough) {
    const auto platform = makePlatform();
    // 600 voxels/s moves 10 a frame at 60 Hz. The first tick ends with the
    // block 3 above the platform's top face; the second ends with it 2 below
    // the bottom face, so only a sweep sees the crossing.
    const float step = 600.0f * m_dt;
    ASSERT_GT(step, 7.0f) << "the first tick must end above the platform";
    const auto block = makeFallingBlock(7.0f - 2.0f * step, 600.0f);
    tick();
    EXPECT_FALSE(contact(block).touching_);
    tick();
    EXPECT_TRUE(contact(block).entered_);
    EXPECT_EQ(contact(block).otherEntity_, platform);
    EXPECT_TRUE(contact(platform).entered_);
}

TEST_F(CollisionSweptTest, SlowBlockAboveThePlatformDoesNotTouch) {
    makePlatform();
    // 30 voxels/s: half a voxel a frame, ending 2 above the top face.
    const auto block = makeFallingBlock(-3.0f, 30.0f);
    tick();
    tick();
    EXPECT_FALSE(contact(block).touching_);
}

TEST_F(CollisionSweptTest, StaticCollidersKeepTheOverlapTest) {
    const auto platform = makePlatform();
    const auto resting = IREntity::createEntity(
        C_LocalTransform{vec3(0.5f, 0.5f, 0.0f)},
        C_ColliderIso3DAABB{vec3(2.0f), vec3(2.0f, 2.0f, -2.0f)},
        C_CollisionLayer{kBlock, kPlatform, true},
        C_ContactEvent{}
    );
    tick();
    EXPECT_TRUE(contact(resting).touching_);
    EXPECT_EQ(contact(resting).otherEntity_, platform);
}

TEST_F(CollisionSweptTest, SweepFollowsTheWorldMotionUnderARotatedScaledParent) {
    // The parent turns local +X onto world +Y and doubles distances, so the
    // child's local velocity of 300 along X moves it 2 * 300 * dt along world
    // Y. One wall crosses that world path; the other sits on the path the
    // local velocity would trace if read as world motion.
    const float worldStep = 2.0f * 300.0f * m_dt;
    ASSERT_GT(worldStep, 7.5f) << "the first tick must end short of the crossed wall";
    const vec4
        quarterTurnZ(0.0f, 0.0f, IRMath::sin(IRMath::kPi / 4.0f), IRMath::cos(IRMath::kPi / 4.0f));
    const auto parent =
        IREntity::createEntity(C_LocalTransform{vec3(0.0f), quarterTurnZ, vec3(2.0f)});
    const float startY = 7.0f - 2.0f * worldStep;
    const auto child = IREntity::createEntity(
        C_LocalTransform{vec3(startY / 2.0f, 0.0f, 0.0f)},
        C_ColliderIso3DAABB{vec3(0.5f)},
        C_CollisionLayer{kBlock, kPlatform, true},
        C_ContactEvent{},
        C_Velocity3D{vec3(300.0f, 0.0f, 0.0f)}
    );
    IREntity::setParent(child, parent);
    const auto crossedWall = makePlatform(vec3(0.0f, 0.5f, 0.0f), vec3(5.0f, 0.5f, 5.0f));
    const auto besidePathWall = makePlatform(vec3(-2.5f, 7.0f, 0.0f), vec3(0.5f, 1.0f, 1.0f));

    tick();
    const vec3 afterFirst = IREntity::getComponent<C_WorldTransform>(child).translation_;
    EXPECT_NEAR(afterFirst.x, 0.0f, 1e-3f);
    EXPECT_NEAR(afterFirst.y, 7.0f - worldStep, 1e-3f);
    EXPECT_FALSE(contact(child).touching_);

    tick();
    EXPECT_TRUE(contact(child).entered_);
    EXPECT_EQ(contact(child).otherEntity_, crossedWall);
    EXPECT_TRUE(contact(crossedWall).touching_);
    EXPECT_FALSE(contact(besidePathWall).touching_);
}

TEST_F(CollisionSweptTest, ColliderIsNotSweptBeforeTheSystemHasSeenIt) {
    // Nothing integrates the velocity: the block appears past the platform
    // already, and its velocity is no evidence it crossed.
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {m_propagate, m_collision_clear, m_collision}
    );
    makePlatform();
    const auto block = makeFallingBlock(1.0f + 4.0f + 3.0f, 600.0f);
    tick();
    EXPECT_FALSE(contact(block).touching_);
    tick();
    EXPECT_FALSE(contact(block).touching_);
}

} // namespace
