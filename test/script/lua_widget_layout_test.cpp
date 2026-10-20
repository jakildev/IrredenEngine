#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/gui_layout_audit.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

// Drives the layout audit through IRGui: `layoutReport` over Lua-built widgets
// at an explicit canvas size (the headless form; a test has no GUI canvas),
// the `LayoutKind` table, and `setCoversBelow` declaring intent.
//
// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaWidgetLayout : public testing::Test {
  protected:
    LuaWidgetLayout() {
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

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(LuaWidgetLayout, LayoutKindMirrorsTheFindingKinds) {
    using IRPrefab::GuiAudit::FindingKind;
    EXPECT_EQ(eval<int>("IRGui.LayoutKind.OVERLAP"), static_cast<int>(FindingKind::OVERLAP));
    EXPECT_EQ(
        eval<int>("IRGui.LayoutKind.TEXT_OVER_WIDGET"),
        static_cast<int>(FindingKind::TEXT_OVER_WIDGET)
    );
    EXPECT_EQ(
        eval<int>("IRGui.LayoutKind.TEXT_CLIPPED"),
        static_cast<int>(FindingKind::TEXT_CLIPPED)
    );
    EXPECT_EQ(
        eval<int>("IRGui.LayoutKind.OUTSIDE_PARENT"),
        static_cast<int>(FindingKind::OUTSIDE_PARENT)
    );
    EXPECT_EQ(eval<int>("IRGui.LayoutKind.OFFSCREEN"), static_cast<int>(FindingKind::OFFSCREEN));
}

TEST_F(LuaWidgetLayout, ReportNamesAnOverlappingPairAndADeclaredCoverDropsIt) {
    run(R"lua(
        a = IRGui.makeButton(10, 10, 40, 14, "")
        b = IRGui.makeButton(30, 10, 40, 14, "")
        report = IRGui.layoutReport(400, 300)
    )lua");

    EXPECT_EQ(eval<int>("#report"), 1);
    EXPECT_TRUE(eval<bool>("report[1].kind == IRGui.LayoutKind.OVERLAP"));
    EXPECT_TRUE(eval<bool>("report[1].a == a and report[1].b == b"));
    EXPECT_EQ(eval<int>("report[1].x"), 30);
    EXPECT_EQ(eval<int>("report[1].y"), 10);
    EXPECT_EQ(eval<int>("report[1].w"), 20);
    EXPECT_EQ(eval<int>("report[1].h"), 14);

    run(R"lua(
        IRGui.setCoversBelow(a, true)
        covered = IRGui.layoutReport(400, 300)
    )lua");

    EXPECT_EQ(eval<int>("#covered"), 0);
}

TEST_F(LuaWidgetLayout, ReportWithNoCanvasSizeAndNoGuiCanvasIsEmpty) {
    run(R"lua(
        IRGui.makeButton(10, 10, 40, 14, "")
        IRGui.makeButton(30, 10, 40, 14, "")
    )lua");

    EXPECT_EQ(eval<int>("#IRGui.layoutReport()"), 0);
}

TEST_F(LuaWidgetLayout, ReportRejectsAHalfCanvasSize) {
    const std::string message = error("IRGui.layoutReport(400)");

    EXPECT_NE(message.find("IRGui.layoutReport"), std::string::npos) << message;
}

TEST_F(LuaWidgetLayout, SetCoversBelowRequiresALiveWidget) {
    run(R"lua(
        panel = IRGui.makePanel(0, 0, 100, 60)
        IRGui.setCoversBelow(panel, false)
    )lua");
    EXPECT_FALSE(IRPrefab::Widget::coversBelow(m_lua.lua()["panel"].get<IREntity::EntityId>()));

    const std::string message = error("IRGui.setCoversBelow(999999, true)");

    EXPECT_NE(message.find("IRGui.setCoversBelow"), std::string::npos) << message;
}

} // namespace
