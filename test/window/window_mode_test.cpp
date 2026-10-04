#include <gtest/gtest.h>
#include <irreden/window/ir_glfw_window.hpp>

// The spellings are the contract between three readers — the `--window-mode`
// enum in IRArgs, the `window_mode` config key, and the IR_WINDOW_MODE env var
// ir-run exports — so a rename has to show up here.
namespace {

using IRWindow::WindowMode;

TEST(WindowModeTest, NamesRoundTrip) {
    for (WindowMode mode :
         {WindowMode::NORMAL, WindowMode::BACKGROUND, WindowMode::HIDDEN, WindowMode::OFFSCREEN}) {
        const auto parsed = IRWindow::parseWindowMode(IRWindow::windowModeName(mode));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, mode);
    }
}

TEST(WindowModeTest, SpellingsMatchTheCommandLineSet) {
    EXPECT_EQ(IRWindow::parseWindowMode("normal"), WindowMode::NORMAL);
    EXPECT_EQ(IRWindow::parseWindowMode("background"), WindowMode::BACKGROUND);
    EXPECT_EQ(IRWindow::parseWindowMode("hidden"), WindowMode::HIDDEN);
    EXPECT_EQ(IRWindow::parseWindowMode("offscreen"), WindowMode::OFFSCREEN);
}

TEST(WindowModeTest, RejectsAnythingElse) {
    // Minimized is a deliberate non-mode (0x0 framebuffer on Windows skips
    // every screenshot); case and whitespace are not forgiven either, since
    // the env rung is launcher plumbing that should fail loudly in the log.
    EXPECT_FALSE(IRWindow::parseWindowMode("minimized").has_value());
    EXPECT_FALSE(IRWindow::parseWindowMode("Hidden").has_value());
    EXPECT_FALSE(IRWindow::parseWindowMode(" hidden").has_value());
    EXPECT_FALSE(IRWindow::parseWindowMode("").has_value());
}

} // namespace
