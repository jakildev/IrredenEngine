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
#include <irreden/update/components/component_collision_response.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/systems/system_acceleration.hpp>
#include <irreden/update/systems/system_collision_response.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/update/systems/system_velocity.hpp>

#include <cstdint>
#include <span>
#include <vector>

// COLLISION_RESPONSE end to end behind ACCELERATION_3D, VELOCITY_3D and
// PROPAGATE_TRANSFORM, with gravity 60 blocks/s^2 (+z is down). Expected
// flush positions are computed in the system's own operation order (solid
// face minus the body's centerOffset + halfExtents), so exact comparisons
// hold on non-dyadic geometry. The tolerance controls run the same fixture
// with contactTolUlps_ = 0, which must fail.

namespace {

using IRComponents::C_Acceleration3D;
using IRComponents::C_ColliderIso3DAABB;
using IRComponents::C_CollisionLayer;
using IRComponents::C_CollisionResponse;
using IRComponents::C_LocalTransform;
using IRComponents::C_Velocity3D;
using IRComponents::C_WorldTransform;
using IREntity::EntityId;
using IRMath::vec3;

constexpr std::uint32_t kBoxLayer = IRComponents::COLLISION_LAYER_DEFAULT;
constexpr std::uint32_t kBodyLayer = IRComponents::COLLISION_LAYER_PARTICLE;
constexpr float kGravity = 60.0f;

struct BodySpec {
    vec3 position_ = vec3(0.0f);
    vec3 velocity_ = vec3(0.0f);
    vec3 halfExtents_ = vec3(0.5f);
    vec3 centerOffset_ = vec3(0.0f);
    std::uint32_t mask_ = kBoxLayer;
    bool solid_ = true;
    C_CollisionResponse response_{};
    bool responds_ = true;
};

class CollisionResponseTest : public testing::Test {
  protected:
    CollisionResponseTest()
        : m_time_manager{}
        , m_entity_manager{}
        , m_system_manager{}
        , m_response{IRSystem::createSystem<IRSystem::COLLISION_RESPONSE>()} {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::ACCELERATION_3D>(),
             IRSystem::createSystem<IRSystem::VELOCITY_3D>(),
             IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
             m_response}
        );
        m_dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    void setTolUlps(float tolUlps) {
        IRSystem::getSystemParams<IRSystem::System<IRSystem::COLLISION_RESPONSE>>(m_response)
            ->contactTolUlps_ = tolUlps;
    }

    static EntityId makeBox(vec3 translation, vec3 halfExtents, vec3 centerOffset = vec3(0.0f)) {
        return IREntity::createEntity(
            C_LocalTransform{translation},
            C_ColliderIso3DAABB{halfExtents, centerOffset},
            C_CollisionLayer{kBoxLayer, 0u, true}
        );
    }

    static EntityId makeBody(const BodySpec &spec) {
        if (!spec.responds_) {
            return IREntity::createEntity(
                C_LocalTransform{spec.position_},
                C_Velocity3D{spec.velocity_},
                C_Acceleration3D{0.0f, 0.0f, kGravity},
                C_ColliderIso3DAABB{spec.halfExtents_, spec.centerOffset_},
                C_CollisionLayer{kBodyLayer, spec.mask_, spec.solid_}
            );
        }
        return IREntity::createEntity(
            C_LocalTransform{spec.position_},
            C_Velocity3D{spec.velocity_},
            C_Acceleration3D{0.0f, 0.0f, kGravity},
            C_ColliderIso3DAABB{spec.halfExtents_, spec.centerOffset_},
            C_CollisionLayer{kBodyLayer, spec.mask_, spec.solid_},
            spec.response_
        );
    }

    // The top face (min z, up is -z) of a box, as the system's snapshot
    // builds it.
    static float boxTop(vec3 translation, vec3 halfExtents, vec3 centerOffset = vec3(0.0f)) {
        return ((translation + centerOffset) - halfExtents).z;
    }

    static float boxBottom(vec3 translation, vec3 halfExtents, vec3 centerOffset = vec3(0.0f)) {
        return ((translation + centerOffset) + halfExtents).z;
    }

