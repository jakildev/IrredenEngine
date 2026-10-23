#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/common/components/component_undo_history.hpp>
#include <irreden/script/lua_script.hpp>

#include <string>

namespace {

class LuaUndoTest : public testing::Test {
  protected:
    LuaUndoTest() {
        m_lua.bindLuaUndo();
    }

    sol::protected_function_result run(const char *source) {
        return m_lua.lua().safe_script(source, sol::script_pass_on_error);
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
};

TEST_F(LuaUndoTest, ExposesCompleteSurfaceAndHistorySemantics) {
    auto result = run(R"lua(
        local names = { 'push', 'undo', 'redo', 'canUndo', 'canRedo', 'clear' }
        for _, name in ipairs(names) do assert(type(IRUndo[name]) == 'function', name) end
        value = 2
        assert(IRUndo.push{
            label = 'first', coalesce = 'slider',
            undo = function() value = 0 end,
            redo = function() value = 1 end,
        })
        assert(IRUndo.push{
            label = 'last', coalesce = 'slider',
            undo = function() value = 1 end,
            redo = function() value = 2 end,
        })
        assert(IRUndo.canUndo() and not IRUndo.canRedo())
        assert(IRUndo.undo() and value == 0)
        assert(IRUndo.canRedo())
        assert(IRUndo.redo() and value == 2)
        assert(IRUndo.undo() and value == 0)
        value = 3
        assert(IRUndo.push{
            label = 'branch',
            undo = function() value = 0 end,
            redo = function() value = 3 end,
        })
        assert(not IRUndo.canRedo())
        IRUndo.clear()
        assert(not IRUndo.canUndo() and not IRUndo.canRedo())
        return value
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_EQ(result.get<int>(), 3);
}

TEST_F(LuaUndoTest, RejectsMalformedRecordsAndTableCollision) {
    EXPECT_FALSE(run("return IRUndo.push{label='x', undo=function() end}").valid());

    IRScript::LuaScript other;
    IREntity::singleton<IRComponents::C_UndoHistory>().clear();
    other.lua()["IRUndo"] = 42;
    EXPECT_THROW(other.bindLuaUndo(), std::invalid_argument);
}

TEST_F(LuaUndoTest, CallbackErrorsDoNotEscapeAndCursorAdvances) {
    auto result = run(R"lua(
        IRUndo.push{
            label = 'bad',
            undo = function() error('undo failed') end,
            redo = function() error('redo failed') end,
        }
        assert(IRUndo.undo())
        assert(IRUndo.canRedo())
        assert(IRUndo.redo())
        return IRUndo.canUndo()
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(result.get<bool>());
}

} // namespace
