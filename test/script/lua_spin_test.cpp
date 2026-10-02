#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/common/components/component_auto_spin_lua.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_angular_velocity.hpp>
#include <irreden/update/components/component_angular_velocity_lua.hpp>
#include <irreden/update/systems/system_angular_velocity_damped.hpp>
#include <irreden/update/systems/system_auto_spin_local_transform.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>

namespace {

using IRComponents::C_AngularVelocity;
using IRComponents::C_AutoSpin;
using IRComponents::C_LocalTransform;
using IRComponents::C_WorldTransform;

constexpr float kEps = 1e-4f;

// Drives C_AutoSpin and C_AngularVelocity the way a Lua creation does: the
// components are constructed, retuned, and impulsed from Lua, and the engine's
// spin systems are composed into the pipeline by their Lua-visible names.
class LuaSpin : public testing::Test {
  protected:
    LuaSpin()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerType<IRMath::vec3, IRMath::vec3(float, float, float)>(
            "vec3",
            "x",
            &IRMath::vec3::x,
            "y",
            &IRMath::vec3::y,
            "z",
            &IRMath::vec3::z
        );
        m_lua.registerTypesFromTraits<C_LocalTransform, C_AutoSpin, C_AngularVelocity>();
        m_lua.registerCreateEntityFunction<C_LocalTransform, C_AutoSpin>("createAutoSpinner");
        m_lua.registerCreateEntityFunction<C_LocalTransform, C_AngularVelocity>(
            "createImpulseSpinner"
        );
        m_lua.registerCreateEntityFunction<C_LocalTransform, C_AutoSpin, C_AngularVelocity>(
            "createComposedSpinner"
        );
        m_lua.registerPrefabSystems<
            IRSystem::AUTO_SPIN_LOCAL_TRANSFORM,
            IRSystem::ANGULAR_VELOCITY_DAMPED,
            IRSystem::PROPAGATE_TRANSFORM>();
    }

    // Runs `script` and returns the entity it created.
    IREntity::EntityId createFromLua(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        EXPECT_TRUE(result.valid()) << sol::error{result}.what();
        return result.valid() ? result.get<IRScript::LuaEntity>().entity : IREntity::EntityId{0};
    }

    void runLua(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    static void expectVec3Near(IRMath::vec3 actual, IRMath::vec3 expected, float eps = kEps) {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    // q and -q are the same rotation; compare by action on the basis vectors.
    static void expectSameRotation(IRMath::vec4 actual, IRMath::vec4 expected) {
        for (const auto &v :
             {IRMath::vec3(1, 0, 0), IRMath::vec3(0, 1, 0), IRMath::vec3(0, 0, 1)}) {
            expectVec3Near(
                IRMath::rotateVectorByQuat(v, actual),
                IRMath::rotateVectorByQuat(v, expected)
            );
        }
    }

    // The documented time-to-rest bound (component_angular_velocity.hpp).
    static int ticksToRest(float impulseRate, float dampingPerFrame) {
        return static_cast<int>(IRMath::ceil(
            IRMath::log2(C_AngularVelocity::kAngularRestEpsilon / impulseRate) /
            IRMath::log2(1.0f - dampingPerFrame)
        ));
    }

    // The spin systems in their documented order, composed from Lua.
    static constexpr const char *kSpinPipeline = R"(
        IRSystem.registerPipeline(IRTime.UPDATE, {
            IRSystem.systemId(IRSystem.SystemName.AUTO_SPIN_LOCAL_TRANSFORM),
            IRSystem.systemId(IRSystem.SystemName.ANGULAR_VELOCITY_DAMPED),
            IRSystem.systemId(IRSystem.SystemName.PROPAGATE_TRANSFORM),
        })
    )";

    // m_lua first so its sol::state outlives any sol::function-bearing columns
    // held by the EntityManager (matches lua_system_register_test.cpp).
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaSpin, AutoSpinAdvancesRotation) {
    constexpr float kRate = 0.05f;
    constexpr int kTicks = 12;
    const IRMath::vec3 axis(0, 0, 1);

    const IREntity::EntityId id = createFromLua(R"(
        return IREntity.createAutoSpinner(
            C_LocalTransform.new(vec3.new(0, 0, 0)),
            C_AutoSpin.new(vec3.new(0, 0, 1), 0.05)
        )
    )");
    runLua(kSpinPipeline);

    for (int i = 0; i < kTicks; ++i) {
        tick();
    }
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        IRMath::quatAxisAngle(axis, kRate * kTicks)
    );

