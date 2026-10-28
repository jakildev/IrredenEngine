#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/command/command_manager.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style_lua.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/widgets.hpp>
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
using IRComponents::C_WidgetList;
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
        m_lua.bindLuaCommands();
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
    IRCommand::CommandManager m_commandManager;
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

    const C_WidgetSlider &legacy = IREntity::getComponent<C_WidgetSlider>(id);
    EXPECT_FLOAT_EQ(legacy.defaultValue_, 60.0f);
    EXPECT_FLOAT_EQ(legacy.step_, 2.2f);
    EXPECT_EQ(legacy.fieldId_, IREntity::kNullEntity);
}

TEST_F(LuaWidgetValues, SliderOptionsPopulateAndRejectBeforeCreation) {
    run(R"lua(
        field = IRGui.makeNumberField(0, 20, 60, 18, 0.06, 0, 1, 0.1)
        slider = IRGui.makeSlider(
            0, 0, 100, 18, "GAIN", 0, 1, 0.75, nil,
            { default = 0.25, step = 0.1, fieldId = field }
        )
    )lua");
    const C_WidgetSlider &slider = IREntity::getComponent<C_WidgetSlider>(entity("slider"));
    EXPECT_FLOAT_EQ(slider.currentValue_, 0.75f);
    EXPECT_FLOAT_EQ(slider.defaultValue_, 0.25f);
    EXPECT_FLOAT_EQ(slider.step_, 0.1f);
    EXPECT_EQ(slider.fieldId_, entity("field"));

    const C_WidgetNumberField &field = IREntity::getComponent<C_WidgetNumberField>(entity("field"));
    EXPECT_FLOAT_EQ(field.value_, 0.1f);
    EXPECT_FLOAT_EQ(field.defaultValue_, 0.1f)
        << "the authored default is the post-snap creation value";

    const IREntity::EntityId before = m_entityManager.getLiveEntityCount();
    run("label = IRGui.makeLabel(0, 0, 'not a number field')");
    EXPECT_GT(m_entityManager.getLiveEntityCount(), before);
    for (const char *call : {
             "IRGui.makeSlider(0,0,10,10,'S',0,1,0,nil,{step=0})",
             "IRGui.makeSlider(0,0,10,10,'S',0,1,0,nil,{step=math.huge})",
             "IRGui.makeSlider(0,0,10,10,'S',0,1,0,nil,{mystery=1})",
             "IRGui.makeSlider(0,0,10,10,'S',0,1,0,nil,{fieldId=12345})",
             "IRGui.makeSlider(0,0,10,10,'S',0,1,0,nil,{fieldId=label})",
         }) {
        const IREntity::EntityId count = m_entityManager.getLiveEntityCount();
        EXPECT_FALSE(error(call).empty()) << call;
        EXPECT_EQ(m_entityManager.getLiveEntityCount(), count) << call;
    }
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

TEST_F(LuaWidgetValues, SetDropdownOpenRevealsTheSelectedItem) {
    IREntity::singleton<IRComponents::C_WidgetInputFrame>().guiCanvasSize_ =
        IRMath::ivec2(640, 720);
    run(R"lua(
        local items = {}
        for i = 1, 20 do items[i] = "ITEM " .. i end
        dd = IRGui.makeDropdown(0, 0, 100, 14, items, 16)
    )lua");
    const IREntity::EntityId id = entity("dd");
    const C_WidgetDropdown::StripLayout layout = IRPrefab::Widget::dropdownStripLayout(id);
    ASSERT_EQ(layout.rows_, IRComponents::kDropdownMaxVisibleRows);
    EXPECT_EQ(IREntity::getComponent<C_WidgetDropdown>(id).topIndex(layout), 0);
    run("IRGui.setDropdownOpen(dd, true)");
    const C_WidgetDropdown &dropdown = IREntity::getComponent<C_WidgetDropdown>(id);
    EXPECT_EQ(dropdown.topIndex(layout), 15 - layout.rows_ + 1)
        << "selected item 16 (index 15) lands on the window's last row";
}

// The reveal reads the placed window: near the canvas bottom the strip opens
// upward, clipped to the rows that fit, and the selection lands inside it.
TEST_F(LuaWidgetValues, SetDropdownOpenRevealsIntoTheClippedWindow) {
    IREntity::singleton<IRComponents::C_WidgetInputFrame>().guiCanvasSize_ =
        IRMath::ivec2(640, 100);
    run(R"lua(
        local items = {}
        for i = 1, 40 do items[i] = "ITEM " .. i end
        dd = IRGui.makeDropdown(0, 55, 100, 20, items, 30, 10)
        IRGui.setDropdownOpen(dd, true)
    )lua");
    const IREntity::EntityId id = entity("dd");
    const C_WidgetDropdown::StripLayout layout = IRPrefab::Widget::dropdownStripLayout(id);
    ASSERT_EQ(layout.rows_, 5);
    EXPECT_LT(layout.offsetY_, 0);
    EXPECT_EQ(IREntity::getComponent<C_WidgetDropdown>(id).topIndex(layout), 29 - 5 + 1);
}

TEST_F(LuaWidgetValues, DropdownStripReportsTheOpenStripRectAndNilWhileClosed) {
    IREntity::singleton<IRComponents::C_WidgetInputFrame>().guiCanvasSize_ =
        IRMath::ivec2(640, 100);
    run(R"lua(
        local items = {}
        for i = 1, 40 do items[i] = "ITEM " .. i end
        dd = IRGui.makeDropdown(0, 55, 100, 20, items, 30, 10)
    )lua");
    const IREntity::EntityId id = entity("dd");
    EXPECT_TRUE(eval<bool>("IRGui.dropdownStrip(dd) == nil")) << "a closed dropdown has no strip";

    run("IRGui.setDropdownOpen(dd, true)");
    const C_WidgetDropdown &dropdown = IREntity::getComponent<C_WidgetDropdown>(id);
    const IRPrefab::GuiRect expected = IRPrefab::WidgetText::dropdownStripRect(
        IREntity::getComponent<C_Widget>(id),
        dropdown,
        IREntity::getComponent<IRComponents::C_GuiPosition>(id).pos_,
        IRPrefab::Widget::dropdownStripLayout(id)
    );
    ASSERT_GT(expected.size_.y, 0);
    EXPECT_LE(expected.pos_.y + expected.size_.y, 55) << "the strip opens above the header";
    EXPECT_EQ(eval<int>("IRGui.dropdownStrip(dd).x"), expected.pos_.x);
    EXPECT_EQ(eval<int>("IRGui.dropdownStrip(dd).y"), expected.pos_.y);
    EXPECT_EQ(eval<int>("IRGui.dropdownStrip(dd).w"), expected.size_.x);
    EXPECT_EQ(eval<int>("IRGui.dropdownStrip(dd).h"), expected.size_.y);

    run("IRGui.setDropdownOpen(dd, false)");
    EXPECT_TRUE(eval<bool>("IRGui.dropdownStrip(dd) == nil"));
}

TEST_F(LuaWidgetValues, ListDefaultsToNoSelectionAndReadsOneBased) {
    run(R"lua(list = IRGui.makeList(0, 0, 100, 54, { "A", "B", "C", "D" }))lua");
    const IREntity::EntityId id = entity("list");
    const C_WidgetList &list = IREntity::getComponent<C_WidgetList>(id);
    EXPECT_EQ(list.selectedIndex_, -1);
    EXPECT_EQ(list.itemHeight_, C_WidgetList{}.itemHeight_) << "row height keeps the C++ default";
    EXPECT_EQ(list.textSize_, TextSize::LARGE);
    EXPECT_EQ(IREntity::getComponent<C_Widget>(id).kind_, WidgetKind::LIST);
    EXPECT_EQ(eval<int>("IRGui.listSelected(list)"), 0);

    run("IRGui.setListSelected(list, 3)");
    EXPECT_EQ(list.selectedIndex_, 2);
    EXPECT_EQ(eval<int>("IRGui.listSelected(list)"), 3);
    run("IRGui.setListSelected(list, 0)");
    EXPECT_EQ(eval<int>("IRGui.listSelected(list)"), 0) << "0 clears the selection";
    run("IRGui.setListSelected(list, 99)");
    EXPECT_EQ(eval<int>("IRGui.listSelected(list)"), 4) << "past the items clamps to the last";
}

TEST_F(LuaWidgetValues, ListSelectionRevealsOnlyWhenItChanges) {
    // 54 trixels of 18-trixel rows: a three-row window over 60 rows.
    run(R"lua(
        local items = {}
        for i = 1, 60 do items[i] = "ROW " .. i end
        list = IRGui.makeList(0, 0, 100, 54, items, 40, IRText.Size.SMALL)
    )lua");
    const IREntity::EntityId id = entity("list");
    C_WidgetList &list = IREntity::getComponent<C_WidgetList>(id);
    EXPECT_EQ(list.textSize_, TextSize::SMALL);
    EXPECT_EQ(eval<int>("IRGui.listSelected(list)"), 40);
    EXPECT_EQ(list.topIndex(54), 39 - 3 + 1) << "the initial selection is revealed";

    list.scrollBy(5, 54); // the wheel moves the window off the selection
    run("IRGui.setListSelected(list, 40)");
    EXPECT_EQ(list.topIndex(54), 42) << "an unchanged selection leaves the wheel's window";
    run("IRGui.setListSelected(list, 2)");
    EXPECT_EQ(list.topIndex(54), 1) << "a changed selection is revealed";
}

TEST_F(LuaWidgetValues, SetListItemsKeepsAValidSelectionAndClearsAnInvalidOne) {
    run(R"lua(
        local items = {}
        for i = 1, 20 do items[i] = "ROW " .. i end
        list = IRGui.makeList(0, 0, 100, 54, items, 15)
    )lua");
    const IREntity::EntityId id = entity("list");
    C_WidgetList &list = IREntity::getComponent<C_WidgetList>(id);
    list.scrollOffset_ = 4;

    run(R"lua(
        local items = {}
        for i = 1, 30 do items[i] = "NEW " .. i end
        IRGui.setListItems(list, items)
    )lua");
    EXPECT_EQ(list.items_.size(), 30u);
    EXPECT_EQ(list.items_[0], "NEW 1");
    EXPECT_EQ(eval<int>("IRGui.listSelected(list)"), 15) << "row 15 still exists";
    EXPECT_EQ(list.topIndex(54), 4) << "the window is not pulled back to the selection";

    list.scrollOffset_ = 25;
    run(R"lua(IRGui.setListItems(list, { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J" }))lua");
    EXPECT_EQ(eval<int>("IRGui.listSelected(list)"), 0) << "row 15 is gone";
    EXPECT_EQ(list.scrollOffset_, 7) << "the window is clamped to the shorter list";

    run("IRGui.setListItems(list, {})");
    EXPECT_TRUE(list.items_.empty());
    EXPECT_EQ(list.scrollOffset_, 0);
}

