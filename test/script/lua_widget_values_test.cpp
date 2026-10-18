#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style_lua.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRComponents::C_GuiPosition;
using IRComponents::C_Widget;
using IRComponents::C_WidgetButton;
using IRComponents::C_WidgetCheckbox;
using IRComponents::C_WidgetDropdown;
using IRComponents::C_WidgetLabel;
using IRComponents::C_WidgetNumberField;
using IRComponents::C_WidgetSlider;
using IRComponents::C_WidgetTextInput;
using IRComponents::WidgetKind;
using IRRender::TextSize;

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

TEST_F(LuaWidgetValues, TextInputBuildsReadsAndSets) {
    run(R"lua(input = IRGui.makeTextInput(10, 20, 120, 12, "before", 24))lua");
    const IREntity::EntityId id = entity("input");
    const C_WidgetTextInput &input = IREntity::getComponent<C_WidgetTextInput>(id);
    EXPECT_EQ(IREntity::getComponent<C_Widget>(id).kind_, WidgetKind::TEXT_INPUT);
    EXPECT_EQ(IREntity::getComponent<C_GuiPosition>(id).pos_, IRMath::ivec2(10, 20));
    EXPECT_EQ(input.maxLength_, 24);
    EXPECT_EQ(eval<std::string>("IRGui.textInputValue(input)"), "before");

    run(R"lua(IRGui.setTextInputValue(input, "after"))lua");
    EXPECT_EQ(eval<std::string>("IRGui.textInputValue(input)"), "after");

    run(R"lua(plainInput = IRGui.makeTextInput(0, 0, 40, 12))lua");
    const C_WidgetTextInput &plainInput =
        IREntity::getComponent<C_WidgetTextInput>(entity("plainInput"));
    EXPECT_EQ(eval<std::string>("IRGui.textInputValue(plainInput)"), "");
    EXPECT_EQ(plainInput.maxLength_, 0);
}

TEST_F(LuaWidgetValues, TextInputRejectsNegativeLengthBeforeCreation) {
    const IREntity::EntityId before = m_entityManager.getLiveEntityCount();
    EXPECT_NE(
        error(R"lua(IRGui.makeTextInput(0, 0, 40, 12, "", -1))lua")
            .find("maxLength must not be negative"),
        std::string::npos
    );
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), before);
}

TEST_F(LuaWidgetValues, NumberFieldBuildsReadsClampsAndSnaps) {
    run(R"lua(
        field = IRGui.makeNumberField(10, 20, 120, 18, 70, 1, 64, 1)
        free = IRGui.makeNumberField(0, 0, 80, 18, 2, 0, 10)
    )lua");
    const IREntity::EntityId id = entity("field");
    EXPECT_EQ(IREntity::getComponent<C_Widget>(id).kind_, WidgetKind::NUMBER_FIELD);
    EXPECT_EQ(IREntity::getComponent<C_GuiPosition>(id).pos_, IRMath::ivec2(10, 20));
    EXPECT_FLOAT_EQ(eval<float>("IRGui.numberFieldValue(field)"), 64.0f);
    run("IRGui.setNumberFieldValue(field, 3.6)");
    EXPECT_FLOAT_EQ(eval<float>("IRGui.numberFieldValue(field)"), 4.0f);
    run("IRGui.setNumberFieldValue(field, -5)");
    EXPECT_FLOAT_EQ(eval<float>("IRGui.numberFieldValue(field)"), 1.0f);
    run("IRGui.setNumberFieldValue(free, 2.5)");
    EXPECT_FLOAT_EQ(eval<float>("IRGui.numberFieldValue(free)"), 2.5f);
}

