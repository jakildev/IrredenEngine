#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_rotation_target.hpp>
#include <irreden/update/components/component_rotation_target_lua.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/update/systems/system_rotation_target_local_transform.hpp>

#include <irreden/script/lua_script.hpp>

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_RotationTarget;
using IRComponents::C_WorldTransform;

constexpr float kEps = 1e-4f;

// End-to-end coverage for the C_RotationTarget Lua binding (#1541): a
// Lua-defined system drives `input_` each tick through the same column view
// any creation's automation lane would use, and the engine's
// ROTATION_TARGET_LOCAL_TRANSFORM maps it onto the entity's rotation. Proves
// the field is reachable (read + write) from Lua via the standard
// component-field path, not a bespoke one-off binding.
class RotationTargetLuaTest : public testing::Test {
  protected:
    RotationTargetLuaTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        // bindLuaDrivenEcs() first so the IRComponent table exists before
        // registerType records the C_RotationTarget handle under it.
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<C_RotationTarget>();
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

    // m_lua first so its sol::state outlives any sol::function-bearing columns
    // held by the EntityManager (matches lua_system_register_test.cpp).
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(RotationTargetLuaTest, LuaWrittenInputDrivesRotation) {
    auto &lua = m_lua.lua();

    const IRMath::vec3 axis(0, 0, 1);
    // input range [0,1] → angle range [0, pi/2]; start at input 0 (angle 0).
    auto id = IREntity::createEntity(
        C_LocalTransform{},
        C_RotationTarget{axis, 0.0f, IRMath::kHalfPi, 0.0f}
    );

    // A Lua automation lane writing input_ in place via the column view —
    // `:at(i)` hands Lua a std::ref to the row, so `.input = v` writes through.
    lua["driveValue"] = 1.0;
    auto registerResult = lua.safe_script(
        R"(
        return IRSystem.registerSystem({
            name = 'DriveRotationInput',
            components = { IRComponent.C_RotationTarget },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    arch.C_RotationTarget:at(i).input = driveValue
                end
            end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    const IRSystem::SystemId luaSysId = registerResult.get<lua_Integer>();

    // Lua drives input_, then the engine systems map it onto the rotation.
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {luaSysId,
         IRSystem::createSystem<IRSystem::ROTATION_TARGET_LOCAL_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    // input 1.0 of [0,1] → t=1 → maxAngle (pi/2): the Lua write landed in the
    // C++ column and ROTATION_TARGET_LOCAL_TRANSFORM consumed it.
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_RotationTarget>(id).input_, 1.0f);
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        IRMath::quatAxisAngle(axis, IRMath::kHalfPi)
    );

    // Drive to the midpoint on a second tick — proves the per-frame write path,
    // not just a one-shot construction-time value.
    lua["driveValue"] = 0.5;
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_RotationTarget>(id).input_, 0.5f);
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        IRMath::quatAxisAngle(axis, IRMath::kHalfPi * 0.5f)
    );
}

TEST_F(RotationTargetLuaTest, LuaReadsScalarFields) {
    auto &lua = m_lua.lua();

    // Non-default config so the read-back can't accidentally match a default.
    IREntity::createEntity(
        C_LocalTransform{},
        C_RotationTarget{
            IRMath::vec3(0, 1, 0),
            -IRMath::kHalfPi,
            IRMath::kHalfPi,
            0.25f,
            0.0f,
            4.0f
        }
    );

    auto registerResult = lua.safe_script(
        R"(
        readInput, readMinAngle, readMaxAngle, readInputMax = nil, nil, nil, nil
        return IRSystem.registerSystem({
            name = 'ReadRotationFields',
            components = { IRComponent.C_RotationTarget },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local rt = arch.C_RotationTarget:at(i)
                    readInput = rt.input
                    readMinAngle = rt.minAngle
                    readMaxAngle = rt.maxAngle
                    readInputMax = rt.inputMax
                end
            end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    const IRSystem::SystemId luaSysId = registerResult.get<lua_Integer>();

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {luaSysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_FLOAT_EQ(lua["readInput"].get<float>(), 0.25f);
    EXPECT_NEAR(lua["readMinAngle"].get<float>(), -IRMath::kHalfPi, kEps);
    EXPECT_NEAR(lua["readMaxAngle"].get<float>(), IRMath::kHalfPi, kEps);
    EXPECT_FLOAT_EQ(lua["readInputMax"].get<float>(), 4.0f);
}

} // namespace
