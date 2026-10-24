#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_rhythmic_launch.hpp>
#include <irreden/update/components/component_spring_platform.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/systems/system_acceleration.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/update/systems/system_rhythmic_launch.hpp>
#include <irreden/update/systems/system_spring_platform.hpp>
#include <irreden/update/systems/system_velocity.hpp>

#include <cstdint>

// A block built resting on its platform: grounded from the start and born
// touching, so its first overlap is a stay (no landing, no contact enter for
// a note or a spring to key on) and the grounded snap adopts the platform it
// touches, holding the block against gravity until its launch. The controls
// pin both halves: a default contact event enters on the first tick, and a
// grounded block touching nothing is not held.

namespace {

using IRComponents::C_Acceleration3D;
using IRComponents::C_ColliderIso3DAABB;
using IRComponents::C_CollisionLayer;
using IRComponents::C_ContactEvent;
using IRComponents::C_LocalTransform;
using IRComponents::C_RhythmicLaunch;
using IRComponents::C_SpringPlatform;
using IRComponents::C_Velocity3D;
using IRComponents::C_WorldTransform;
using IRComponents::SPRING_IDLE;
using IRComponents::SPRING_LOCKED;
using IREntity::EntityId;
using IRMath::ColorHSV;
using IRMath::vec3;

constexpr std::uint32_t kBlock = IRComponents::COLLISION_LAYER_NOTE_BLOCK;
constexpr std::uint32_t kPlatform = IRComponents::COLLISION_LAYER_NOTE_PLATFORM;
constexpr float kGravity = 60.0f;
// A 4-voxel block whose box spans [z - 4, z] (+z is down) rests with its
// bottom one voxel into the platform's top face at z = 0, so its translation
// is z = 1; the snap writes `platform z - restOffsetZ` with the platform's
// translation at its centre, z = 0.5.
constexpr float kContactDepth = 1.0f;
constexpr float kPlatformCenterZ = 0.5f;
constexpr float kRestZ = kContactDepth;
constexpr float kRestOffsetZ = kPlatformCenterZ - kRestZ;
constexpr float kLongPeriod = 1000.0f;
constexpr vec3 kImpulse = vec3(0.0f, 0.0f, -40.0f);

class RhythmicLaunchRestTest : public testing::Test {
  protected:
    RhythmicLaunchRestTest()
        : m_time_manager{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::COLLISION_EVENT_CLEAR>(),
             IRSystem::createSystem<IRSystem::ACCELERATION_3D>(),
             IRSystem::createSystem<IRSystem::VELOCITY_3D>(),
             IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
             IRSystem::createSystem<IRSystem::COLLISION_NOTE_PLATFORM>(),
             IRSystem::createSystem<IRSystem::SPRING_PLATFORM>(),
             IRSystem::createSystem<IRSystem::RHYTHMIC_LAUNCH>()}
        );
    }

    void tick(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            m_system_manager.executePipeline(IRTime::Events::UPDATE);
        }
    }

    static const C_ContactEvent &contact(EntityId entity) {
        return IREntity::getComponent<C_ContactEvent>(entity);
    }

    static const C_RhythmicLaunch &launch(EntityId entity) {
        return IREntity::getComponent<C_RhythmicLaunch>(entity);
    }

    static float z(EntityId entity) {
        return IREntity::getComponent<C_LocalTransform>(entity).translation_.z;
    }

    // A 5 x 5 x 1 platform whose top face is z = 0, static.
    static EntityId makePlatform() {
        return IREntity::createEntity(
            C_LocalTransform{vec3(2.5f, 2.5f, kPlatformCenterZ)},
            C_ColliderIso3DAABB{vec3(2.5f, 2.5f, 0.5f)},
            C_CollisionLayer{kPlatform, kBlock, true},
            C_ContactEvent{}
        );
    }

    // The same platform on a spring (compressing along +z, down), with the
    // given contact event, built locked or idle.
    static EntityId makeSpringPlatform(C_ContactEvent event, bool startLocked) {
        return IREntity::createEntity(
            C_LocalTransform{vec3(2.5f, 2.5f, kPlatformCenterZ)},
            C_Velocity3D{vec3(0.0f)},
            C_ColliderIso3DAABB{vec3(2.5f, 2.5f, 0.5f)},
            C_CollisionLayer{kPlatform, kBlock, true},
            event,
            C_SpringPlatform{
                200.0f,
                10.0f,
                8.0f,
                0.7f,
                0.2f,
                1.0f,
                3,
                2,
                0.5f,
                0.15f,
                vec3(0.0f, 0.0f, 1.0f),
                ColorHSV{0.0f, 0.0f, -0.3f, 0.0f},
                ColorHSV{0.0f, 0.0f, 0.0f, 0.0f},
                0.0f,
                0.0f,
                startLocked
            }
        );
    }

    static const C_SpringPlatform &spring(EntityId entity) {
        return IREntity::getComponent<C_SpringPlatform>(entity);
    }

    // A block at `z` under gravity, grounded or airborne from the build, with
    // the given contact event.
    static EntityId makeBlock(float z, bool startGrounded, C_ContactEvent event, float period) {
        return IREntity::createEntity(
            C_LocalTransform{vec3(0.5f, 0.5f, z)},
            C_Velocity3D{vec3(0.0f)},
            C_Acceleration3D{0.0f, 0.0f, kGravity},
            C_ColliderIso3DAABB{vec3(2.0f), vec3(2.0f, 2.0f, -2.0f)},
            C_CollisionLayer{kBlock, kPlatform, true},
            event,
            C_RhythmicLaunch{period, kImpulse, kRestOffsetZ, 0.0f, startGrounded}
        );
    }

    IRTime::TimeManager m_time_manager;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(RhythmicLaunchRestTest, BornTouchingBlockRestsOnItsPlatformWithoutEntering) {
    const EntityId platform = makePlatform();
    const EntityId block = makeBlock(kRestZ, true, C_ContactEvent{true}, kLongPeriod);

    tick();
    EXPECT_FALSE(contact(block).entered_);
    EXPECT_TRUE(contact(block).stayed_);
    EXPECT_TRUE(contact(block).touching_);
    EXPECT_EQ(launch(block).lastPlatformEntity_, platform);
    EXPECT_TRUE(launch(block).grounded_);
    EXPECT_FLOAT_EQ(z(block), kRestZ);

    for (int i = 0; i < 120; ++i) {
        tick();
        ASSERT_FLOAT_EQ(z(block), kRestZ) << "moved on tick " << i + 2;
        ASSERT_FALSE(contact(block).entered_) << "entered on tick " << i + 2;
    }
    EXPECT_EQ(launch(block).launchCount_, 0);
    EXPECT_EQ(IREntity::getComponent<C_Velocity3D>(block).velocity_, vec3(0.0f));
}