    // The body translation z that puts its bottom on `top`.
    static float flushZ(float top, const BodySpec &spec) {
        return top - (spec.centerOffset_ + spec.halfExtents_).z;
    }

    static float topOf(const BodySpec &spec, EntityId body) {
        return z(body) + spec.centerOffset_.z - spec.halfExtents_.z;
    }

    static vec3 translation(EntityId entity) {
        return IREntity::getComponent<C_WorldTransform>(entity).translation_;
    }
    static float z(EntityId entity) {
        return translation(entity).z;
    }
    static vec3 velocity(EntityId entity) {
        return IREntity::getComponent<C_Velocity3D>(entity).velocity_;
    }
    static bool resting(EntityId entity) {
        return IREntity::getComponent<C_CollisionResponse>(entity).resting_;
    }

    float tolAt(
        vec3 bodyPosition, const BodySpec &spec, vec3 boxTranslation, vec3 boxHalf, vec3 boxOffset
    ) const {
        const float bodyMagnitude = IRSystem::detail::maxAbsComponent(bodyPosition) +
                                    IRSystem::detail::maxAbsComponent(spec.centerOffset_) +
                                    IRSystem::detail::maxAbsComponent(spec.halfExtents_);
        const float solidMagnitude = IRSystem::detail::maxAbsComponent(boxTranslation) +
                                     IRSystem::detail::maxAbsComponent(boxOffset) +
                                     IRSystem::detail::maxAbsComponent(boxHalf);
        return IRSystem::detail::collisionContactTolerance(
            IRMath::max(bodyMagnitude, solidMagnitude),
            IRSystem::kCollisionContactTolUlps
        );
    }

    // Drops `body` from its bottom 8 above the support whose flush
    // translation z is `flush`, with restitution 0.5.
    void expectBounceToRest(EntityId body, float flush) {
        const float gStep = kGravity * m_dt;
        int impacts = 0;
        float apexZ = 0.0f;
        std::vector<float> apexHeights{8.0f};
        bool rising = false;
        int tickIndex = 0;
        for (; tickIndex < 600; ++tickIndex) {
            const float before = velocity(body).z;
            tick();
            const float after = velocity(body).z;
            if (resting(body)) {
                break;
            }
            if (after < 0.0f && before + gStep > 0.0f) {
                ++impacts;
                EXPECT_NEAR(after, -0.5f * (before + gStep), 1e-4f) << "impact " << impacts;
                EXPECT_EQ(z(body), flush) << "impact " << impacts;
                if (rising) {
                    apexHeights.push_back(flush - apexZ);
                }
                rising = true;
                apexZ = z(body);
            } else if (rising) {
                apexZ = IRMath::min(apexZ, z(body));
            }
        }
        ASSERT_LT(tickIndex, 600) << "never came to rest";
        if (rising) {
            apexHeights.push_back(flush - apexZ);
        }
        EXPECT_GE(impacts, 3);
        ASSERT_GE(apexHeights.size(), 3u);
        for (std::size_t i = 1; i <= 2; ++i) {
            const float ratio = apexHeights[i] / apexHeights[i - 1];
            EXPECT_GE(ratio, 0.18f) << "apex " << i;
            EXPECT_LE(ratio, 0.30f) << "apex " << i;
        }

        const vec3 rested = translation(body);
        EXPECT_EQ(rested.z, flush);
        for (int i = 0; i < 30; ++i) {
            if (i > 0) {
                tick();
            }
            EXPECT_TRUE(resting(body)) << "tick " << i;
            EXPECT_EQ(velocity(body), vec3(0.0f)) << "tick " << i;
            EXPECT_EQ(translation(body), rested) << "tick " << i;
        }
    }

    // A body resting on its support sliding at 16 blocks/s, friction 0.25.
    void expectSlideDecaysThenStops(EntityId body) {
        float expected = 16.0f;
        for (int k = 1; k <= 7; ++k) {
            tick();
            expected *= 0.75f;
            EXPECT_NEAR(velocity(body).x, expected, expected * 1e-4f) << "tick " << k;
        }
        tick();
        const float stoppedX = translation(body).x;
        for (int k = 8; k < 38; ++k) {
            EXPECT_EQ(velocity(body).x, 0.0f) << "tick " << k;
            EXPECT_EQ(translation(body).x, stoppedX) << "tick " << k;
            tick();
        }
    }

