#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

#include <irreden/script/lua_script.hpp>

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_WorldTransform;

constexpr float kEps = 1e-4f;

// End-to-end coverage for the C_LocalTransform Lua binding (#2191): a
// Lua-defined system writes the math-typed SQT fields (rotation/translation/
// scale) in place through the same `:at(i)` column view any creation's EVAL
// tick would use, and PROPAGATE_TRANSFORM composes them onto C_WorldTransform.
// Proves the fields are reachable (read + write) from Lua via the standard
// { x, y, z, w } table convention — no C++ shim, no IRMath::vec* usertype.
class LocalTransformLuaTest : public testing::Test {
  protected:
    LocalTransformLuaTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        // bindLuaDrivenEcs() first so the IRComponent table exists before
        // registerType records the C_LocalTransform handle under it.
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<C_LocalTransform>();
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

    // Push a quaternion's components into Lua globals so the EVAL tick can
    // assemble the { x, y, z, w } table the binding's setter accepts.
    void setQuatGlobals(IRMath::vec4 q) {
        auto &lua = m_lua.lua();
        lua["qx"] = q.x;
        lua["qy"] = q.y;
        lua["qz"] = q.z;
        lua["qw"] = q.w;
    }

    IRSystem::SystemId registerLuaSystem(const char *src) {
        auto result = m_lua.lua().safe_script(src, sol::script_pass_on_error);
        EXPECT_TRUE(result.valid()) << sol::error{result}.what();
        return result.get<lua_Integer>();
    }

    // m_lua first so its sol::state outlives any sol::function-bearing columns
    // held by the EntityManager (matches lua_rotation_target_lua_test.cpp).
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LocalTransformLuaTest, KeyedRotationTableDrivesWorldTransform) {
    auto id = IREntity::createEntity(C_LocalTransform{}, C_WorldTransform{});

    // A Lua EVAL lane writing rotation_ in place via the column view —
    // `:at(i)` hands Lua a std::ref to the row, so `.rotation = {..}` writes
    // through the referenced C_LocalTransform, exactly like the proven scalar
    // member-pointer path in lua_rotation_target_lua_test.cpp.
    const IRSystem::SystemId luaSysId = registerLuaSystem(
        R"(
        return IRSystem.registerSystem({
            name = 'DriveLocalRotation',
            components = { IRComponent.C_LocalTransform },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    arch.C_LocalTransform:at(i).rotation = { x = qx, y = qy, z = qz, w = qw }
                end
            end,
        })
    )"
    );

    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {luaSysId, IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );

    // First tick: yaw 90° about +Z. The Lua write lands in the C++ column and
    // PROPAGATE_TRANSFORM (root entity → world.rotation = local.rotation)
    // reflects it.
    const IRMath::vec4 expected1 = IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), IRMath::kHalfPi);
    setQuatGlobals(expected1);
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    expectSameRotation(IREntity::getComponent<C_LocalTransform>(id).rotation_, expected1);
    expectSameRotation(IREntity::getComponent<C_WorldTransform>(id).rotation_, expected1);

    // Second tick with a different quat — proves the per-frame write path, not
    // a one-shot construction-time value.
    const IRMath::vec4 expected2 =
        IRMath::quatAxisAngle(IRMath::vec3(1, 0, 0), IRMath::kHalfPi * 0.5f);
    setQuatGlobals(expected2);
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    expectSameRotation(IREntity::getComponent<C_WorldTransform>(id).rotation_, expected2);
}

TEST_F(LocalTransformLuaTest, IndexedRotationTableIsAccepted) {
    auto id = IREntity::createEntity(C_LocalTransform{}, C_WorldTransform{});

    // The indexed { qx, qy, qz, qw } spelling must round-trip identically to
    // the keyed one (quatFromLua accepts both).
    const IRSystem::SystemId luaSysId = registerLuaSystem(
        R"(
        return IRSystem.registerSystem({
            name = 'DriveLocalRotationIndexed',
            components = { IRComponent.C_LocalTransform },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    arch.C_LocalTransform:at(i).rotation = { qx, qy, qz, qw }
                end
            end,
        })
    )"
    );

    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {luaSysId, IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );

    const IRMath::vec4 expected = IRMath::quatAxisAngle(IRMath::vec3(0, 1, 0), IRMath::kHalfPi);
    setQuatGlobals(expected);
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    expectSameRotation(IREntity::getComponent<C_WorldTransform>(id).rotation_, expected);
}