TEST_F(RhythmicLaunchRestTest, RestingBlockLaunchesAtItsPeriod) {
    makePlatform();
    const float dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
    const float period = 10.0f * dt;
    const EntityId block = makeBlock(kRestZ, true, C_ContactEvent{true}, period);

    tick(9);
    EXPECT_EQ(launch(block).launchCount_, 0);
    EXPECT_FLOAT_EQ(z(block), kRestZ);
    tick();
    EXPECT_EQ(launch(block).launchCount_, 1);
    EXPECT_FALSE(launch(block).grounded_);
    EXPECT_EQ(IREntity::getComponent<C_Velocity3D>(block).velocity_, kImpulse);
}

TEST_F(RhythmicLaunchRestTest, DefaultContactEventEntersOnTheFirstTick) {
    makePlatform();
    const EntityId block = makeBlock(kRestZ, true, C_ContactEvent{}, kLongPeriod);

    tick();
    EXPECT_TRUE(contact(block).entered_);
    EXPECT_FALSE(contact(block).stayed_);
}

TEST_F(RhythmicLaunchRestTest, BornLockedPlatformUnderARestingBlockHoldsStill) {
    const EntityId platform = makeSpringPlatform(C_ContactEvent{true}, true);
    const EntityId block = makeBlock(kRestZ, true, C_ContactEvent{true}, kLongPeriod);

    for (int i = 0; i < 120; ++i) {
        tick();
        ASSERT_FLOAT_EQ(z(platform), kPlatformCenterZ) << "the platform moved on tick " << i + 1;
        ASSERT_FLOAT_EQ(z(block), kRestZ) << "the block moved on tick " << i + 1;
        ASSERT_FALSE(contact(platform).entered_) << "the platform entered on tick " << i + 1;
    }
    EXPECT_EQ(spring(platform).state_, SPRING_LOCKED);
    EXPECT_FLOAT_EQ(spring(platform).displacement_, 8.0f * 0.7f);
}

TEST_F(RhythmicLaunchRestTest, DefaultPlatformCatchesTheBlockItWasBuiltUnder) {
    const EntityId platform = makeSpringPlatform(C_ContactEvent{}, false);
    makeBlock(kRestZ, true, C_ContactEvent{true}, kLongPeriod);

    tick();
    EXPECT_TRUE(contact(platform).entered_);
    EXPECT_NE(spring(platform).state_, SPRING_IDLE);
}

TEST_F(RhythmicLaunchRestTest, GroundedBlockTouchingNothingIsNotHeld) {
    makePlatform();
    const float apex = kRestZ - 20.0f;
    const EntityId block = makeBlock(apex, true, C_ContactEvent{true}, kLongPeriod);

    tick(10);
    EXPECT_EQ(launch(block).lastPlatformEntity_, IREntity::kNullEntity);
    EXPECT_GT(z(block), apex);
}

} // namespace