    IRTime::TimeManager m_time_manager;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRSystem::SystemId m_response;
    float m_dt = 0.0f;
};

// ---- Bounce ------------------------------------------------------------------

TEST_F(CollisionResponseTest, BounceHalvesEachImpactThenRests) {
    const vec3 boxAt(0.0f, 0.0f, 1.0f);
    const vec3 boxHalf(5.0f, 5.0f, 0.5f);
    makeBox(boxAt, boxHalf);
    BodySpec spec;
    spec.response_ = C_CollisionResponse{0.5f, 0.0f, true};
    const float flush = flushZ(boxTop(boxAt, boxHalf), spec);
    spec.position_ = vec3(0.0f, 0.0f, flush - 8.0f);
    const EntityId body = makeBody(spec);
    expectBounceToRest(body, flush);
}

TEST_F(CollisionResponseTest, FloorPlaneBounceHalvesEachImpactThenRests) {
    BodySpec spec;
    spec.response_ = C_CollisionResponse{0.5f, 0.0f, true, 0.5f};
    const float flush = flushZ(0.5f, spec);
    spec.position_ = vec3(0.0f, 0.0f, flush - 8.0f);
    const EntityId body = makeBody(spec);
    expectBounceToRest(body, flush);
}

// ---- Slide -------------------------------------------------------------------

TEST_F(CollisionResponseTest, FrictionDecaysASlideThenStopsIt) {
    const vec3 boxAt(0.0f, 0.0f, 1.0f);
    const vec3 boxHalf(50.0f, 5.0f, 0.5f);
    makeBox(boxAt, boxHalf);
    BodySpec spec;
    spec.response_ = C_CollisionResponse{0.0f, 0.25f, true};
    spec.position_ = vec3(0.0f, 0.0f, flushZ(boxTop(boxAt, boxHalf), spec));
    spec.velocity_ = vec3(16.0f, 0.0f, 0.0f);
    expectSlideDecaysThenStops(makeBody(spec));
}

TEST_F(CollisionResponseTest, FloorPlaneFrictionDecaysASlideThenStopsIt) {
    BodySpec spec;
    spec.response_ = C_CollisionResponse{0.0f, 0.25f, true, 0.5f};
    spec.position_ = vec3(0.0f, 0.0f, flushZ(0.5f, spec));
    spec.velocity_ = vec3(16.0f, 0.0f, 0.0f);
    expectSlideDecaysThenStops(makeBody(spec));
}

TEST_F(CollisionResponseTest, FrictionlessSlideKeepsItsSpeed) {
    const vec3 boxAt(0.0f, 0.0f, 1.0f);
    const vec3 boxHalf(50.0f, 5.0f, 0.5f);
    makeBox(boxAt, boxHalf);
    BodySpec spec;
    spec.response_ = C_CollisionResponse{0.0f, 0.0f, true};
    const float flush = flushZ(boxTop(boxAt, boxHalf), spec);
    spec.position_ = vec3(0.0f, 0.0f, flush);
    spec.velocity_ = vec3(16.0f, 0.0f, 0.0f);
    const EntityId body = makeBody(spec);
    for (int k = 0; k < 60; ++k) {
        tick();
        EXPECT_EQ(velocity(body).x, 16.0f) << "tick " << k;
        EXPECT_EQ(z(body), flush) << "tick " << k;
    }
}

// ---- One-sided layers -------------------------------------------------------

class CollisionResponseLayerTest : public CollisionResponseTest {
  protected:
    const vec3 m_box_at{0.0f, 0.0f, 1.0f};
    const vec3 m_box_half{5.0f, 5.0f, 0.5f};