    // Retune from a Lua system column: read the fields back, then write a new
    // axis and rate in place.
    auto registerResult = m_lua.lua().safe_script(
        R"(
        readRate, readAxisZ = nil, nil
        return IRSystem.registerSystem({
            name = 'RetuneAutoSpin',
            components = { IRComponent.C_AutoSpin },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local spin = arch.C_AutoSpin:at(i)
                    readRate = spin.radiansPerFrame
                    readAxisZ = spin.axis.z
                    spin.radiansPerFrame = 0.25
                    spin.axis = { x = 1, y = 0, z = 0 }
                end
            end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {static_cast<IRSystem::SystemId>(registerResult.get<lua_Integer>())}
    );
    tick();

    EXPECT_FLOAT_EQ(m_lua.lua()["readRate"].get<float>(), kRate);
    EXPECT_FLOAT_EQ(m_lua.lua()["readAxisZ"].get<float>(), 1.0f);
    const C_AutoSpin &spin = IREntity::getComponent<C_AutoSpin>(id);
    EXPECT_FLOAT_EQ(spin.radiansPerFrame_, 0.25f);
    expectVec3Near(spin.axis_, IRMath::vec3(1, 0, 0));
}

TEST_F(LuaSpin, AngularVelocityColumnPropertiesReadAndWrite) {
    const IREntity::EntityId id = createFromLua(R"(
        return IREntity.createImpulseSpinner(
            C_LocalTransform.new(vec3.new(0, 0, 0)),
            C_AngularVelocity.new(vec3.new(0, 1, 0), 0.15, 0.05)
        )
    )");

    // Only the Lua system runs, so the values it reads and writes are not
    // decayed by ANGULAR_VELOCITY_DAMPED in between.
    auto registerResult = m_lua.lua().safe_script(
        R"(
        readRate, readDamping, readAxisX, readAxisY, readAxisZ = nil, nil, nil, nil, nil
        return IRSystem.registerSystem({
            name = 'RetuneAngularVelocity',
            components = { IRComponent.C_AngularVelocity },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local spin = arch.C_AngularVelocity:at(i)
                    readRate = spin.radiansPerFrame
                    readDamping = spin.dampingPerFrame
                    local axis = spin.axis
                    readAxisX, readAxisY, readAxisZ = axis.x, axis.y, axis.z
                    spin.radiansPerFrame = 0.4
                    spin.dampingPerFrame = 0.25
                    spin.axis = { x = 1, y = 0, z = 0 }
                end
            end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {static_cast<IRSystem::SystemId>(registerResult.get<lua_Integer>())}
    );
    tick();

    EXPECT_FLOAT_EQ(m_lua.lua()["readRate"].get<float>(), 0.15f);
    EXPECT_FLOAT_EQ(m_lua.lua()["readDamping"].get<float>(), 0.05f);
    EXPECT_FLOAT_EQ(m_lua.lua()["readAxisX"].get<float>(), 0.0f);
    EXPECT_FLOAT_EQ(m_lua.lua()["readAxisY"].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(m_lua.lua()["readAxisZ"].get<float>(), 0.0f);
    const C_AngularVelocity &spin = IREntity::getComponent<C_AngularVelocity>(id);
    EXPECT_FLOAT_EQ(spin.radiansPerFrame_, 0.4f);
    EXPECT_FLOAT_EQ(spin.dampingPerFrame_, 0.25f);
    expectVec3Near(spin.axis_, IRMath::vec3(1, 0, 0));
}

TEST_F(LuaSpin, ImpulseDecaysToRest) {
    constexpr float kImpulse = 0.2f;
    constexpr float kDamping = 0.1f;
    const IRMath::vec3 axis(0, 0, 1);

    const IREntity::EntityId id = createFromLua(R"(
        return IREntity.createImpulseSpinner(
            C_LocalTransform.new(vec3.new(0, 0, 0)),
            C_AngularVelocity.new(vec3.new(1, 0, 0), 0.0, 0.1)
        )
    )");
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_AngularVelocity>(id).dampingPerFrame_, kDamping);

    // The impulse arrives from a Lua system that runs once, ahead of the spin
    // systems — never from inside the spin tick.
    auto registerResult = m_lua.lua().safe_script(
        R"(
        return IRSystem.registerSystem({
            name = 'KickSpinner',
            components = { IRComponent.C_AngularVelocity },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    arch.C_AngularVelocity:at(i):impulse({ x = 0, y = 0, z = 2 }, 0.2)
                end
            end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {static_cast<IRSystem::SystemId>(registerResult.get<lua_Integer>())}
    );
    tick();

    {
        const C_AngularVelocity &spin = IREntity::getComponent<C_AngularVelocity>(id);
        EXPECT_FLOAT_EQ(spin.radiansPerFrame_, kImpulse);
        // The entity was at rest, so the impulse axis replaces the old one
        // (normalized).
        expectVec3Near(spin.axis_, axis);
    }

    runLua(kSpinPipeline);
    const int bound = ticksToRest(kImpulse, kDamping);
    float previousRate = kImpulse;
    float totalAngle = 0.0f;
    int ticksUntilRest = 0;
    while (previousRate != 0.0f && ticksUntilRest <= bound) {
        totalAngle += previousRate;
        tick();
        ++ticksUntilRest;
        const float rate = IREntity::getComponent<C_AngularVelocity>(id).radiansPerFrame_;
        EXPECT_LT(rate, previousRate) << "tick " << ticksUntilRest;
        previousRate = rate;
    }
    EXPECT_FLOAT_EQ(previousRate, 0.0f);
    EXPECT_LE(ticksUntilRest, bound);
    EXPECT_EQ(ticksUntilRest, C_AngularVelocity::ticksToRest(kImpulse, kDamping));
    // Not a degenerate one-tick stop: the decay took most of the bound.
    EXPECT_GT(ticksUntilRest, bound / 2);

    const IRMath::vec4 restRotation = IREntity::getComponent<C_WorldTransform>(id).rotation_;
    expectSameRotation(restRotation, IRMath::quatAxisAngle(axis, totalAngle));

    // At rest it stays put.
    tick();
    tick();
    expectSameRotation(IREntity::getComponent<C_WorldTransform>(id).rotation_, restRotation);
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_AngularVelocity>(id).radiansPerFrame_, 0.0f);
}

TEST_F(LuaSpin, ImpulseComposesWithAutoSpin) {
    constexpr float kAutoRate = 0.03f;
    constexpr float kImpulse = 0.2f;
    constexpr float kDamping = 0.1f;
    const IRMath::vec3 axis(0, 0, 1);

    const IREntity::EntityId id = createFromLua(R"(
        local spin = C_AngularVelocity.new(vec3.new(0, 0, 1), 0.0, 0.1)
        spin:impulse(vec3.new(0, 0, 1), 0.2)
        return IREntity.createComposedSpinner(
            C_LocalTransform.new(vec3.new(0, 0, 0)),
            C_AutoSpin.new(vec3.new(0, 0, 1), 0.03),
            spin
        )
    )");
    runLua(kSpinPipeline);

    // Run well past the impulse's rest so the remaining motion is the steady
    // spin alone.
    const int ticks = ticksToRest(kImpulse, kDamping) + 5;
    float impulseAngle = 0.0f;
    for (float rate = kImpulse; rate >= C_AngularVelocity::kAngularRestEpsilon;
         rate *= 1.0f - kDamping) {
        impulseAngle += rate;
    }
    for (int i = 0; i < ticks; ++i) {
        tick();
    }
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_AngularVelocity>(id).radiansPerFrame_, 0.0f);
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        IRMath::quatAxisAngle(axis, kAutoRate * ticks + impulseAngle)
    );

    // The steady spin keeps turning after the impulse has settled.
    const IRMath::vec4 before = IREntity::getComponent<C_WorldTransform>(id).rotation_;
    tick();
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        IRMath::quatMul(IRMath::quatAxisAngle(axis, kAutoRate), before)
    );
}

TEST_F(LuaSpin, ImpulsesOnDifferentAxesSumAsAngularVelocity) {
    const IREntity::EntityId id = createFromLua(R"(
        local spin = C_AngularVelocity.new()
        spin:impulse({ x = 1, y = 0, z = 0 }, 0.3)
        spin:impulse({ x = 0, y = 1, z = 0 }, 0.4)
        return IREntity.createImpulseSpinner(C_LocalTransform.new(vec3.new(0, 0, 0)), spin)
    )");
    {
        const C_AngularVelocity &spin = IREntity::getComponent<C_AngularVelocity>(id);
        EXPECT_NEAR(spin.radiansPerFrame_, 0.5f, kEps);
        expectVec3Near(spin.axis_, IRMath::vec3(0.6f, 0.8f, 0.0f));
    }

    // Same axis adds to the rate; an equal and opposite impulse cancels it.
    C_AngularVelocity spin{IRMath::vec3(0, 0, 1), 0.1f};
    spin.impulse(IRMath::vec3(0, 0, 1), 0.2f);
    EXPECT_NEAR(spin.radiansPerFrame_, 0.3f, kEps);
    expectVec3Near(spin.axis_, IRMath::vec3(0, 0, 1));
    spin.impulse(IRMath::vec3(0, 0, -1), spin.radiansPerFrame_);
    EXPECT_FLOAT_EQ(spin.radiansPerFrame_, 0.0f);
    expectVec3Near(spin.axis_, IRMath::vec3(0, 0, 1));
}

// The system and `ticksToRest()` share one damping policy: NaN and -inf never
// decay, +inf stops after one tick. Each case keeps the transform finite.
TEST_F(LuaSpin, NonFiniteDampingFollowsTheTicksToRestPolicy) {
    constexpr float kRate = 0.2f;
    constexpr int kTicks = 3;
    const IRMath::vec3 axis(0, 0, 1);

    const IREntity::EntityId nanId = createFromLua(R"(
        return IREntity.createImpulseSpinner(
            C_LocalTransform.new(vec3.new(0, 0, 0)),
            C_AngularVelocity.new(vec3.new(0, 0, 1), 0.2, 0 / 0)
        )
    )");
    const IREntity::EntityId negInfId = createFromLua(R"(
        return IREntity.createImpulseSpinner(
            C_LocalTransform.new(vec3.new(0, 0, 0)),
            C_AngularVelocity.new(vec3.new(0, 0, 1), 0.2, -1 / 0)
        )
    )");
    const IREntity::EntityId posInfId = createFromLua(R"(
        return IREntity.createImpulseSpinner(
            C_LocalTransform.new(vec3.new(0, 0, 0)),
            C_AngularVelocity.new(vec3.new(0, 0, 1), 0.2, 1 / 0)
        )
    )");
    ASSERT_TRUE(std::isnan(IREntity::getComponent<C_AngularVelocity>(nanId).dampingPerFrame_));
    runLua(kSpinPipeline);
    for (int i = 0; i < kTicks; ++i) {
        tick();
    }

    for (const IREntity::EntityId id : {nanId, negInfId}) {
        const C_AngularVelocity &spin = IREntity::getComponent<C_AngularVelocity>(id);
        EXPECT_EQ(C_AngularVelocity::ticksToRest(spin.radiansPerFrame_, spin.dampingPerFrame_), -1);
        EXPECT_FLOAT_EQ(spin.radiansPerFrame_, kRate);
        expectSameRotation(
            IREntity::getComponent<C_WorldTransform>(id).rotation_,
            IRMath::quatAxisAngle(axis, kRate * kTicks)
        );
    }

    EXPECT_EQ(C_AngularVelocity::ticksToRest(kRate, std::numeric_limits<float>::infinity()), 1);
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_AngularVelocity>(posInfId).radiansPerFrame_, 0.0f);
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(posInfId).rotation_,
        IRMath::quatAxisAngle(axis, kRate)
    );
}

