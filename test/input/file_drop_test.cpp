#include <gtest/gtest.h>

#include "../common/hidden_window.hpp"

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/time/ir_time_types.hpp>

#include <optional>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using IRInput::FileDropEvent;
using IRWindow::WindowMode;

TEST(FileDropWindowTest, EveryUnmappedWindowModeRegistersTheDropCallback) {
    for (const WindowMode mode : {WindowMode::HIDDEN, WindowMode::OFFSCREEN}) {
        IRTest::HiddenWindow hidden;
        if (!hidden.open(mode)) {
            GTEST_SKIP() << "no GLFW window on this host";
        }
        IRWindow::IRGLFWWindow &window = hidden.window();
        window.pollEvents();
        EXPECT_TRUE(window.getFileDropsToProcess().empty()) << IRWindow::windowModeName(mode);

        const GLFWdropfun registered = glfwSetDropCallback(window.getRawWindow(), nullptr);
        EXPECT_EQ(registered, &IRWindow::irglfwCallback_drop) << IRWindow::windowModeName(mode);
    }
}

// m_window is declared FIRST so it is destroyed LAST: the input manager reads
// it while it lives.
class FileDropTest : public testing::Test {
  protected:
    void SetUp() override {
        if (!m_window.open()) {
            GTEST_SKIP() << "no GLFW window on this host";
        }
        m_entityManager.emplace();
        m_inputManager.emplace();
    }

    void TearDown() override {
        m_inputManager.reset();
        m_entityManager.reset();
    }

    void drop(std::vector<std::string> paths) {
        m_window.dropFiles(std::move(paths));
    }

    void pumpFrame() {
        IRInput::getInputManager().tick();
        IRInput::getInputManager().advanceInputState(IRTime::Events::INPUT);
    }

    IRTest::HiddenWindow m_window;
    std::optional<IREntity::EntityManager> m_entityManager;
    std::optional<IRInput::InputManager> m_inputManager;
};

TEST_F(FileDropTest, ADropIsQueuedAsOneEventOwningItsPathsAtTheCursor) {
    IRMath::dvec2 before{0.0, 0.0};
    IRMath::dvec2 after{0.0, 0.0};
    IRWindow::getCursorPosition(before);
    drop({"/pictures/a.png", "/pictures/folder"});
    IRWindow::getCursorPosition(after);

    std::queue<FileDropEvent> &queue = m_window.window().getFileDropsToProcess();
    ASSERT_EQ(queue.size(), 1u);
    EXPECT_EQ(
        queue.front().paths_,
        (std::vector<std::string>{"/pictures/a.png", "/pictures/folder"})
    );
    // The pointer is the host's; it only pins the event when it held still.
    if (before == after) {
        EXPECT_EQ(queue.front().windowPoint_, before);
    }
}

TEST_F(FileDropTest, ADropWithNoPathsQueuesNothing) {
    IRWindow::irglfwCallback_drop(m_window.window().getRawWindow(), 0, nullptr);
    EXPECT_TRUE(m_window.window().getFileDropsToProcess().empty());
}

TEST_F(FileDropTest, ADropIsDeliveredForOneFrameAndThenGone) {
    drop({"/first"});
    EXPECT_TRUE(IRInput::droppedFiles().empty()) << "not before the frame boundary";

    pumpFrame();
    EXPECT_TRUE(m_window.window().getFileDropsToProcess().empty());
    for (const IRTime::Events event : IRInput::kTrackedEvents) {
        IRInput::getInputManager().advanceInputState(event);
        const std::span<const FileDropEvent> drops = IRInput::droppedFiles();
        ASSERT_EQ(drops.size(), 1u);
        EXPECT_EQ(drops[0].paths_, std::vector<std::string>{"/first"});
    }

    drop({"/second", "/second-sibling"});
    drop({"/third"});
    pumpFrame();
    const std::span<const FileDropEvent> drops = IRInput::droppedFiles();
    ASSERT_EQ(drops.size(), 2u) << "the unread first drop does not carry over";
    EXPECT_EQ(drops[0].paths_, (std::vector<std::string>{"/second", "/second-sibling"}));
    EXPECT_EQ(drops[1].paths_, std::vector<std::string>{"/third"});

    pumpFrame();
    EXPECT_TRUE(IRInput::droppedFiles().empty());
}

TEST_F(FileDropTest, SyntheticInputDeliversInjectedDropsAndSuppressesTheWindows) {
    IRInput::beginSyntheticInput();
    IRInput::injectMouseMove({7, 9});
    drop({"/from-the-window"});
    IRInput::injectFileDrop({{"/injected/a", "/injected/b"}, {120.5, 80.25}});
    IRInput::injectFileDrop({{"/injected/c"}, {1.0, 2.0}});
    EXPECT_TRUE(IRInput::droppedFiles().empty()) << "not before the frame boundary";

    pumpFrame();
    const std::span<const FileDropEvent> drops = IRInput::droppedFiles();
    ASSERT_EQ(drops.size(), 2u);
    EXPECT_EQ(drops[0].paths_, (std::vector<std::string>{"/injected/a", "/injected/b"}));
    EXPECT_EQ(drops[0].windowPoint_, IRMath::dvec2(120.5, 80.25));
    EXPECT_EQ(drops[1].paths_, std::vector<std::string>{"/injected/c"});
    EXPECT_EQ(IRInput::getMousePosition(), IRMath::vec2(7, 9)) << "a drop leaves the cursor alone";

    pumpFrame();
    EXPECT_TRUE(IRInput::droppedFiles().empty());
}

#ifndef IR_RELEASE
TEST_F(FileDropTest, InjectingOutsideSyntheticInputAssertsAndDeliversNothing) {
    EXPECT_THROW(IRInput::injectFileDrop({{"/injected"}, {0.0, 0.0}}), std::runtime_error);
    pumpFrame();
    EXPECT_TRUE(IRInput::droppedFiles().empty());
}
#endif

} // namespace
