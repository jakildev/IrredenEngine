#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/update/components/component_collision_response_lua.hpp>

#include <sol/sol.hpp>

#include <tuple>

// C_CollisionResponse through the sol2 seam: the trait registers the type on
// IRComponent, a batch factory attaches it with the constructors clamping
// restitution and friction, and IRSystem.SystemName carries the system.

namespace {

using IRComponents::C_CollisionResponse;
using IREntity::EntityId;

class LuaCollisionResponseTest : public testing::Test {
  protected:
    LuaCollisionResponseTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerType<IRMath::ivec3, IRMath::ivec3(int, int, int)>("ivec3");
        m_lua.registerTypesFromTraits<C_CollisionResponse>();
        m_lua.registerCreateEntityBatchFunction<C_CollisionResponse>("createResponders");
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaCollisionResponseTest, TraitRegistersTheComponent) {
    auto result = m_lua.lua().safe_script(
        "return IRComponent.C_CollisionResponse ~= nil",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(LuaCollisionResponseTest, BatchFactoryAttachesClampedResponses) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        local bouncy = IREntity.createResponders(
            ivec3.new(1, 1, 1),
            function() return C_CollisionResponse.new(1.5, -0.2, true) end
        )
        local floored = IREntity.createResponders(
            ivec3.new(1, 1, 1),
            function() return C_CollisionResponse.new(0.5, 0.25, false, 12.0) end
        )
        return bouncy[1].entity, floored[1].entity
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const auto [bouncyId, flooredId] = result.get<std::tuple<EntityId, EntityId>>();

    const C_CollisionResponse &bouncy = IREntity::getComponent<C_CollisionResponse>(bouncyId);
    EXPECT_EQ(bouncy.restitution_, 1.0f);
    EXPECT_EQ(bouncy.friction_, 0.0f);
    EXPECT_TRUE(bouncy.restOnSupport_);
    EXPECT_FALSE(bouncy.hasFloor_);

    const C_CollisionResponse &floored = IREntity::getComponent<C_CollisionResponse>(flooredId);
    EXPECT_EQ(floored.restitution_, 0.5f);
    EXPECT_EQ(floored.friction_, 0.25f);
    EXPECT_TRUE(floored.hasFloor_);
    EXPECT_EQ(floored.floorZ_, 12.0f);
    EXPECT_FALSE(floored.restOnSupport_);
}

TEST_F(LuaCollisionResponseTest, RestSpeedClampsAndRestingIsReadOnly) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        local response = C_CollisionResponse.new()
        local default = response.restSpeed
        response.restSpeed = -3.0
        local clamped = response.restSpeed
        response.restSpeed = 4.0
        local writeResting = pcall(function() response.resting = true end)
        return default, clamped, response.restSpeed, response.resting, writeResting
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const auto [defaultSpeed, clamped, raised, resting, wroteResting] =
        result.get<std::tuple<float, float, float, bool, bool>>();
    EXPECT_EQ(defaultSpeed, IRComponents::kCollisionRestSpeedDefault);
    EXPECT_EQ(clamped, 0.0f);
    EXPECT_EQ(raised, 4.0f);
    EXPECT_FALSE(resting);
    EXPECT_FALSE(wroteResting);
}

TEST_F(LuaCollisionResponseTest, SystemNameCarriesCollisionResponse) {
    auto result = m_lua.lua().safe_script(
        "return IRSystem.SystemName.COLLISION_RESPONSE",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    EXPECT_EQ(result.get<int>(), static_cast<int>(IRSystem::COLLISION_RESPONSE));
}

} // namespace