TEST(AngularVelocityTicksToRest, BoundsOutOfRangeDamping) {
    constexpr float kRate = 0.25f;
    // Never decays: the system would spin forever, so the helper reports -1.
    EXPECT_EQ(C_AngularVelocity::ticksToRest(kRate, 0.0f), -1);
    EXPECT_EQ(C_AngularVelocity::ticksToRest(kRate, -0.5f), -1);
    EXPECT_EQ(C_AngularVelocity::ticksToRest(kRate, 1.0e-9f), -1);
    EXPECT_EQ(C_AngularVelocity::ticksToRest(kRate, std::numeric_limits<float>::quiet_NaN()), -1);
    // Above 1 clamps to 1: one tick, like the system.
    EXPECT_EQ(C_AngularVelocity::ticksToRest(kRate, 1.0f), 1);
    EXPECT_EQ(C_AngularVelocity::ticksToRest(kRate, 2.0f), 1);
    // A spin already at rest takes no ticks whatever the damping.
    EXPECT_EQ(C_AngularVelocity::ticksToRest(0.0f, 0.0f), 0);
    EXPECT_EQ(
        C_AngularVelocity::ticksToRest(C_AngularVelocity::kAngularRestEpsilon * 0.5f, 0.0f),
        0
    );
    // Negative rates use their magnitude.
    EXPECT_EQ(
        C_AngularVelocity::ticksToRest(-kRate, 0.1f),
        C_AngularVelocity::ticksToRest(kRate, 0.1f)
    );
}

} // namespace
