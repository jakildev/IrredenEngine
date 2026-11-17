#include <irreden/script/lua_script.hpp>
#include <irreden/window/file_dialog.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using IRWindow::FileDialogMode;
using IRWindow::FileDialogRequest;
using IRWindow::FileDialogResult;
using IRWindow::FileDialogStatus;

// `<tmp>/lua_file_dialog/` with a run root (`root/inside.txt`) and, outside
// it, what a user could pick: `picked/a.txt`, `picked/sub/b.txt`,
// `beside/c.txt`.
struct DialogTree {
    DialogTree()
        : base_{fs::weakly_canonical(fs::path(testing::TempDir()) / "lua_file_dialog")} {
        fs::remove_all(base_);
        fs::create_directories(base_ / "root");
        fs::create_directories(base_ / "picked" / "sub");
        fs::create_directories(base_ / "beside");
        write("root/inside.txt", "inside");
        write("picked/a.txt", "alpha");
        write("picked/sub/b.txt", "beta");
        write("beside/c.txt", "gamma");
    }
    ~DialogTree() {
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

// m_lua is declared FIRST so it is destroyed LAST (test/CLAUDE.md).
class LuaFileDialogTest : public testing::Test {
  protected:
    LuaFileDialogTest() {
        m_lua.bindLuaFiles(m_tree.base_ / "root");
        m_lua.bindLuaFileDialog();
        m_lua.setFileDialogInvoker([this](const FileDialogRequest &request) {
            m_requests.push_back(request);
            return m_next;
        });
        sol::state &lua = m_lua.lua();
        lua["pickedFile"] = m_tree.at("picked/a.txt");
        lua["pickedSibling"] = m_tree.at("picked/sub/b.txt");
        lua["pickedFolder"] = m_tree.at("picked");
        lua["besideFile"] = m_tree.at("beside/c.txt");
    }

    void accept(std::vector<std::string> paths) {
        m_next = {};
        m_next.status_ = FileDialogStatus::ACCEPTED;
        m_next.paths_ = std::move(paths);
    }

    void run(const char *source) {
        const sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << sol::error(result).what();
    }

    void expectRaises(const char *source, const std::string &message) {
        const sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_FALSE(result.valid()) << source;
        const std::string what = sol::error(result).what();
        EXPECT_NE(what.find("IRDialog.open"), std::string::npos) << what;
        EXPECT_NE(what.find(message), std::string::npos) << what;
    }

    IRScript::LuaScript m_lua;
    DialogTree m_tree;
    std::vector<FileDialogRequest> m_requests;
    FileDialogResult m_next;
};

TEST_F(LuaFileDialogTest, PassesTheNormalizedRequest) {
    run(R"lua(
        local r = IRDialog.open{
            mode = IRDialog.Mode.OPEN_FILES,
            title = "Choose images",
            directory = pickedFolder,
            filters = {
                { name = "Images", extensions = { "*.png", ".jpg" } },
                { extensions = { "vxs" } },
            },
        }
        assert(r.status == IRDialog.Status.CANCELLED)
        assert(type(r.paths) == "table" and #r.paths == 0)
        assert(r.error == nil)
    )lua");
    ASSERT_EQ(m_requests.size(), 1u);
    const FileDialogRequest &request = m_requests.front();
    EXPECT_EQ(request.mode_, FileDialogMode::OPEN_FILES);
    EXPECT_EQ(request.title_, "Choose images");
    EXPECT_EQ(fs::path(request.directory_), m_tree.base_ / "picked");
    ASSERT_EQ(request.filters_.size(), 2u);
    EXPECT_EQ(request.filters_[0].name_, "Images");
    EXPECT_EQ(request.filters_[0].extensions_, (std::vector<std::string>{"png", "jpg"}));
    EXPECT_EQ(request.filters_[1].name_, "*.vxs");
}

TEST_F(LuaFileDialogTest, DefaultsToASingleFileWithNoOptions) {
    run("IRDialog.open()");
    run("IRDialog.open{}");
    ASSERT_EQ(m_requests.size(), 2u);
    for (const FileDialogRequest &request : m_requests) {
        EXPECT_EQ(request.mode_, FileDialogMode::OPEN_FILE);
        EXPECT_TRUE(request.title_.empty());
        EXPECT_TRUE(request.directory_.empty());
        EXPECT_TRUE(request.filters_.empty());
    }
}

TEST_F(LuaFileDialogTest, EnumTablesMirrorTheCppEnums) {
    run(R"lua(
        modeFile, modeFiles, modeFolder =
            IRDialog.Mode.OPEN_FILE, IRDialog.Mode.OPEN_FILES, IRDialog.Mode.OPEN_FOLDER
        statusAccepted, statusUnsupported = IRDialog.Status.ACCEPTED, IRDialog.Status.UNSUPPORTED
        assert(IRDialog.Status.CANCELLED and IRDialog.Status.REFUSED and IRDialog.Status.FAILED)
        assert(IRDialog.Mode.FILES == nil, "a typo reads nil")
    )lua");
    sol::state &lua = m_lua.lua();
    EXPECT_EQ(lua["modeFile"].get<int>(), static_cast<int>(FileDialogMode::OPEN_FILE));
    EXPECT_EQ(lua["modeFiles"].get<int>(), static_cast<int>(FileDialogMode::OPEN_FILES));
    EXPECT_EQ(lua["modeFolder"].get<int>(), static_cast<int>(FileDialogMode::OPEN_FOLDER));
    EXPECT_EQ(lua["statusAccepted"].get<int>(), static_cast<int>(FileDialogStatus::ACCEPTED));
    EXPECT_EQ(lua["statusUnsupported"].get<int>(), static_cast<int>(FileDialogStatus::UNSUPPORTED));
}

TEST_F(LuaFileDialogTest, APickedFileIsReadableAndNothingBesideItIs) {
    run(R"lua(
        assert(IRFile.readText(pickedFile) == nil, "confined before the pick")
        assert(IRFile.mtime(pickedFile) == 0)
    )lua");
    accept({m_tree.at("picked/a.txt")});
    run(R"lua(
        local r = IRDialog.open{ mode = IRDialog.Mode.OPEN_FILE }
        assert(r.status == IRDialog.Status.ACCEPTED)
        assert(#r.paths == 1 and r.paths[1] == pickedFile)
        assert(IRFile.readText(r.paths[1]) == "alpha")
        assert(IRFile.mtime(r.paths[1]) ~= 0)

        assert(IRFile.readText(pickedSibling) == nil, "a file beside the pick")
        assert(IRFile.list(pickedFolder) == nil, "the pick's folder")
        assert(IRFile.readText(besideFile) == nil)
        assert(IRFile.writeText(r.paths[1], "overwritten") == false, "grants are read-only")
        assert(IRFile.readText("inside.txt") == "inside", "the root still reads")
    )lua");
    std::ifstream picked(m_tree.base_ / "picked" / "a.txt");
    std::string text;
    picked >> text;
    EXPECT_EQ(text, "alpha");
    EXPECT_EQ(m_lua.pathGrants().size(), 1u);
}

TEST_F(LuaFileDialogTest, SeveralPickedFilesAreEachGranted) {
    accept({m_tree.at("picked/a.txt"), m_tree.at("beside/c.txt")});
    run(R"lua(
        local r = IRDialog.open{ mode = IRDialog.Mode.OPEN_FILES }
        assert(#r.paths == 2)
        assert(IRFile.readText(r.paths[1]) == "alpha")
        assert(IRFile.readText(r.paths[2]) == "gamma")
        assert(IRFile.readText(pickedSibling) == nil)
    )lua");
}

TEST_F(LuaFileDialogTest, APickedFolderIsListableAndReadableBeneath) {
    accept({m_tree.at("picked")});
    run(R"lua(
        local r = IRDialog.open{ mode = IRDialog.Mode.OPEN_FOLDER }
        assert(r.status == IRDialog.Status.ACCEPTED and #r.paths == 1)
        local entries = IRFile.list(r.paths[1])
        assert(#entries == 2)
        assert(entries[1].name == "a.txt" and entries[1].directory == false)
        assert(entries[2].name == "sub" and entries[2].directory == true)
        assert(IRFile.readText(r.paths[1] .. "/" .. entries[1].name) == "alpha")
        assert(IRFile.readText(r.paths[1] .. "/sub/b.txt") == "beta")
        assert(#IRFile.list(r.paths[1] .. "/sub") == 1)

        assert(IRFile.readText(besideFile) == nil, "a folder beside the pick")
        assert(IRFile.readText(r.paths[1] .. "/../beside/c.txt") == nil)
        assert(IRFile.writeText(r.paths[1] .. "/new.txt", "x") == false)
    )lua");
    EXPECT_FALSE(fs::exists(m_tree.base_ / "picked" / "new.txt"));
}

TEST_F(LuaFileDialogTest, ListStaysConfinedToTheRootWithoutAGrant) {
    run(R"lua(
        local entries = IRFile.list(".")
        assert(#entries == 1 and entries[1].name == "inside.txt")
        assert(IRFile.list("..") == nil)
        assert(IRFile.list(pickedFolder) == nil)
        assert(IRFile.list("missing") == nil)
        assert(IRFile.list("inside.txt") == nil, "not a directory")
    )lua");
}

TEST_F(LuaFileDialogTest, AnythingButAcceptedGrantsNothing) {
    for (const FileDialogStatus status :
         {FileDialogStatus::CANCELLED,
          FileDialogStatus::REFUSED,
          FileDialogStatus::UNSUPPORTED,
          FileDialogStatus::FAILED}) {
        m_next = {};
        m_next.status_ = status;
        m_next.paths_ = {m_tree.at("picked/a.txt")};
        m_next.error_ = status == FileDialogStatus::CANCELLED ? "" : "why";
        m_lua.lua()["expected"] = static_cast<int>(status);
        run(R"lua(
            local r = IRDialog.open{}
            assert(r.status == expected)
            assert(#r.paths == 0)
            assert(IRFile.readText(pickedFile) == nil)
        )lua");
    }
    EXPECT_EQ(m_lua.pathGrants().size(), 0u);
}

TEST_F(LuaFileDialogTest, ARefusalCarriesItsReason) {
    m_next = {};
    m_next.status_ = FileDialogStatus::REFUSED;
    m_next.error_ = "a capture run never opens a file dialog";
    run(R"lua(
        local r = IRDialog.open{ title = "x" }
        assert(r.status == IRDialog.Status.REFUSED)
        assert(#r.paths == 0)
        assert(r.error == "a capture run never opens a file dialog")
    )lua");
}

TEST_F(LuaFileDialogTest, APickThatNoLongerResolvesIsDropped) {
    accept({m_tree.at("picked/a.txt"), m_tree.at("picked/vanished.txt")});
    run(R"lua(
        local r = IRDialog.open{ mode = IRDialog.Mode.OPEN_FILES }
        assert(r.status == IRDialog.Status.ACCEPTED)
        assert(#r.paths == 1 and r.paths[1] == pickedFile)
        assert(type(r.error) == "string")
    )lua");
    accept({m_tree.at("picked/vanished.txt")});
    run(R"lua(
        local r = IRDialog.open{}
        assert(r.status == IRDialog.Status.FAILED and #r.paths == 0)
    )lua");
    // A folder pick is never a file grant, and the reverse.
    accept({m_tree.at("picked")});
    run("assert(IRDialog.open{}.status == IRDialog.Status.FAILED)");
    accept({m_tree.at("beside/c.txt")});
    run(R"lua(
        assert(IRDialog.open{ mode = IRDialog.Mode.OPEN_FOLDER }.status == IRDialog.Status.FAILED)
        assert(IRFile.readText(besideFile) == nil)
    )lua");
}

TEST_F(LuaFileDialogTest, MalformedOptionsRaiseBeforeThePanel) {
    expectRaises("IRDialog.open('images')", "expects a table");
    expectRaises("IRDialog.open{ titel = 'x' }", "unknown option");
    expectRaises("IRDialog.open{ [1] = 'x' }", "unknown option");
    expectRaises("IRDialog.open{ mode = 'OPEN_FILES' }", "IRDialog.Mode");
    expectRaises("IRDialog.open{ mode = 17 }", "IRDialog.Mode");
    expectRaises("IRDialog.open{ mode = 0.5 }", "IRDialog.Mode");
    expectRaises("IRDialog.open{ title = 5 }", "title must be a string");
    expectRaises("IRDialog.open{ title = 'a' .. string.char(0) .. 'b' }", "NUL");
    expectRaises("IRDialog.open{ directory = {} }", "directory must be a string");
    expectRaises("IRDialog.open{ filters = 'png' }", "filters must be an array");
    expectRaises("IRDialog.open{ filters = { name = 'x' } }", "non-integer key");
    expectRaises("IRDialog.open{ filters = { [2] = { extensions = { 'png' } } } }", "holes");
    expectRaises("IRDialog.open{ filters = { 'png' } }", "filters[1] must be a table");
    expectRaises(
        "IRDialog.open{ filters = { { extensions = { 'png' }, ext = 1 } } }",
        "unknown option"
    );
    expectRaises("IRDialog.open{ filters = { { name = 'Images' } } }", "filters[1].extensions");
    expectRaises("IRDialog.open{ filters = { { extensions = {} } } }", "no extensions");
    expectRaises("IRDialog.open{ filters = { { extensions = { 5 } } } }", "extensions[1]");
    expectRaises("IRDialog.open{ filters = { { extensions = { 'a/b' } } } }", "'a/b'");
    expectRaises("IRDialog.open{ filters = { { name = 7, extensions = { 'png' } } } }", ".name");
    expectRaises(
        "IRDialog.open{ mode = IRDialog.Mode.OPEN_FOLDER, filters = { { extensions = { 'png' } } } "
        "}",
        "OPEN_FOLDER"
    );
    EXPECT_TRUE(m_requests.empty());
    EXPECT_EQ(m_lua.pathGrants().size(), 0u);
}

TEST(LuaFileDialogRegistrationTest, IsOptInIdempotentAndPreservesCustomKeys) {
    IRScript::LuaScript lua;
    EXPECT_FALSE(lua.lua()["IRDialog"].valid());
    lua.lua().safe_script("IRDialog = { custom = 17 }");
    lua.bindLuaFileDialog();
    lua.bindLuaFileDialog();
    EXPECT_EQ(lua.lua()["IRDialog"]["custom"].get<int>(), 17);
    EXPECT_EQ(lua.lua()["IRDialog"]["open"].get_type(), sol::type::function);
}

TEST(LuaFileDialogRegistrationTest, RejectsNonTableCollision) {
    IRScript::LuaScript lua;
    lua.lua()["IRDialog"] = 17;
    EXPECT_THROW(lua.bindLuaFileDialog(), std::invalid_argument);
    EXPECT_EQ(lua.lua()["IRDialog"].get<int>(), 17);
}

TEST(LuaFileDialogRegistrationTest, WithoutAHostTheCallIsUnsupported) {
    IRScript::LuaScript lua;
    lua.bindLuaFileDialog();
    const sol::protected_function_result result = lua.lua().safe_script(
        R"lua(
        local r = IRDialog.open{ title = "x" }
        assert(r.status == IRDialog.Status.UNSUPPORTED)
        assert(#r.paths == 0 and type(r.error) == "string")
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

// The binding in front of the real controller: a capture run and an
// unattended window return REFUSED to Lua, the panel is never called, and
// nothing is granted.
TEST(LuaFileDialogGateTest, ACaptureRunReachesLuaAsRefused) {
    const DialogTree tree;
    for (const bool captureRun : {true, false}) {
        IRScript::LuaScript lua;
        lua.bindLuaFiles(tree.base_ / "root");
        lua.bindLuaFileDialog();
        int panelCalls = 0;
        lua.setFileDialogInvoker([&](const FileDialogRequest &request) {
            IRWindow::FileDialogHost host;
            host.captureRun_ = captureRun;
            host.windowMode_ =
                captureRun ? IRWindow::WindowMode::NORMAL : IRWindow::WindowMode::BACKGROUND;
            return IRWindow::runFileDialog(request, host, [&](const FileDialogRequest &) {
                ++panelCalls;
                FileDialogResult picked;
                picked.status_ = FileDialogStatus::ACCEPTED;
                picked.paths_ = {tree.at("picked/a.txt")};
                return picked;
            });
        });
        lua.lua()["pickedFile"] = tree.at("picked/a.txt");
        const sol::protected_function_result result = lua.lua().safe_script(
            R"lua(
            local r = IRDialog.open{ title = "x" }
            assert(r.status == IRDialog.Status.REFUSED)
            assert(#r.paths == 0 and type(r.error) == "string")
            assert(IRFile.readText(pickedFile) == nil)
        )lua",
            sol::script_pass_on_error
        );
        ASSERT_TRUE(result.valid()) << sol::error(result).what();
        EXPECT_EQ(panelCalls, 0);
        EXPECT_EQ(lua.pathGrants().size(), 0u);
    }
}

} // namespace
