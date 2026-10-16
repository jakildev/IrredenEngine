#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRComponents::C_GuiPosition;
using IRComponents::C_Widget;
using IRComponents::C_WidgetCheckbox;
using IRComponents::C_WidgetDropdown;
using IRComponents::C_WidgetSlider;
using IRComponents::WidgetKind;

// Drives the IRGui value widgets (slider, checkbox, dropdown) from Lua: their
// factories, the polled readers an editor uses each frame, the setters, and
// the errors a wrong id or bad argument raises. The WIDGET_APPLY_* systems
// that move the values under the mouse are covered by their own tests; here
// the component is written the way they would.
//
// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaWidgetValues : public testing::Test {
  protected:
    LuaWidgetValues() {
        m_lua.bindLuaDrivenEcs();
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    void run(const char *script) {
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    }

    template <typename T> T eval(const char *expr) {
        auto result =
            m_lua.lua().safe_script(std::string("return ") + expr, sol::script_pass_on_error);
        EXPECT_TRUE(result.valid());
        return result.get<T>();
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

TEST_F(LuaWidgetValues, SliderBuildsReadsAndClamps) {
    run(R"lua(slider = IRGui.makeSlider(10, 20, 120, 12, "BPM", 20, 240, 60))lua");
    const IREntity::EntityId id = entity("slider");
    const C_Widget &widget = IREntity::getComponent<C_Widget>(id);
    EXPECT_EQ(widget.kind_, WidgetKind::SLIDER);
    EXPECT_EQ(widget.size_, IRMath::ivec2(120, 12));
    EXPECT_EQ(IREntity::getComponent<C_GuiPosition>(id).pos_, IRMath::ivec2(10, 20));
    EXPECT_FLOAT_EQ(eval<float>("IRGui.sliderValue(slider)"), 60.0f);

    IREntity::getComponent<C_WidgetSlider>(id).currentValue_ = 90.0f; // the mouse moved it
    EXPECT_FLOAT_EQ(eval<float>("IRGui.sliderValue(slider)"), 90.0f);

    run("IRGui.setSliderValue(slider, 999)");
    EXPECT_FLOAT_EQ(eval<float>("IRGui.sliderValue(slider)"), 240.0f);
    run("s2 = IRGui.makeSlider(0, 0, 10, 10, 'X', 0, 1, 5)");
    EXPECT_FLOAT_EQ(eval<float>("IRGui.sliderValue(s2)"), 1.0f)
        << "an initial value outside the range clamps";

    EXPECT_NE(
        error("IRGui.makeSlider(0, 0, 10, 10, 'X', 1, 1, 1)").find("min must be below max"),
        std::string::npos
    );
}

TEST_F(LuaWidgetValues, CheckboxBuildsReadsAndSets) {
    run(R"lua(
        box = IRGui.makeCheckbox(0, 0, 80, 12, "LOCK", true)
        plain = IRGui.makeCheckbox(0, 20, 80, 12, "OFF")
    )lua");
    EXPECT_EQ(IREntity::getComponent<C_Widget>(entity("box")).kind_, WidgetKind::CHECKBOX);
    EXPECT_TRUE(eval<bool>("IRGui.checkboxState(box)"));
    EXPECT_FALSE(eval<bool>("IRGui.checkboxState(plain)"));
    run("IRGui.setCheckboxState(box, false)");
    EXPECT_FALSE(IREntity::getComponent<C_WidgetCheckbox>(entity("box")).checked_);
}

TEST_F(LuaWidgetValues, DropdownIndicesAreOneBasedInLua) {
    run(R"lua(dd = IRGui.makeDropdown(0, 0, 100, 14, { "RAMP", "TRIANGLE", "SINE" }, 2))lua");
    const IREntity::EntityId id = entity("dd");
    const C_WidgetDropdown &dd = IREntity::getComponent<C_WidgetDropdown>(id);
    EXPECT_EQ(IREntity::getComponent<C_Widget>(id).kind_, WidgetKind::DROPDOWN);
    ASSERT_EQ(dd.items_.size(), 3u);
    EXPECT_EQ(dd.items_[1], "TRIANGLE");
    EXPECT_EQ(dd.selectedIndex_, 1);
    EXPECT_EQ(dd.itemHeight_, 14) << "the rows default to the widget's own height";
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 2);

    run("IRGui.setDropdownSelected(dd, 3)");
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 3);
    run("IRGui.setDropdownSelected(dd, 99)");
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 3)
        << "an index past the items clamps to the last";

    run(R"lua(IRGui.setDropdownItems(dd, { "A", "B" }, 2))lua");
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 2);
    EXPECT_EQ(IREntity::getComponent<C_WidgetDropdown>(id).items_[0], "A");
    run("IRGui.setDropdownItems(dd, {})");
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 0)
        << "an empty dropdown has nothing selected";
    EXPECT_FALSE(eval<bool>("IRGui.isDropdownOpen(dd)"));
}

TEST_F(LuaWidgetValues, SetDropdownOpenOpensAndClosesTheStrip) {
    run(R"lua(dd = IRGui.makeDropdown(0, 0, 100, 14, { "A", "B" }))lua");
    run("IRGui.setDropdownOpen(dd, true)");
    EXPECT_TRUE(eval<bool>("IRGui.isDropdownOpen(dd)"));
    run("IRGui.setDropdownOpen(dd, false)");
    EXPECT_FALSE(eval<bool>("IRGui.isDropdownOpen(dd)"));
}

TEST_F(LuaWidgetValues, DropdownZeroMeansNothingSelected) {
    run(R"lua(dd = IRGui.makeDropdown(0, 0, 100, 14, { "A", "B", "C" }, 0))lua");
    const IREntity::EntityId id = entity("dd");
    EXPECT_EQ(IREntity::getComponent<C_WidgetDropdown>(id).selectedIndex_, -1);
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 0);

    run("IRGui.setDropdownSelected(dd, 2)");
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 2);
    run("IRGui.setDropdownSelected(dd, 0)");
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 0) << "the setter clears the selection";
    EXPECT_EQ(IREntity::getComponent<C_WidgetDropdown>(id).selectedIndex_, -1);

    run(R"lua(IRGui.setDropdownItems(dd, { "X", "Y" }, 0))lua");
    EXPECT_EQ(eval<int>("IRGui.dropdownSelected(dd)"), 0);
}

TEST_F(LuaWidgetValues, MalformedDropdownItemsCreateNoWidget) {
    const IREntity::EntityId before = m_entityManager.getLiveEntityCount();
    EXPECT_NE(
        error("IRGui.makeDropdown(0, 0, 10, 10, { 'A', 3 })").find("list of strings"),
        std::string::npos
    );
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), before)
        << "a rejected item list leaves no dropdown or hitbox behind";
}

TEST_F(LuaWidgetValues, ReadersRejectTheWrongKindOfWidget) {
    run(R"lua(
        slider = IRGui.makeSlider(0, 0, 10, 10, "S", 0, 1, 0)
        label = IRGui.makeLabel(0, 0, "L")
    )lua");
    EXPECT_NE(error("IRGui.checkboxState(slider)").find("is not a checkbox"), std::string::npos);
    EXPECT_NE(error("IRGui.dropdownSelected(label)").find("is not a dropdown"), std::string::npos);
    EXPECT_NE(error("IRGui.sliderValue(12345)").find("is not a slider"), std::string::npos);

    run("IRGui.setDisabled(slider, true)");
    EXPECT_TRUE(IREntity::getComponent<C_Widget>(entity("slider")).disabled_);
    EXPECT_FALSE(eval<bool>("IRGui.isHovered(slider)"));
}

} // namespace
