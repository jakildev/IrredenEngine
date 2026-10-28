#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRComponents::C_Widget;
using IRComponents::C_WidgetColorSwatch;
using IRComponents::WidgetKind;

// The IRGui colour-swatch surface through the sol2 seam: the factory, its
// options, the readers and setters, and the errors a wrong id raises.
//
// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaWidgetColorSwatch : public testing::Test {
  protected:
    LuaWidgetColorSwatch() {
        m_lua.bindLuaDrivenEcs();
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    void run(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    }

    std::string error(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        sol::error err = result;
        return err.what();
    }

    IREntity::EntityId entity(const char *global) {
        return m_lua.lua()[global].get<IREntity::EntityId>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(LuaWidgetColorSwatch, FactoryBuildsTheWidgetWithItsColorAndSelection) {
    run(R"lua(
        swatch = IRGui.makeColorSwatch(4, 6, 12, 12, { 10, 20, 30 })
        picked = IRGui.makeColorSwatch(20, 6, 12, 12, { r = 1, g = 2, b = 3, a = 4 }, { selected = true })
    )lua");
    const IREntity::EntityId swatch = entity("swatch");
    const C_Widget &widget = IREntity::getComponent<C_Widget>(swatch);
    EXPECT_EQ(widget.kind_, WidgetKind::COLOR_SWATCH);
    EXPECT_EQ(widget.size_, IRMath::ivec2(12, 12));
    const C_WidgetColorSwatch &data = IREntity::getComponent<C_WidgetColorSwatch>(swatch);
    EXPECT_EQ(data.color_.toPackedRGBA(), IRMath::Color(10, 20, 30, 255).toPackedRGBA());
    EXPECT_FALSE(data.selected_);
    const C_WidgetColorSwatch &picked = IREntity::getComponent<C_WidgetColorSwatch>(entity("picked"));
    EXPECT_TRUE(picked.selected_);
    EXPECT_EQ(picked.color_.toPackedRGBA(), IRMath::Color(1, 2, 3, 4).toPackedRGBA());
}

TEST_F(LuaWidgetColorSwatch, ReadersAndSettersRoundTrip) {
    run(R"lua(
        local id = IRGui.makeColorSwatch(0, 0, 8, 8, { 5, 6, 7 })
        local r, g, b, a = IRGui.colorSwatchColor(id)
        assert(r == 5 and g == 6 and b == 7 and a == 255, "the colour reads back")
        IRGui.setColorSwatchColor(id, { 9, 8, 7, 6 })
        r, g, b, a = IRGui.colorSwatchColor(id)
        assert(r == 9 and g == 8 and b == 7 and a == 6, "the colour follows the setter")
        assert(IRGui.colorSwatchSelected(id) == false)
        IRGui.setColorSwatchSelected(id, true)
        assert(IRGui.colorSwatchSelected(id) == true, "the selection follows the setter")
    )lua");
}

TEST_F(LuaWidgetColorSwatch, RejectsBadOptionsAndIds) {
    EXPECT_NE(
        error("IRGui.makeColorSwatch(0, 0, 8, 8, { 1, 1, 1 }, { colour = true })").find("unknown option"),
        std::string::npos
    );
    EXPECT_NE(
        error("IRGui.makeColorSwatch(0, 0, 8, 8, { 1, 1, 1 }, { selected = 1 })").find("selected"),
        std::string::npos
    );
    EXPECT_NE(
        error("IRGui.makeColorSwatch(0, 0, 8, 8, { 1, 1, 1 }, { onClick = 1 })").find("onClick"),
        std::string::npos
    );
    EXPECT_NE(error("IRGui.colorSwatchColor(123456)").find("not a color swatch"), std::string::npos);
    run("label = IRGui.makeLabel(0, 0, 'X')");
    EXPECT_NE(error("IRGui.setColorSwatchSelected(label, true)").find("not a color swatch"), std::string::npos);
}

} // namespace
