#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/layout.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <string>

namespace {

class LuaLayoutBindings : public testing::Test {
  protected:
    LuaLayoutBindings() {
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
        sol::error failure = result;
        return failure.what();
    }

    template <typename T> T value(const char *name) {
        return m_lua.lua()[name].get<T>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(LuaLayoutBindings, BuildsComputesSerializesAndRestoresFourLeaves) {
    run(R"lua(
        fixed = function(value, minPx, maxPx)
            return { mode = IRLayout.SizeMode.FIXED_PX, value = value,
                     minPx = minPx, maxPx = maxPx }
        end
        fraction = function(value)
            return { mode = IRLayout.SizeMode.FRACTION, value = value }
        end
        catalog = IRGui.makePanel(0, 0, 1, 1, "CATALOG")
        details = IRGui.makePanel(0, 0, 1, 1, "DETAILS")
        assets = IRGui.makePanel(0, 0, 1, 1, "ASSETS")
        log = IRGui.makePanel(0, 0, 1, 1, "LOG")
        root = IRLayout.makeRow(nil, fraction(1), "root")
        IRLayout.makeLeaf(root, fixed(30, 10, 80), catalog, "catalog")
        right = IRLayout.makeColumn(root, fraction(1), "right")
        top = IRLayout.makeRow(right, fraction(1), "top")
        IRLayout.makeLeaf(top, fraction(1), details, "details")
        IRLayout.makeLeaf(top, fixed(20, 10, 60), assets, "assets")
        IRLayout.makeLeaf(right, fixed(30, 10, 80), log, "log")
        IRLayout.buildSplitters()
        IRLayout.setRoot(root, 0, 0, 100, 100)
    )lua");

    IRPrefab::Layout::compute();
    run(R"lua(
        x0, y0, w0, h0 = IRLayout.leafRect(catalog)
        saved = IRLayout.serialize()
        changed = saved:gsub('"p":0,"i":0,"a":30,"b":64',
                             '"p":0,"i":0,"a":40,"b":54')
        restored = IRLayout.deserialize(changed)
        revision0 = IRLayout.revision()
    )lua");
    EXPECT_EQ(value<int>("x0"), 0);
    EXPECT_EQ(value<int>("y0"), 0);
    EXPECT_EQ(value<int>("w0"), 30);
    EXPECT_EQ(value<int>("h0"), 100);
    EXPECT_TRUE(value<bool>("restored"));

    IRPrefab::Layout::compute();
    run(R"lua(
        x1, y1, w1, h1 = IRLayout.leafRect(catalog)
        revision1 = IRLayout.revision()
    )lua");
    EXPECT_EQ(value<int>("w1"), 40);
    EXPECT_EQ(value<int>("h1"), 100);
    EXPECT_GT(value<std::uint64_t>("revision1"), value<std::uint64_t>("revision0"));

    run("roundTrip = IRLayout.deserialize(saved)");
    ASSERT_TRUE(value<bool>("roundTrip"));
    IRPrefab::Layout::compute();
    run("x2, y2, w2, h2 = IRLayout.leafRect(catalog)");
    EXPECT_EQ(value<int>("w2"), 30);
}

TEST_F(LuaLayoutBindings, RejectsMalformedBuilderCallsBeforeMutation) {
    run(R"lua(
        panel = IRGui.makePanel(0, 0, 10, 10, "PANEL")
        unattached = IRGui.makePanel(0, 0, 10, 10, "UNATTACHED")
        root = IRLayout.makeRow(nil,
            { mode = IRLayout.SizeMode.FRACTION, value = 1 }, "root")
        leaf = IRLayout.makeLeaf(root,
            { mode = IRLayout.SizeMode.FIXED_PX, value = 10 }, panel, "panel")
    )lua");
    m_lua.lua()["nonWidget"] = static_cast<lua_Integer>(IREntity::createEntity());
    const std::size_t nodesBefore = IRPrefab::Layout::getLayout().nodes_.size();
    const auto entitiesBefore = m_entityManager.getLiveEntityCount();
    const auto expectRefusal = [&](const char *script) {
        EXPECT_FALSE(error(script).empty()) << script;
        EXPECT_EQ(IRPrefab::Layout::getLayout().nodes_.size(), nodesBefore);
        EXPECT_EQ(m_entityManager.getLiveEntityCount(), entitiesBefore);
    };

    expectRefusal("IRLayout.makeRow(leaf, { mode = 1, value = 1 })");
    expectRefusal("IRLayout.makeLeaf(root, { mode = 0, value = 10 }, panel)");
    expectRefusal("IRLayout.makeColumn(root, { mode = 99, value = 1 })");
    expectRefusal("IRLayout.makeRow(root, { mode = 'FRACTION', value = 1 })");
    expectRefusal("IRLayout.makeRow(root, { value = 1 })");
    expectRefusal("IRLayout.makeRow(root, { mode = 1.7, value = 1 })");
    expectRefusal("IRLayout.makeRow(1.5, { mode = 1, value = 1 })");
    expectRefusal("IRLayout.makeRow(nil, { mode = 1, value = 1 })");
    expectRefusal("IRLayout.makeRow(root, { mode = 0, value = 0 })");
    expectRefusal("IRLayout.makeRow(root, { mode = 0, value = 1, minPx = 5, maxPx = 4 })");
    expectRefusal("IRLayout.setRoot(root, 0, 0, 0, 10)");
    expectRefusal("IRLayout.setRoot(99, 0, 0, 10, 10)");
    expectRefusal("IRLayout.leafRect(unattached)");
    expectRefusal("IRLayout.makeLeaf(root, { mode = 1, value = 1 }, nonWidget)");

    run("IRLayout.buildSplitters()");
    const auto entitiesAfterBuild = m_entityManager.getLiveEntityCount();
    EXPECT_NE(error("IRLayout.buildSplitters()").find("only once"), std::string::npos);
    EXPECT_NE(
        error("IRLayout.makeRow(root, { mode = 1, value = 1 })").find("after buildSplitters"),
        std::string::npos
    );
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), entitiesAfterBuild);
}

TEST_F(LuaLayoutBindings, CorruptRestoreIsFailureAtomic) {
    run(R"lua(
        left = IRGui.makePanel(0, 0, 10, 10, "LEFT")
        right = IRGui.makePanel(0, 0, 10, 10, "RIGHT")
        root = IRLayout.makeRow(nil, { mode = IRLayout.SizeMode.FRACTION, value = 1 })
        IRLayout.makeLeaf(root, { mode = IRLayout.SizeMode.FIXED_PX, value = 30 }, left)
        IRLayout.makeLeaf(root, { mode = IRLayout.SizeMode.FRACTION, value = 1 }, right)
        IRLayout.buildSplitters()
        IRLayout.setRoot(root, 0, 0, 100, 40)
    )lua");
    IRPrefab::Layout::compute();
    run(R"lua(
        before = IRLayout.serialize()
        _, _, widthBefore = IRLayout.leafRect(left)
        revisionBefore = IRLayout.revision()
        bad = before:gsub('"a":%d+', '"a":45', 1):gsub(']}$', ',{"p":0,"i":1,"a":12}]}')
        accepted = IRLayout.deserialize(bad)
    )lua");
    IRPrefab::Layout::compute();
    run(R"lua(
        after = IRLayout.serialize()
        _, _, widthAfter = IRLayout.leafRect(left)
        revisionAfter = IRLayout.revision()
    )lua");
    EXPECT_FALSE(value<bool>("accepted"));
    EXPECT_EQ(value<int>("widthBefore"), 30);
    EXPECT_EQ(value<int>("widthAfter"), 30);
    EXPECT_EQ(value<std::uint64_t>("revisionAfter"), value<std::uint64_t>("revisionBefore"));
    EXPECT_EQ(value<std::string>("after"), value<std::string>("before"));
}

} // namespace