    EntityId dropOntoBox(std::uint32_t mask, bool solid) {
        makeBox(m_box_at, m_box_half);
        BodySpec spec;
        spec.mask_ = mask;
        spec.solid_ = solid;
        spec.position_ = vec3(0.0f, 0.0f, flushZ(boxTop(m_box_at, m_box_half), spec) - 2.0f);
        return makeBody(spec);
    }
};

TEST_F(CollisionResponseLayerTest, BodyMaskIncludingTheBoxLayerLands) {
    const EntityId body = dropOntoBox(kBoxLayer, true);
    for (int k = 0; k < 120; ++k) {
        tick();
    }
    EXPECT_TRUE(resting(body));
    EXPECT_EQ(z(body), boxTop(m_box_at, m_box_half) - 0.5f);
}

TEST_F(CollisionResponseLayerTest, BodyMaskWithoutTheBoxLayerFallsThrough) {
    BodySpec spec;
    const EntityId body = dropOntoBox(0u, true);
    for (int k = 0; k < 120; ++k) {
        tick();
    }
    EXPECT_GT(topOf(spec, body), boxBottom(m_box_at, m_box_half));
}

TEST_F(CollisionResponseLayerTest, NonSolidBodyFallsThrough) {
    BodySpec spec;
    const EntityId body = dropOntoBox(kBoxLayer, false);
    for (int k = 0; k < 120; ++k) {
        tick();
    }
    EXPECT_GT(topOf(spec, body), boxBottom(m_box_at, m_box_half));
}

// ---- Seams -------------------------------------------------------------------

TEST_F(CollisionResponseTest, SlideCrossesAFlushSeam) {
    const vec3 boxHalfA(2.0f, 2.0f, 0.5f);
    const vec3 boxHalfB(4.0f, 2.0f, 0.5f);
    makeBox(vec3(0.0f, 0.0f, 1.0f), boxHalfA);
    makeBox(vec3(6.0f, 0.0f, 1.0f), boxHalfB);
    BodySpec spec;
    spec.response_ = C_CollisionResponse{0.0f, 0.0f, true};
    const float flush = flushZ(boxTop(vec3(0.0f, 0.0f, 1.0f), boxHalfA), spec);
    spec.position_ = vec3(0.0f, 0.0f, flush);
    spec.velocity_ = vec3(6.0f, 0.0f, 0.0f);
    const EntityId body = makeBody(spec);
    for (int k = 0; k < 60; ++k) {
        tick();
        EXPECT_EQ(velocity(body).x, 6.0f) << "tick " << k;
        EXPECT_EQ(z(body), flush) << "tick " << k;
    }
    EXPECT_GT(translation(body).x - 0.5f, 2.0f);
}

// 64 seams on non-dyadic geometry. Box B's top is half a tolerance higher
// than A's, so a slider resting on A lies inside B on z by less than the
// rounding allowance: B must not read as a wall.
class CollisionResponseSeamTest : public CollisionResponseTest {
  protected:
    // Returns the number of the 64 slides that kept 6 blocks/s on every tick
    // and crossed the seam.
    int crossings() {
        int crossed = 0;
        for (int i = 0; i < 64; ++i) {
            m_entity_manager.destroyAllEntities();
            const float topA = 8.3f + 0.0371f * static_cast<float>(i);
            const vec3 boxHalfA(2.0f, 2.0f, 0.3f);
            const vec3 boxHalfB(4.0f, 2.0f, 0.3f);
            const vec3 boxA(0.0f, 0.0f, topA + 0.3f);
            const float builtTopA = boxTop(boxA, boxHalfA);

            BodySpec spec;
            spec.halfExtents_ = vec3(0.3f);
            spec.centerOffset_ = vec3(0.0f, 0.0f, 0.07f);
            spec.response_ = C_CollisionResponse{0.0f, 0.0f, true};
            spec.velocity_ = vec3(6.0f, 0.0f, 0.0f);
            spec.position_ = vec3(0.0f, 0.0f, flushZ(builtTopA, spec));

            const vec3 boxBGuess(6.0f, 0.0f, builtTopA + 0.3f);
            const float tol = tolAt(spec.position_, spec, boxBGuess, boxHalfB, vec3(0.0f));
            const vec3 boxB(6.0f, 0.0f, (builtTopA - 0.5f * tol) + 0.3f);
            EXPECT_LT(boxTop(boxB, boxHalfB), builtTopA) << "fixture " << i;
            EXPECT_GT(boxTop(boxB, boxHalfB), builtTopA - tol) << "fixture " << i;

            makeBox(boxA, boxHalfA);
            makeBox(boxB, boxHalfB);
            const EntityId body = makeBody(spec);
            bool keptSpeed = true;
            for (int k = 0; k < 60; ++k) {
                tick();
                keptSpeed = keptSpeed && velocity(body).x == 6.0f;
            }
            if (keptSpeed && translation(body).x - 0.3f > 2.0f) {
                ++crossed;
            }
        }
        return crossed;
    }
};