TEST_F(LuaWidgetValues, NumberFieldRejectsBadArgumentsBeforeCreation) {
    const IREntity::EntityId before = m_entityManager.getLiveEntityCount();
    for (const char *call : {
             "IRGui.makeNumberField(0, 0, 10, 10, math.huge, 0, 1)",
             "IRGui.makeNumberField(0, 0, 10, 10, 0, 0/0, 1)",
             "IRGui.makeNumberField(0, 0, 10, 10, 0, 0, -math.huge)",
             "IRGui.makeNumberField(0, 0, 10, 10, 0, 1, 1)",
             "IRGui.makeNumberField(0, 0, 10, 10, 0, 0, 1, 0)",
             "IRGui.makeNumberField(0, 0, 10, 10, 0, 0, 1, math.huge)",
             "IRGui.makeNumberField(0, 0, 10, 10, 0, 0, 1, 1, 7)",
         }) {
        EXPECT_FALSE(error(call).empty()) << call;
        EXPECT_EQ(m_entityManager.getLiveEntityCount(), before) << call;
    }

    run("field = IRGui.makeNumberField(0, 0, 10, 10, 0.5, 0, 1, nil, nil)");
    EXPECT_FLOAT_EQ(eval<float>("IRGui.numberFieldValue(field)"), 0.5f);
}

TEST_F(LuaWidgetValues, NumberFieldEditCycleCommitsCancelsAndFilters) {
    run("field = IRGui.makeNumberField(0, 0, 120, 18, 4, 1, 64, 1)");
    C_WidgetNumberField &field = IREntity::getComponent<C_WidgetNumberField>(entity("field"));
    field.updateEdit(true, false, "", false, false, 16);
    EXPECT_EQ(field.editBuffer_, "4");
    field.editBuffer_.clear();
    field.updateEdit(true, false, "99", false, false, 16);
    EXPECT_FLOAT_EQ(eval<float>("IRGui.numberFieldValue(field)"), 4.0f);
    field.updateEdit(false, false, "", false, false, 16);
    EXPECT_FLOAT_EQ(eval<float>("IRGui.numberFieldValue(field)"), 64.0f);

    field.setValue(4.0f);
    field.updateEdit(true, false, "", false, false, 16);
    field.editBuffer_.clear();
    field.updateEdit(true, false, "2.6", false, false, 16);
    field.updateEdit(false, false, "", false, false, 16);
    EXPECT_FLOAT_EQ(field.value_, 3.0f);

    field.updateEdit(true, false, "", false, false, 16);
    field.editBuffer_.clear();
    field.updateEdit(true, false, "1-2", false, false, 16);
    EXPECT_EQ(field.editBuffer_, "12");
    field.editBuffer_.clear();
    field.updateEdit(true, false, "1.2.3", false, false, 16);
    EXPECT_EQ(field.editBuffer_, "1.23");
    field.updateEdit(false, false, "", false, true, 16);
    EXPECT_FLOAT_EQ(field.value_, 3.0f);

    field.updateEdit(true, false, "", false, false, 16);
    field.editBuffer_ = "-";
    field.updateEdit(false, false, "", false, false, 16);
    EXPECT_FLOAT_EQ(field.value_, 3.0f);
    field.updateEdit(true, false, "", false, false, 16);
    field.editBuffer_ = "12";
    field.updateEdit(true, true, "", false, false, 16);
    EXPECT_FLOAT_EQ(field.value_, 3.0f);
}

TEST_F(LuaWidgetValues, NumberFieldFormattingPreservesUneditedValues) {
    run("free = IRGui.makeNumberField(0, 0, 120, 18, 2.555, 0, 10)");
    C_WidgetNumberField &free = IREntity::getComponent<C_WidgetNumberField>(entity("free"));
    free.updateEdit(true, false, "", false, false, 16);
    EXPECT_EQ(free.editBuffer_, "2.56");
    free.updateEdit(false, false, "", false, false, 16);
    EXPECT_FLOAT_EQ(free.value_, 2.555f);

    run("offset = IRGui.makeNumberField(0, 0, 120, 18, 1.5, 0.5, 10, 1)");
    C_WidgetNumberField &offset = IREntity::getComponent<C_WidgetNumberField>(entity("offset"));
    EXPECT_EQ(offset.formattedValue(), "1.5");
    offset.updateEdit(true, false, "", false, false, 16);
    offset.updateEdit(false, false, "", false, false, 16);
    EXPECT_FLOAT_EQ(offset.value_, 1.5f);
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
    EXPECT_NE(error("IRGui.textInputValue(slider)").find("is not a text input"), std::string::npos);
    EXPECT_NE(
        error("IRGui.numberFieldValue(slider)").find("is not a number field"),
        std::string::npos
    );

    run("IRGui.setDisabled(slider, true)");
    EXPECT_TRUE(IREntity::getComponent<C_Widget>(entity("slider")).disabled_);
    EXPECT_FALSE(eval<bool>("IRGui.isHovered(slider)"));
}

