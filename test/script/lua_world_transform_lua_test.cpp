#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/components/component_world_transform_lua.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

#include <irreden/script/lua_script.hpp>

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_WorldTransform;

constexpr float kEps = 1e-4f;

// End-to-end coverage for the C_WorldTransform Lua binding: a Lua EVAL system
// ordered after PROPAGATE_TRANSFORM reads and writes the resolved world SQT in
// place through the `:at(i)` column view, the way a creation writes a
// render-only offset before the voxel bake.
class WorldTransformLuaTest : public testing::Test {
  protected:
    WorldTransformLuaTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        // bindLuaDrivenEcs() first so the IRComponent table exists before
        // registerType records the C_WorldTransform handle under it.
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<C_WorldTransform>();
    }

    static void expectVec3Near(IRMath::vec3 actual, IRMath::vec3 expected, float eps = kEps) {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    IRSystem::SystemId registerLuaSystem(const char *src) {
        auto result = m_lua.lua().safe_script(src, sol::script_pass_on_error);
        EXPECT_TRUE(result.valid()) << sol::error{result}.what();
        return result.get<lua_Integer>();
    }

    // m_lua first so its sol::state outlives any sol::function-bearing columns
    // held by the EntityManager.
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(WorldTransformLuaTest, TranslationOffsetAfterPropagationIsWrittenAndReadBack) {
    const IRMath::vec3 propagated(4.0f, -2.0f, 7.0f);
    auto id = IREntity::createEntity(C_LocalTransform{propagated}, C_WorldTransform{});

    const IRSystem::SystemId luaSysId = registerLuaSystem(
        R"(
        readX, readY, readZ = nil, nil, nil
        return IRSystem.registerSystem({
            name = 'OffsetWorldTranslation',
            components = { IRComponent.C_WorldTransform },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local h = arch.C_WorldTransform:at(i)
                    local t = h.translation
                    h.translation = { x = t.x + 0.5, y = t.y - 1.5, z = t.z + 2.0 }
                    local r = h.translation
                    readX, readY, readZ = r.x, r.y, r.z
                end
            end,
        })
    )"
    );

    // The Lua system is ordered AFTER propagation, as a render-only writer must be.
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(), luaSysId}
    );
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    const IRMath::vec3 expected = propagated + IRMath::vec3(0.5f, -1.5f, 2.0f);
    expectVec3Near(IREntity::getComponent<C_WorldTransform>(id).translation_, expected);
    // The offset is non-zero, so this is not the untouched propagated value.
    EXPECT_GT(IRMath::length(expected - propagated), 1.0f);

    auto &lua = m_lua.lua();
    EXPECT_NEAR(lua["readX"].get<float>(), expected.x, kEps);
    EXPECT_NEAR(lua["readY"].get<float>(), expected.y, kEps);
    EXPECT_NEAR(lua["readZ"].get<float>(), expected.z, kEps);
}

TEST_F(WorldTransformLuaTest, RotationAndScaleWritesLandInCppFields) {
    auto id = IREntity::createEntity(C_LocalTransform{}, C_WorldTransform{});

    const IRSystem::SystemId luaSysId = registerLuaSystem(
        R"(
        return IRSystem.registerSystem({
            name = 'WriteWorldRotationScale',
            components = { IRComponent.C_WorldTransform },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local h = arch.C_WorldTransform:at(i)
                    h.rotation = { x = 0.0, y = 0.0, z = 0.70710678, w = 0.70710678 }
                    h.scale = { x = 2.0, y = 4.0, z = 0.5 }
                end
            end,
        })
    )"
    );

    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(), luaSysId}
    );
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    const auto &world = IREntity::getComponent<C_WorldTransform>(id);
    EXPECT_NEAR(world.rotation_.x, 0.0f, kEps);
    EXPECT_NEAR(world.rotation_.y, 0.0f, kEps);
    EXPECT_NEAR(world.rotation_.z, 0.70710678f, kEps);
    EXPECT_NEAR(world.rotation_.w, 0.70710678f, kEps);
    expectVec3Near(world.scale_, IRMath::vec3(2.0f, 4.0f, 0.5f));
}

TEST_F(WorldTransformLuaTest, OffsetDoesNotSurviveReprojectionByPropagateTransform) {
    const IRMath::vec3 propagated(1.0f, 2.0f, 3.0f);
    auto id = IREntity::createEntity(C_LocalTransform{propagated}, C_WorldTransform{});

    const IRSystem::SystemId luaSysId = registerLuaSystem(
        R"(
        return IRSystem.registerSystem({
            name = 'OffsetWorldTranslationOnce',
            components = { IRComponent.C_WorldTransform },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local h = arch.C_WorldTransform:at(i)
                    local t = h.translation
                    h.translation = { x = t.x + 5.0, y = t.y, z = t.z }
                end
            end,
        })
    )"
    );

    const IRSystem::SystemId propagateId = IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {propagateId, luaSysId});

    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    expectVec3Near(
        IREntity::getComponent<C_WorldTransform>(id).translation_,
        propagated + IRMath::vec3(5.0f, 0.0f, 0.0f)
    );

    // A frame without the Lua system: propagation alone rewrites the world value.
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {propagateId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    expectVec3Near(IREntity::getComponent<C_WorldTransform>(id).translation_, propagated);
}

} // namespace