TEST_F(LuaWidgetValues, ListBindingsRejectWrongWidgetsAndArguments) {
    run(R"lua(
        button = IRGui.makeButton(0, 0, 80, 20, "BUTTON")
        list = IRGui.makeList(0, 30, 100, 54, { "A" })
    )lua");
    for (const char *call : {
             "IRGui.listSelected(button)",
             "IRGui.setListSelected(button, 1)",
             "IRGui.setListItems(button, { 'A' })",
             "IRGui.listSelected(999999)",
             "IRGui.setListItems(list, { 'A', 2 })",
         }) {
        EXPECT_NE(error(call).find("IRGui."), std::string::npos) << call;
    }
    const IREntity::EntityId before = m_entityManager.getLiveEntityCount();
    EXPECT_FALSE(error("IRGui.makeList(0, 0, 10, 10, { 'A', {} })").empty());
    EXPECT_FALSE(error("IRGui.makeList(0, 0, 10, 10, { 'A' }, 1, 99)").empty());
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), before) << "a malformed call creates no widget";
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

// The queue half of IRGui.focus; WIDGET_INPUT::beginTick consumes the request.
TEST_F(LuaWidgetValues, FocusQueuesOneRequestTheLatestWins) {
    EXPECT_EQ(IRPrefab::Widget::takeFocusRequest(), IREntity::kNullEntity);
    run(R"lua(
        first = IRGui.makeTextInput(0, 0, 40, 12)
        second = IRGui.makeTextInput(0, 20, 40, 12)
        IRGui.focus(first)
        IRGui.focus(second)
    )lua");
    EXPECT_EQ(IRPrefab::Widget::takeFocusRequest(), entity("second"));
    EXPECT_EQ(IRPrefab::Widget::takeFocusRequest(), IREntity::kNullEntity);
}