TEST_F(LocalTransformLuaTest, PartialRotationTableDefaultsToIdentity) {
    // Start from a non-identity rotation so an identity result can't be the
    // untouched default — the empty-table write must actively reset it.
    auto id = IREntity::createEntity(
        C_LocalTransform{
            IRMath::vec3(0.0f),
            IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), IRMath::kHalfPi)
        },
        C_WorldTransform{}
    );

    const IRSystem::SystemId luaSysId = registerLuaSystem(
        R"(
        return IRSystem.registerSystem({
            name = 'ResetLocalRotation',
            components = { IRComponent.C_LocalTransform },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    arch.C_LocalTransform:at(i).rotation = {}
                end
            end,
        })
    )"
    );

    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {luaSysId, IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    // quatFromLua fills missing components with identity (0,0,0,1).
    const IRMath::vec4 identity(0.0f, 0.0f, 0.0f, 1.0f);
    expectSameRotation(IREntity::getComponent<C_LocalTransform>(id).rotation_, identity);
    expectSameRotation(IREntity::getComponent<C_WorldTransform>(id).rotation_, identity);
}

TEST_F(LocalTransformLuaTest, TranslationAndScaleWriteReadRoundTripAndScalarGettersUnchanged) {
    auto id = IREntity::createEntity(C_LocalTransform{}, C_WorldTransform{});

    // One tick that writes translation + scale via the table convention, then
    // reads all three math fields back as tables (proving the property getter),
    // plus the legacy translation_x/y/z scalar getters (proving no regression).
    // The scalar getters are bound as bare-lambda member *functions* — Lua
    // invokes them with call syntax (`h:translation_x()`), unlike the new
    // `sol::property` fields which use assignment/index syntax.
    const IRSystem::SystemId luaSysId = registerLuaSystem(
        R"(
        readTx, readTy, readTz = nil, nil, nil
        readSx, readSy, readSz = nil, nil, nil
        readScalarX, readScalarY, readScalarZ = nil, nil, nil
        return IRSystem.registerSystem({
            name = 'WriteReadLocalTransform',
            components = { IRComponent.C_LocalTransform },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local h = arch.C_LocalTransform:at(i)
                    h.translation = { x = 1.5, y = -2.0, z = 3.25 }
                    h.scale = { x = 2.0, y = 4.0, z = 0.5 }
                    local t = h.translation
                    readTx, readTy, readTz = t.x, t.y, t.z
                    local s = h.scale
                    readSx, readSy, readSz = s.x, s.y, s.z
                    readScalarX = h:translation_x()
                    readScalarY = h:translation_y()
                    readScalarZ = h:translation_z()
                end
            end,
        })
    )"
    );

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {luaSysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    auto &lua = m_lua.lua();
    // Property-table getter round-trip.
    EXPECT_FLOAT_EQ(lua["readTx"].get<float>(), 1.5f);
    EXPECT_FLOAT_EQ(lua["readTy"].get<float>(), -2.0f);
    EXPECT_FLOAT_EQ(lua["readTz"].get<float>(), 3.25f);
    EXPECT_FLOAT_EQ(lua["readSx"].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(lua["readSy"].get<float>(), 4.0f);
    EXPECT_FLOAT_EQ(lua["readSz"].get<float>(), 0.5f);
    // Legacy scalar getters (member functions) read the same freshly-written
    // translation through the live column ref — unchanged by the new bindings.
    EXPECT_FLOAT_EQ(lua["readScalarX"].get<float>(), 1.5f);
    EXPECT_FLOAT_EQ(lua["readScalarY"].get<float>(), -2.0f);
    EXPECT_FLOAT_EQ(lua["readScalarZ"].get<float>(), 3.25f);

    // The writes landed in the C++ column, not just a Lua-side copy.
    const auto &local = IREntity::getComponent<C_LocalTransform>(id);
    expectVec3Near(local.translation_, IRMath::vec3(1.5f, -2.0f, 3.25f));
    expectVec3Near(local.scale_, IRMath::vec3(2.0f, 4.0f, 0.5f));
}

} // namespace
