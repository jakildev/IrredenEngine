#ifndef IR_TEST_HIDDEN_WINDOW_H
#define IR_TEST_HIDDEN_WINDOW_H

// PURPOSE: A real, never-shown IRGLFWWindow for tests that need the engine's
//   own GLFW callbacks and event queues instead of a sentinel pointer. `open`
//   returns false on a host that cannot create one (no display, no context),
//   so the caller skips.

#include <irreden/ir_window.hpp>

#include <optional>
#include <string>
#include <vector>

namespace IRTest {

class HiddenWindow {
  public:
    /// @p mode must be one that never maps the window: HIDDEN or OFFSCREEN.
    bool open(IRWindow::WindowMode mode = IRWindow::WindowMode::HIDDEN) {
        if (mode != IRWindow::WindowMode::HIDDEN && mode != IRWindow::WindowMode::OFFSCREEN) {
            return false;
        }
        if (!glfwInit()) {
            return false;
        }
        // IRGLFWWindow asserts on a failed create, so probe with its hints first.
        for (int i = 0; i < IRWindow::kNumWindowHints; ++i) {
            const auto &[hint, value] = IRWindow::getWindowHint(i);
            glfwWindowHint(hint, value);
        }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow *probe = glfwCreateWindow(16, 16, "ir-hidden-window-probe", nullptr, nullptr);
        glfwDefaultWindowHints();
        if (probe == nullptr) {
            glfwTerminate();
            return false;
        }
        glfwDestroyWindow(probe);
        m_window.emplace(IRMath::ivec2(64, 64), false, -1, std::string{}, mode);
        return true;
    }

    IRWindow::IRGLFWWindow &window() {
        return *m_window;
    }

    /// Hands @p paths to the engine's drop callback the way GLFW does: as
    /// borrowed C strings that stop being valid when the callback returns.
    void dropFiles(std::vector<std::string> paths) {
        std::vector<const char *> borrowed;
        borrowed.reserve(paths.size());
        for (const std::string &path : paths) {
            borrowed.push_back(path.c_str());
        }
        IRWindow::irglfwCallback_drop(
            m_window->getRawWindow(),
            static_cast<int>(borrowed.size()),
            borrowed.data()
        );
        for (std::string &path : paths) {
            path.assign(path.size(), '!');
        }
    }

  private:
    // Its destructor terminates GLFW and clears the engine's window global.
    std::optional<IRWindow::IRGLFWWindow> m_window;
};

} // namespace IRTest

#endif /* IR_TEST_HIDDEN_WINDOW_H */
