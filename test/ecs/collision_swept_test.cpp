#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>

// COLLISION_NOTE_PLATFORM sweeps each pair over the frame: a collider with a
// C_Velocity3D moved velocity * dt since the last tick, and the pair touches
// if the boxes met at any moment in between. Covered twice: the pure slab test
// (`IRMath::sweptAabbOverlap`) and the system end to end, where a block fast
// enough to clear a one-voxel platform between two ticks still lands on it.

namespace {

using IRComponents::C_ColliderIso3DAABB;
using IRComponents::C_CollisionLayer;
using IRComponents::C_ContactEvent;
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
        , m_system_manager{} {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::COLLISION_EVENT_CLEAR>(),
             IRSystem::createSystem<IRSystem::COLLISION_NOTE_PLATFORM>()}
        );
        m_dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    // A 5 x 5 x 1 platform whose top face is z = 0 (+Z down).
    IREntity::EntityId makePlatform() {
        return IREntity::createEntity(
            C_WorldTransform{vec3(0.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
            C_ColliderIso3DAABB{vec3(2.5f, 2.5f, 0.5f), vec3(2.5f, 2.5f, 0.5f)},
            C_CollisionLayer{kPlatform, kBlock, true},
            C_ContactEvent{}
        );
    }

    // A 4-voxel block whose box spans [z - 4, z], falling at `speed` voxels a
    // second; its transform is where VELOCITY_3D left it this frame.
    IREntity::EntityId makeFallingBlock(float z, float speed) {
        return IREntity::createEntity(
            C_WorldTransform{vec3(0.5f, 0.5f, z), vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
            C_ColliderIso3DAABB{vec3(2.0f), vec3(2.0f, 2.0f, -2.0f)},
            C_CollisionLayer{kBlock, kPlatform, true},
            C_ContactEvent{},
            C_Velocity3D{vec3(0.0f, 0.0f, speed)}
        );
    }

    IRTime::TimeManager m_time_manager;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    float m_dt = 0.0f;
};

TEST_F(CollisionSweptTest, FastBlockLandsInsteadOfPassingThrough) {
    const auto platform = makePlatform();
    // 600 voxels/s moves 10 a frame at 60 Hz: the block ends 3 voxels below
    // the platform's bottom face, having started 2 above its top.
    const float step = 600.0f * m_dt;
    const auto block = makeFallingBlock(1.0f + 4.0f + 3.0f, 600.0f);
    ASSERT_GT(step, 4.0f + 1.0f) << "the block must clear the platform within one frame";
    tick();
    EXPECT_TRUE(IREntity::getComponent<C_ContactEvent>(block).entered_);
    EXPECT_EQ(IREntity::getComponent<C_ContactEvent>(block).otherEntity_, platform);
    EXPECT_TRUE(IREntity::getComponent<C_ContactEvent>(platform).entered_);
}

TEST_F(CollisionSweptTest, SlowBlockAboveThePlatformDoesNotTouch) {
    makePlatform();
    // 30 voxels/s: half a voxel a frame, ending 2 above the top face.
    const auto block = makeFallingBlock(-2.0f, 30.0f);
    tick();
    EXPECT_FALSE(IREntity::getComponent<C_ContactEvent>(block).touching_);
}

TEST_F(CollisionSweptTest, StaticCollidersKeepTheOverlapTest) {
    const auto platform = makePlatform();
    const auto resting = IREntity::createEntity(
        C_WorldTransform{vec3(0.5f, 0.5f, 0.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
        C_ColliderIso3DAABB{vec3(2.0f), vec3(2.0f, 2.0f, -2.0f)},
        C_CollisionLayer{kBlock, kPlatform, true},
        C_ContactEvent{}
    );
    tick();
    EXPECT_TRUE(IREntity::getComponent<C_ContactEvent>(resting).touching_);
    EXPECT_EQ(IREntity::getComponent<C_ContactEvent>(resting).otherEntity_, platform);
}

} // namespace