TEST_F(LuaWidgetValues, FactoriesStoreTheTrailingTextSize) {
    run(R"lua(
        S = IRText.Size.SMALL
        label = IRGui.makeLabel(0, 0, "L", nil, S)
        button = IRGui.makeButton(0, 0, 40, 12, "B", nil, S)
        slider = IRGui.makeSlider(0, 0, 40, 20, "S", 0, 1, 0, S)
        box = IRGui.makeCheckbox(0, 0, 40, 12, "C", false, S)
        dd = IRGui.makeDropdown(0, 0, 40, 12, { "A" }, 1, 12, S)
        number = IRGui.makeNumberField(0, 0, 40, 12, 1, 0, 2, 0.5, S)

        plainLabel = IRGui.makeLabel(0, 0, "L")
        plainButton = IRGui.makeButton(0, 0, 40, 12, "B")
        plainSlider = IRGui.makeSlider(0, 0, 40, 20, "S", 0, 1, 0)
        plainBox = IRGui.makeCheckbox(0, 0, 40, 12, "C")
        plainDd = IRGui.makeDropdown(0, 0, 40, 12, { "A" })
    )lua");
    EXPECT_EQ(IREntity::getComponent<C_WidgetLabel>(entity("label")).textSize_, TextSize::SMALL);
    EXPECT_EQ(IREntity::getComponent<C_WidgetButton>(entity("button")).textSize_, TextSize::SMALL);
    EXPECT_EQ(IREntity::getComponent<C_WidgetSlider>(entity("slider")).textSize_, TextSize::SMALL);
    EXPECT_EQ(IREntity::getComponent<C_WidgetCheckbox>(entity("box")).textSize_, TextSize::SMALL);
    EXPECT_EQ(IREntity::getComponent<C_WidgetDropdown>(entity("dd")).textSize_, TextSize::SMALL);
    EXPECT_EQ(
        IREntity::getComponent<C_WidgetNumberField>(entity("number")).textSize_,
        TextSize::SMALL
    );

    EXPECT_EQ(
        IREntity::getComponent<C_WidgetLabel>(entity("plainLabel")).textSize_,
        TextSize::LARGE
    );
    EXPECT_EQ(
        IREntity::getComponent<C_WidgetButton>(entity("plainButton")).textSize_,
        TextSize::LARGE
    );
    EXPECT_EQ(
        IREntity::getComponent<C_WidgetSlider>(entity("plainSlider")).textSize_,
        TextSize::LARGE
    );
    EXPECT_EQ(
        IREntity::getComponent<C_WidgetCheckbox>(entity("plainBox")).textSize_,
        TextSize::LARGE
    );
    EXPECT_EQ(
        IREntity::getComponent<C_WidgetDropdown>(entity("plainDd")).textSize_,
        TextSize::LARGE
    );
    EXPECT_EQ(IREntity::getComponent<C_WidgetLabel>(entity("label")).colorOverride_.alpha_, 0)
        << "a nil color before the size keeps the theme color";
}

TEST_F(LuaWidgetValues, TextMeasureAndGlyphStepFollowTheSize) {
    run(R"lua(
        sw, sh = IRText.measure("ABCD", IRText.Size.SMALL)
        lw, lh = IRText.measure("ABCD", IRText.Size.LARGE)
        dw, dh = IRText.measure("ABCD")
        ssx, ssy = IRGui.glyphStep(IRText.Size.SMALL)
        dsx, dsy = IRGui.glyphStep()
    )lua");
    EXPECT_EQ(eval<int>("sw"), 22);
    EXPECT_EQ(eval<int>("sh"), 7);
    EXPECT_EQ(eval<int>("lw"), 30);
    EXPECT_EQ(eval<int>("lh"), 11);
    EXPECT_EQ(eval<int>("dw"), 30);
    EXPECT_EQ(eval<int>("ssx"), 6);
    EXPECT_EQ(eval<int>("ssy"), 10);
    EXPECT_EQ(eval<int>("dsx"), 8);
    EXPECT_EQ(eval<int>("dsy"), 14);
}