TEST_F(CollisionResponseSeamTest, SlideCrossesSeamsBetweenTopsBuiltDifferently) {
    EXPECT_EQ(crossings(), 64);
}

TEST_F(CollisionResponseSeamTest, ExactIntervalsHitAGhostWallAtSuchSeams) {
    setTolUlps(0.0f);
    EXPECT_LT(crossings(), 64);
}

// ---- Rest on non-dyadic tops -------------------------------------------------

class CollisionResponseRestTest : public CollisionResponseTest {
  protected:
    struct Outcome {
        int stayedFlush_ = 0;
        int fellThrough_ = 0;
    };

    // Rests a body on 64 non-dyadic box tops for 600 ticks, starting
    // `insideFraction` of a tolerance inside the top.
    Outcome restOnTops(float insideFraction) {
        Outcome outcome;
        for (int i = 0; i < 64; ++i) {
            m_entity_manager.destroyAllEntities();
            const float top = 8.3f + 0.0371f * static_cast<float>(i);
            const vec3 boxHalf(2.0f, 2.0f, 0.3f);
            const vec3 boxOffset(0.0f, 0.0f, 0.13f);
            const vec3 boxAt(0.0f, 0.0f, top + 0.3f - 0.13f);
            const float builtTop = boxTop(boxAt, boxHalf, boxOffset);

            BodySpec spec;
            spec.halfExtents_ = vec3(0.3f);
            spec.centerOffset_ = vec3(0.0f, 0.0f, 0.07f);
            const float flush = flushZ(builtTop, spec);
            const float tol = tolAt(vec3(0.0f, 0.0f, flush), spec, boxAt, boxHalf, boxOffset);
            spec.position_ = vec3(0.0f, 0.0f, flush + insideFraction * tol);

            makeBox(boxAt, boxHalf, boxOffset);
            const EntityId body = makeBody(spec);
            bool flushEveryTick = true;
            for (int k = 0; k < 600; ++k) {
                tick();
                flushEveryTick = flushEveryTick && z(body) == flush && resting(body);
            }
            const double bottom = static_cast<double>(z(body)) +
                                  static_cast<double>(spec.centerOffset_.z) +
                                  static_cast<double>(spec.halfExtents_.z);
            if (flushEveryTick && IRMath::abs(bottom - static_cast<double>(builtTop)) <= tol) {
                ++outcome.stayedFlush_;
            }
            if (topOf(spec, body) > boxBottom(boxAt, boxHalf, boxOffset)) {
                ++outcome.fellThrough_;
            }
        }
        return outcome;
    }
};

TEST_F(CollisionResponseRestTest, FlushBodyStaysBitIdenticalOnNonDyadicTops) {
    const Outcome outcome = restOnTops(0.0f);
    EXPECT_EQ(outcome.stayedFlush_, 64);
    EXPECT_EQ(outcome.fellThrough_, 0);
}

TEST_F(CollisionResponseRestTest, BodyRoundedInsideItsSupportIsPulledFlush) {
    const Outcome outcome = restOnTops(0.5f);
    EXPECT_EQ(outcome.stayedFlush_, 64);
    EXPECT_EQ(outcome.fellThrough_, 0);
}