TEST_F(LuaWidgetValues, FocusRejectsANonWidgetAndQueuesNothing) {
    EXPECT_NE(error("IRGui.focus(12345)").find("is not a live widget"), std::string::npos);
    EXPECT_EQ(IRPrefab::Widget::takeFocusRequest(), IREntity::kNullEntity);
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

    field.setValue(9.0f);
    field.updateEdit(true, false, "", false, false, 16);
    field.editBuffer_.clear();
    field.updateEdit(true, false, "", true, false, 16);
    EXPECT_FLOAT_EQ(field.value_, 4.0f);
    EXPECT_EQ(field.editBuffer_, "4");
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

TEST_F(LuaWidgetValues, HotkeySurfaceValidatesAndMirrorsHintVisibility) {
    run(R"lua(
        button = IRGui.makeButton(0, 0, 80, 20, "BUTTON")
        plain = IRGui.makeButton(0, 30, 80, 20, "PLAIN")
        label = IRGui.makeLabel(0, 60, "LABEL")
        IRGui.setHotkey(button, { key = IRInput.Key.M, mods = IRInput.Modifier.ALT, hint = "M" })
        IRGui.setHotkey(plain, { key = IRInput.Key.N })
    )lua");
    const auto &hotkey = IREntity::getComponent<IRComponents::C_WidgetHotkey>(entity("button"));
    EXPECT_EQ(hotkey.key_, IRInput::kKeyButtonM);
    EXPECT_EQ(hotkey.modifiers_, IRInput::kModifierAlt);
    EXPECT_EQ(hotkey.hint_, "M");
    const auto &plain = IREntity::getComponent<IRComponents::C_WidgetHotkey>(entity("plain"));
    EXPECT_EQ(plain.modifiers_, IRInput::kModifierNone);
    EXPECT_TRUE(plain.hint_.empty());

    auto &frame = IREntity::singleton<IRComponents::C_WidgetInputFrame>();
    frame.hintsVisible_ = true;
    EXPECT_TRUE(eval<bool>("IRGui.hintsVisible()"));
    run("IRGui.clearHotkey(button)");
    EXPECT_FALSE(IREntity::getComponentOptional<IRComponents::C_WidgetHotkey>(entity("button")));

    for (const char *call : {
             "IRGui.setHotkey(button, { key = 'M' })",
             "IRGui.setHotkey(button, { key = IRInput.Key.SPACE })",
             "IRGui.setHotkey(button, { key = IRInput.Key.UP, mods = IRInput.Modifier.CONTROL })",
             "IRGui.setHotkey(button, { key = IRInput.Key.M, mods = 128 })",
             "IRGui.setHotkey(button, { key = IRInput.Key.M, surprise = true })",
             "IRGui.setHotkey(button, { key = IRInput.Key.MOUSE_LEFT })",
             "IRGui.setHotkey(button, { key = 0 })",
             "IRGui.setHotkey(button, { key = 999999 })",
             "IRGui.setHotkey(label, { key = IRInput.Key.L })",
             "IRGui.setHotkey(999999, { key = IRInput.Key.L })",
         }) {
        EXPECT_FALSE(error(call).empty()) << call;
    }
}

} // namespace
