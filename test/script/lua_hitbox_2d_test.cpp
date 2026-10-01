#include <gtest/gtest.h>

#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/input/components/component_hitbox_2d_lua.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

namespace {

class LuaHitBox2DTest : public testing::Test {
  protected:
    LuaHitBox2DTest() {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<IRComponents::C_HitBox2D>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
};

TEST_F(LuaHitBox2DTest, ConstructorsAndPropertiesAreBound) {
    auto result = m_lua.lua().safe_script(
        R"lua(
        local empty = C_HitBox2D.new()
        assert(empty:halfWidth() == 0)
        assert(empty:halfHeight() == 0)

        local hitbox = C_HitBox2D.new(18, 10)
        assert(hitbox:halfWidth() == 9)
        assert(hitbox:halfHeight() == 5)
        hitbox.padding = 7
        hitbox.hovered = true
        hitbox.enabled = false
        return hitbox.padding == 7 and hitbox.hovered and not hitbox.enabled
    )lua",
        sol::script_pass_on_error
    );

    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    EXPECT_TRUE(result.get<bool>());
}

} // namespace
