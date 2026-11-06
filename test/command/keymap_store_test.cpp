#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>
#include <irreden/command/command_manager.hpp>
#include <irreden/command/keymap_store.hpp>

#include "../common/env_var.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Per-creation keymap persistence. Every case writes under its own temporary
// preferences directory, so no run reads or writes the real user directory, and
// each "restart" is a fresh `CommandManager` re-registering its actions.

namespace {

namespace fs = std::filesystem;
using IRCommand::KeyChord;
using IRCommand::KeymapLoadStatus;
using IRCommand::KeymapSaveStatus;

constexpr KeyChord kK{IRInput::kKeyButtonK, IRInput::kModifierNone};
constexpr KeyChord kJ{IRInput::kKeyButtonJ, IRInput::kModifierNone};
constexpr KeyChord kCtrlK{IRInput::kKeyButtonK, IRInput::kModifierControl};
constexpr KeyChord kShiftL{IRInput::kKeyButtonL, IRInput::kModifierShift};

std::string readFile(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

void writeFile(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

class KeymapStoreTest : public testing::Test {
  protected:
    KeymapStoreTest()
        : m_root(
              fs::temp_directory_path() /
              ("ir_keymap_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
          )
        , m_env(IRCommand::kPreferencesDirEnv, m_root.string().c_str())
        , m_path(IRCommand::keymapPathFor("test_creation")) {}

    ~KeymapStoreTest() override {
        std::error_code error;
        fs::permissions(m_path.parent_path(), fs::perms::owner_all, error);
        fs::remove_all(m_root, error);
    }

    // One process lifetime: a fresh manager, the actions registered in the
    // given order, and the keymap attached. `attachFirst` attaches before the
    // registrations, the order a creation that loads preferences early uses.
    void launch(const std::vector<std::string> &order, bool attachFirst = false) {
        m_commands.reset();
        m_commands = std::make_unique<IRCommand::CommandManager>();
        if (attachFirst) {
            m_load = m_commands->keymap().attach(m_path, "test_creation");
        }
        for (const std::string &id : order) {
            IRCommand::ActionSpec spec;
            spec.id_ = id;
            spec.invoke_ = []() {};
            if (id == "test.a") {
                spec.defaultChords_ = {kK};
            } else if (id == "test.b") {
                spec.defaultChords_ = {kJ};
            } else if (id == "test.c") {
                spec.defaultChords_ = {kShiftL};
            }
            ASSERT_TRUE(m_commands->actions().registerAction(std::move(spec)));
        }
        if (!attachFirst) {
            m_load = m_commands->keymap().attach(m_path, "test_creation");
        }
    }

    std::vector<KeyChord> chords(const std::string &id) {
        return m_commands->actions().findAction(id)->chords_;
    }

    IRCommand::ActionRegistry &actions() {
        return m_commands->actions();
    }

    fs::path m_root;
    IRTest::ScopedEnv m_env;
    fs::path m_path;
    std::unique_ptr<IRCommand::CommandManager> m_commands;
    IRCommand::KeymapLoadResult m_load;
};

TEST_F(KeymapStoreTest, PathComesFromThePreferencesOverride) {
    EXPECT_EQ(m_path, m_root / "keymaps" / "test_creation.keymap");
}

TEST_F(KeymapStoreTest, RoundTripsAcrossReorderedRegistrationAndRestart) {
    launch({"test.a", "test.b", "test.c"});
    EXPECT_EQ(m_load.status_, KeymapLoadStatus::MISSING);
    ASSERT_TRUE(actions().replaceChord("test.a", kK, kCtrlK).ok());
    ASSERT_TRUE(actions().addChord("test.b", kK).ok());
    ASSERT_TRUE(actions().clearChords("test.c").ok());
    ASSERT_TRUE(m_commands->keymap().lastSave().ok());
    const std::string saved = readFile(m_path);
    EXPECT_NE(saved.find("irreden-keymap 1"), std::string::npos);
    EXPECT_NE(saved.find("bind test.a ctrl+K"), std::string::npos);
    EXPECT_NE(saved.find("unbind test.c"), std::string::npos);

    launch({"test.c", "test.b", "test.a"});
    EXPECT_EQ(m_load.status_, KeymapLoadStatus::LOADED);
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kCtrlK});
    EXPECT_EQ(chords("test.b"), (std::vector<KeyChord>{kJ, kK}));
    EXPECT_TRUE(chords("test.c").empty());

    launch({"test.b", "test.a", "test.c"}, true);
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kCtrlK});
    EXPECT_EQ(chords("test.b"), (std::vector<KeyChord>{kJ, kK}));
    EXPECT_TRUE(chords("test.c").empty());
    EXPECT_EQ(readFile(m_path), saved) << "loading alone never rewrites the file";
}

TEST_F(KeymapStoreTest, RestoreDefaultsDropsTheOverride) {
    launch({"test.a"});
    ASSERT_TRUE(actions().replaceChord("test.a", kK, kJ).ok());
    ASSERT_TRUE(actions().restoreDefaults("test.a").ok());
    launch({"test.a"});
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kK});
    EXPECT_EQ(readFile(m_path).find("test.a"), std::string::npos);
}