// A 140-trixel panel fits 14 SMALL rows and 10 LARGE ones.
TEST_F(LuaWidgetValues, SmallRowsFitProportionallyMoreLines) {
    run(R"lua(
        function linesHeight(count, size)
            local _, h = IRText.measure(string.rep("ROW\n", count - 1) .. "ROW", size)
            return h
        end
    )lua");
    EXPECT_LE(eval<int>("linesHeight(14, IRText.Size.SMALL)"), 140);
    EXPECT_GT(eval<int>("linesHeight(15, IRText.Size.SMALL)"), 140);
    EXPECT_LE(eval<int>("linesHeight(10, IRText.Size.LARGE)"), 140);
    EXPECT_GT(eval<int>("linesHeight(11, IRText.Size.LARGE)"), 140);
}

TEST_F(LuaWidgetValues, ABadTextSizeRaisesAndCreatesNothing) {
    const IREntity::EntityId before = m_entityManager.getLiveEntityCount();
    for (const char *call : {
             "IRGui.makeLabel(0, 0, 'L', nil, 'SMALL')",
             "IRGui.makeButton(0, 0, 10, 10, 'B', nil, 7)",
             "IRGui.makeSlider(0, 0, 10, 10, 'S', 0, 1, 0, 'SMALL')",
             "IRGui.makeCheckbox(0, 0, 10, 10, 'C', false, 7)",
             "IRGui.makeDropdown(0, 0, 10, 10, { 'A' }, 1, 10, 0.5)",
             "IRText.measure('A', 'SMALL')",
             "IRGui.glyphStep(7)",
         }) {
        EXPECT_NE(
            error(call).find("size must be IRText.Size.LARGE or IRText.Size.SMALL"),
            std::string::npos
        ) << call;
    }
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), before);
}

// Numbers no lua_Integer can hold, and the near misses either side of the two
// members, raise the same error rather than reaching an integer conversion.
TEST_F(LuaWidgetValues, ANonRepresentableTextSizeRaises) {
    const IREntity::EntityId before = m_entityManager.getLiveEntityCount();
    for (const char *size :
         {"math.huge", "-math.huge", "0/0", "2^63", "-2^63", "1e308", "-1", "2", "1.5", "-0.5"}) {
        for (const std::string &call : {
                 std::string("IRText.measure('A', ") + size + ")",
                 std::string("IRGui.glyphStep(") + size + ")",
                 std::string("IRGui.makeLabel(0, 0, 'L', nil, ") + size + ")",
             }) {
            EXPECT_NE(
                error(call.c_str()).find("size must be IRText.Size.LARGE or IRText.Size.SMALL"),
                std::string::npos
            ) << call;
        }
    }
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), before);
}

TEST_F(LuaWidgetValues, TextStyleTextSizeIsRangeChecked) {
    IRScript::bindLuaType<IRComponents::C_TextStyle>(m_lua);
    run(R"lua(
        style = C_TextStyle.new()
        before = style.textSize
        style.textSize = IRText.Size.SMALL
    )lua");
    EXPECT_EQ(eval<int>("before"), static_cast<int>(TextSize::LARGE));
    EXPECT_EQ(eval<int>("style.textSize"), static_cast<int>(TextSize::SMALL));
    EXPECT_NE(
        error("style.textSize = 'LARGE'").find("C_TextStyle.textSize: size must be IRText.Size"),
        std::string::npos
    );
    EXPECT_EQ(eval<int>("style.textSize"), static_cast<int>(TextSize::SMALL));
}

} // namespace
