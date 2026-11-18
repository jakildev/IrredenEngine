#include <gtest/gtest.h>

#include "../common/hidden_window.hpp"

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/time/ir_time_types.hpp>

#include <sol/sol.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

// `<tmp>/lua_file_drop/` with a run root (`root/inside.txt`) and, outside it,
// what a user could drop: `dropped/a.txt`, `dropped/sub/b.txt`, `beside/c.txt`.
struct DropTree {
    DropTree()
        : base_{fs::weakly_canonical(fs::path(testing::TempDir()) / "lua_file_drop")} {
        fs::remove_all(base_);
        fs::create_directories(base_ / "root");
        fs::create_directories(base_ / "dropped" / "sub");
        fs::create_directories(base_ / "beside");
        write("root/inside.txt", "inside");
        write("dropped/a.txt", "alpha");
        write("dropped/sub/b.txt", "beta");
        write("beside/c.txt", "gamma");
    }
    ~DropTree() {
        std::error_code ignored;
        fs::remove_all(base_, ignored);
    }

    void write(const char *relative, const char *text) const {
        std::ofstream(base_ / relative, std::ios::binary) << text;
    }
    std::string at(const char *relative) const {
        return (base_ / relative).string();
    }

    fs::path base_;
};

// A real, never-shown engine window, so a drop can enter through the GLFW
// callback as well as through injection. m_window is declared FIRST so it is
// destroyed LAST; TearDown releases the managers before the Lua state.
class LuaFileDropTest : public testing::Test {
  protected:
    void SetUp() override {
        if (!m_window.open()) {
            GTEST_SKIP() << "no GLFW window on this host";
        }
        m_lua.emplace();
        m_entityManager.emplace();
        m_inputManager.emplace();
        m_commandManager.emplace();
        m_lua->bindLuaCommands();
        m_lua->bindLuaInput();
        m_lua->bindLuaFiles(m_tree.base_ / "root");
        sol::state &lua = m_lua->lua();
        lua["droppedFile"] = m_tree.at("dropped/a.txt");
        lua["droppedSibling"] = m_tree.at("dropped/sub/b.txt");
        lua["droppedFolder"] = m_tree.at("dropped");
        lua["besideFile"] = m_tree.at("beside/c.txt");
        lua["missingFile"] = m_tree.at("dropped/vanished.txt");
    }

    void TearDown() override {
        m_commandManager.reset();
        m_inputManager.reset();
        m_entityManager.reset();
        m_lua.reset();
    }

    void pumpFrame() {
        IRInput::getInputManager().tick();
        IRInput::getInputManager().advanceInputState(IRTime::Events::INPUT);
    }

    void run(const char *source) {
        const sol::protected_function_result result =
            m_lua->lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << sol::error(result).what();
    }

    void expectRaises(const std::string &source, const std::string &message) {
        const sol::protected_function_result result =
            m_lua->lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_FALSE(result.valid()) << source;
        const std::string what = sol::error(result).what();
        EXPECT_NE(what.find(message), std::string::npos) << source << ": " << what;
    }

    IRTest::HiddenWindow m_window;
    DropTree m_tree;
    std::optional<IRScript::LuaScript> m_lua;
    std::optional<IREntity::EntityManager> m_entityManager;
    std::optional<IRInput::InputManager> m_inputManager;
    std::optional<IRCommand::CommandManager> m_commandManager;
};

TEST_F(LuaFileDropTest, ThePollIsAnEmptyTableWithoutADrop) {
    pumpFrame();
    run(R"lua(
        local drops = IRInput.droppedFiles()
        assert(type(drops) == "table" and #drops == 0)
    )lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 0u);
}