TEST_F(KeymapStoreTest, MissingExtensionActionSurvivesARoundTrip) {
    launch({"test.a", "test.ext"});
    ASSERT_TRUE(actions().addChord("test.ext", kShiftL).ok());

    launch({"test.a"});
    EXPECT_FALSE(actions().isRegistered("test.ext"));
    ASSERT_TRUE(actions().replaceChord("test.a", kK, kJ).ok());
    EXPECT_NE(readFile(m_path).find("bind test.ext shift+L"), std::string::npos)
        << "saving without the extension keeps its binding";

    launch({"test.a", "test.ext"});
    EXPECT_EQ(chords("test.ext"), std::vector<KeyChord>{kShiftL});
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kJ});

    actions().unregisterAction("test.ext");
    EXPECT_TRUE(actions().overrides().contains("test.ext"));
    IRCommand::ActionSpec spec;
    spec.id_ = "test.ext";
    spec.invoke_ = []() {};
    ASSERT_TRUE(actions().registerAction(std::move(spec)));
    EXPECT_EQ(chords("test.ext"), std::vector<KeyChord>{kShiftL})
        << "re-enabling in the same session restores the user's chord";
}

TEST_F(KeymapStoreTest, MalformedFileKeepsWorkingBindingsAndIsNotOverwritten) {
    const std::string garbage = "this is not a keymap\nbind test.a ctrl+K\n";
    writeFile(m_path, garbage);
    launch({"test.a", "test.b"});
    EXPECT_EQ(m_load.status_, KeymapLoadStatus::MALFORMED);
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kK});
    ASSERT_TRUE(actions().replaceChord("test.b", kJ, kShiftL).ok());
    EXPECT_EQ(chords("test.b"), std::vector<KeyChord>{kShiftL}) << "the session still rebinds";
    EXPECT_EQ(m_commands->keymap().lastSave().status_, KeymapSaveStatus::PROTECTED);
    EXPECT_EQ(readFile(m_path), garbage);
}

TEST_F(KeymapStoreTest, NewerVersionIsNotAppliedOrOverwritten) {
    const std::string future = "irreden-keymap 99\nbind test.a ctrl+K\n";
    writeFile(m_path, future);
    launch({"test.a"});
    EXPECT_EQ(m_load.status_, KeymapLoadStatus::VERSION_TOO_NEW);
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kK});
    ASSERT_TRUE(actions().clearChords("test.a").ok());
    EXPECT_EQ(readFile(m_path), future);
}

TEST_F(KeymapStoreTest, BadLinesAreSkippedAndTheRestApplies) {
    writeFile(
        m_path,
        "irreden-keymap 1\nbind test.a ctrl+K\nbind test.b hyper+Q\nfrobnicate test.b\n"
        "bind test.b ctrl+K\n"
    );
    launch({"test.a", "test.b"});
    EXPECT_EQ(m_load.status_, KeymapLoadStatus::LOADED);
    EXPECT_EQ(m_load.skippedLines_, 2u);
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kCtrlK});
    EXPECT_EQ(chords("test.b"), std::vector<KeyChord>{kCtrlK});
}

TEST_F(KeymapStoreTest, FailedWriteKeepsTheSessionBindings) {
    writeFile(m_root / "blocker", "a file where the keymaps directory should be");
    m_commands = std::make_unique<IRCommand::CommandManager>();
    IRCommand::ActionSpec spec;
    spec.id_ = "test.a";
    spec.defaultChords_ = {kK};
    spec.invoke_ = []() {};
    ASSERT_TRUE(actions().registerAction(std::move(spec)));
    m_commands->keymap().attach(m_root / "blocker" / "keymaps" / "x.keymap", "x");
    ASSERT_TRUE(actions().replaceChord("test.a", kK, kJ).ok());
    EXPECT_EQ(m_commands->keymap().lastSave().status_, KeymapSaveStatus::WRITE_ERROR);
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kJ});
}

TEST_F(KeymapStoreTest, FailedWriteLeavesThePreviousFile) {
#if defined(_WIN32)
    GTEST_SKIP() << "directory write permissions do not block file creation on Windows";
#else
    launch({"test.a"});
    ASSERT_TRUE(actions().replaceChord("test.a", kK, kJ).ok());
    const std::string previous = readFile(m_path);
    fs::permissions(m_path.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
    ASSERT_TRUE(actions().replaceChord("test.a", kJ, kCtrlK).ok());
    EXPECT_EQ(m_commands->keymap().lastSave().status_, KeymapSaveStatus::WRITE_ERROR);
    EXPECT_EQ(chords("test.a"), std::vector<KeyChord>{kCtrlK});
    fs::permissions(m_path.parent_path(), fs::perms::owner_all);
    EXPECT_EQ(readFile(m_path), previous);
    EXPECT_FALSE(fs::exists(fs::path(m_path.string() + ".tmp")));
#endif
}

} // namespace