TEST_F(CollisionResponseRestTest, ExactIntervalsDropABodyRoundedInsideItsSupport) {
    setTolUlps(0.0f);
    EXPECT_GE(restOnTops(0.5f).fellThrough_, 1);
}

// ---- Floor plane -------------------------------------------------------------

TEST_F(CollisionResponseTest, BodyBelowTheFloorIsLiftedOntoIt) {
    BodySpec spec;
    spec.halfExtents_ = vec3(0.3f, 0.3f, 0.45f);
    spec.centerOffset_ = vec3(0.0f, 0.0f, 0.11f);
    constexpr float kFloorZ = 4.7f;
    spec.response_ = C_CollisionResponse{0.5f, 0.0f, true, kFloorZ};
    spec.position_ = vec3(0.0f, 0.0f, flushZ(kFloorZ, spec) + 3.0f);
    const EntityId body = makeBody(spec);
    tick();
    EXPECT_EQ(z(body), kFloorZ - (spec.centerOffset_.z + spec.halfExtents_.z));
}

// ---- Swept -------------------------------------------------------------------

class CollisionResponseSweptTest : public CollisionResponseTest {
  protected:
    const vec3 m_box_at{0.0f, 0.0f, 1.0f};
    const vec3 m_box_half{5.0f, 5.0f, 0.5f};

    // A body falling at 600 blocks/s from its bottom 0.5 above a 1-thick
    // box: one tick moves it 10 blocks, clear through the box.
    EntityId dropFast(bool responds) {
        makeBox(m_box_at, m_box_half);
        BodySpec spec;
        spec.response_ = C_CollisionResponse{0.0f, 0.0f, true};
        spec.responds_ = responds;
        spec.position_ = vec3(0.0f, 0.0f, flushZ(boxTop(m_box_at, m_box_half), spec) - 0.5f);
        spec.velocity_ = vec3(0.0f, 0.0f, 600.0f);
        return makeBody(spec);
    }
};

TEST_F(CollisionResponseSweptTest, FastBodyLandsOnAThinBoxInsteadOfTunneling) {
    const EntityId body = dropFast(true);
    tick();
    EXPECT_EQ(z(body), flushZ(boxTop(m_box_at, m_box_half), BodySpec{}));
}

TEST_F(CollisionResponseSweptTest, FastBodyWithoutAResponseTunnels) {
    const EntityId body = dropFast(false);
    tick();
    EXPECT_GT(topOf(BodySpec{}, body), boxBottom(m_box_at, m_box_half));
}

TEST_F(CollisionResponseTest, TwoFrameRatesLandOnTheSameFace) {
    const vec3 boxAt(0.0f, 0.0f, 1.0f);
    const C_ColliderIso3DAABB box{vec3(5.0f, 5.0f, 0.5f)};
    const IRSystem::detail::CollisionSolid solid =
        IRSystem::detail::makeCollisionSolid(boxAt, box, kBoxLayer);
    const C_ColliderIso3DAABB collider{vec3(0.5f)};
    const C_CollisionLayer layer{kBodyLayer, kBoxLayer, true};
    const float flush = solid.min_.z - (collider.centerOffset_ + collider.halfExtents_).z;
    const vec3 start(0.0f, 0.0f, flush - 0.5f);
    std::vector<IRSystem::detail::CollisionCandidate> scratch;
    std::vector<float> landed;
    for (const float step : {m_dt, 0.5f * m_dt}) {
        vec3 velocity(0.0f, 0.0f, 600.0f);
        vec3 translation = start + velocity * vec3(step);
        ASSERT_GT(translation.z - 0.5f, solid.max_.z) << "the step must cross the box";
        C_CollisionResponse response{0.0f, 0.0f, true};
        IRSystem::detail::resolveCollisionBody(
            translation,
            velocity,
            response,
            collider,
            layer,
            std::span<const IRSystem::detail::CollisionSolid>(&solid, 1),
            step,
            IRSystem::kCollisionContactTolUlps,
            scratch
        );
        landed.push_back(translation.z);
    }
    EXPECT_EQ(landed[0], flush);
    EXPECT_EQ(landed[1], flush);
}

} // namespace