TEST_F(LuaFileDropTest, ADropOnTheWindowIsDeliveredAndGrantedWhenPolled) {
    run(R"lua(assert(IRFile.readText(droppedFile) == nil, "confined before the drop"))lua");
    m_window.dropFiles({m_tree.at("dropped/a.txt")});
    pumpFrame();
    EXPECT_EQ(m_lua->pathGrants().size(), 0u) << "delivery alone grants nothing";
    run(R"lua(assert(IRFile.readText(droppedFile) == nil, "confined until polled"))lua");

    run(R"lua(
        local drops = IRInput.droppedFiles()
        assert(#drops == 1)
        local drop = drops[1]
        assert(#drop.paths == 1 and drop.paths[1] == droppedFile)
        assert(type(drop.x) == "number" and type(drop.y) == "number")
        assert(IRFile.readText(drop.paths[1]) == "alpha")

        assert(IRFile.readText(droppedSibling) == nil, "a file beside the drop")
        assert(IRFile.list(droppedFolder) == nil, "the drop's folder")
        assert(IRFile.readText(besideFile) == nil)
        assert(IRFile.writeText(drop.paths[1], "overwritten") == false, "grants are read-only")
        assert(IRFile.readText("inside.txt") == "inside", "the root still reads")
    )lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 1u);
}

TEST_F(LuaFileDropTest, ADroppedFolderIsListableAndReadableBeneath) {
    m_window.dropFiles({m_tree.at("dropped")});
    pumpFrame();
    run(R"lua(
        local drops = IRInput.droppedFiles()
        assert(#drops == 1 and #drops[1].paths == 1 and drops[1].paths[1] == droppedFolder)
        local entries = IRFile.list(droppedFolder)
        assert(#entries == 2 and entries[1].name == "a.txt" and entries[2].name == "sub")
        assert(IRFile.readText(droppedSibling) == "beta")
        assert(IRFile.readText(besideFile) == nil, "a folder beside the drop")
        assert(IRFile.readText(droppedFolder .. "/../beside/c.txt") == nil)
        assert(IRFile.writeText(droppedFolder .. "/new.txt", "x") == false)
    )lua");
    EXPECT_FALSE(fs::exists(m_tree.base_ / "dropped" / "new.txt"));
}

TEST_F(LuaFileDropTest, AnInjectedDropCarriesItsPointAndIsGrantedLikeARealOne) {
    run(R"lua(
        IRInput.beginSyntheticInput()
        IRInput.injectFileDrop({ droppedFile, droppedFolder }, 12.5, 40)
        IRInput.injectFileDrop({ besideFile }, 300, 200)
        assert(#IRInput.droppedFiles() == 0, "not before the next frame")
    )lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 0u) << "injecting grants nothing";
    pumpFrame();
    run(R"lua(
        local drops = IRInput.droppedFiles()
        assert(#drops == 2)
        assert(drops[1].x == 12.5 and drops[1].y == 40)
        assert(#drops[1].paths == 2)
        assert(drops[1].paths[1] == droppedFile and drops[1].paths[2] == droppedFolder)
        assert(drops[2].x == 300 and drops[2].y == 200 and drops[2].paths[1] == besideFile)
        assert(IRFile.readText(droppedFile) == "alpha")
        assert(IRFile.readText(droppedSibling) == "beta")
        assert(IRFile.readText(besideFile) == "gamma")
    )lua");
}

TEST_F(LuaFileDropTest, EveryPollInAFrameSeesTheDropAndTheNextFrameDoesNot) {
    m_window.dropFiles({m_tree.at("dropped/a.txt")});
    pumpFrame();
    run(R"lua(
        assert(#IRInput.droppedFiles() == 1)
        assert(IRInput.droppedFiles()[1].paths[1] == droppedFile)
    )lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 1u);
    pumpFrame();
    run(R"lua(
        assert(#IRInput.droppedFiles() == 0)
        assert(IRFile.readText(droppedFile) == "alpha", "the grant outlives the frame")
    )lua");
}

TEST_F(LuaFileDropTest, ADropNobodyPolledIsGoneAndGrantsNothing) {
    m_window.dropFiles({m_tree.at("dropped/a.txt")});
    pumpFrame();
    pumpFrame();
    run(R"lua(
        assert(#IRInput.droppedFiles() == 0)
        assert(IRFile.readText(droppedFile) == nil)
    )lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 0u);
}

TEST_F(LuaFileDropTest, PathsThatDoNotResolveAreLeftOut) {
    m_window.dropFiles({m_tree.at("dropped/vanished.txt"), m_tree.at("dropped/a.txt")});
    m_window.dropFiles({m_tree.at("dropped/vanished.txt")});
    m_window.dropFiles({"inside.txt"});
    pumpFrame();
    run(R"lua(
        local drops = IRInput.droppedFiles()
        assert(#drops == 1, "a drop left with no paths is left out")
        assert(#drops[1].paths == 1 and drops[1].paths[1] == droppedFile)
        assert(IRFile.readText(missingFile) == nil)
    )lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 1u);
}

TEST_F(LuaFileDropTest, OnlyTheScriptThatPolledIsGranted) {
    IRScript::LuaScript other;
    other.bindLuaCommands();
    other.bindLuaFiles(m_tree.base_ / "root");
    other.lua()["droppedFile"] = m_tree.at("dropped/a.txt");
    const sol::protected_function_result surface = other.lua().safe_script(
        R"lua(
            assert(type(IRInput.droppedFiles) == "function", "bound with the command surface")
            assert(IRInput.injectFileDrop == nil, "the inject is harness-only")
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(surface.valid()) << sol::error(surface).what();

    m_window.dropFiles({m_tree.at("dropped/a.txt")});
    pumpFrame();
    run(R"lua(assert(#IRInput.droppedFiles() == 1))lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 1u);
    EXPECT_EQ(other.pathGrants().size(), 0u);
    const sol::protected_function_result confined = other.lua().safe_script(
        R"lua(assert(IRFile.readText(droppedFile) == nil))lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(confined.valid()) << sol::error(confined).what();
}

TEST_F(LuaFileDropTest, InjectRejectsMalformedArgumentsBeforeQueuing) {
    run("IRInput.beginSyntheticInput()");
    for (const char *arguments :
         {"droppedFile, 1, 2",
          "nil, 1, 2",
          "{}, 1, 2",
          "{ droppedFile, nil, besideFile }, 1, 2",
          "{ droppedFile, name = besideFile }, 1, 2",
          "{ droppedFile, 7 }, 1, 2",
          "{ droppedFile }",
          "{ droppedFile }, 1",
          "{ droppedFile }, '1', 2",
          "{ droppedFile }, 1, 0/0",
          "{ droppedFile }, math.huge, 2"}) {
        expectRaises(
            std::string("IRInput.injectFileDrop(") + arguments + ")",
            "IRInput.injectFileDrop"
        );
    }
    pumpFrame();
    run(R"lua(assert(#IRInput.droppedFiles() == 0))lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 0u);
}

#ifndef IR_RELEASE
TEST_F(LuaFileDropTest, InjectOutsideSyntheticInputRaisesAndGrantsNothing) {
    const sol::protected_function_result result = m_lua->lua().safe_script(
        "IRInput.injectFileDrop({ droppedFile }, 1, 2)",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(result.valid());
    pumpFrame();
    run(R"lua(
        assert(#IRInput.droppedFiles() == 0)
        assert(IRFile.readText(droppedFile) == nil)
    )lua");
    EXPECT_EQ(m_lua->pathGrants().size(), 0u);
}
#endif

} // namespace
