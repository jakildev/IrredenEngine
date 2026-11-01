#include <gtest/gtest.h>
#include <irreden/ir_window.hpp>
#include <irreden/window/ir_glfw_window.hpp>

#include <stdexcept>

// An offscreen capture's size is the request times the backing scale. The
// scale comes from the created window's own framebuffer/window ratio, so an OS
// clamp of the hidden window (Windows caps it to the desktop work area) and a
// retina backing (2x on macOS) both land on the requested pixel size.
namespace {

using IRMath::ivec2;
using IRMath::vec2;
using IRWindow::framebufferSizeForExtent;
using IRWindow::measureFramebufferScale;
using IRWindow::WindowMode;

ivec2 offscreenFramebufferSize(ivec2 request, ivec2 osWindow, ivec2 osFramebuffer) {
    return framebufferSizeForExtent(request, measureFramebufferScale(osWindow, osFramebuffer));
}

TEST(WindowFramebufferScaleTest, WindowsClampKeepsTheRequestedSize) {
    // Three 2560x1440 monitors: SM_CYMAXTRACK 1460 less the caption and two
    // frames leaves a 1421-row client area.
    EXPECT_EQ(
        offscreenFramebufferSize(ivec2(2560, 1440), ivec2(2560, 1421), ivec2(2560, 1421)),
        ivec2(2560, 1440)
    );
}

TEST(WindowFramebufferScaleTest, RetinaBackingDoublesThePointExtent) {
    EXPECT_EQ(
        offscreenFramebufferSize(ivec2(1280, 720), ivec2(1280, 720), ivec2(2560, 1440)),
        ivec2(2560, 1440)
    );
}

TEST(WindowFramebufferScaleTest, RetinaBackingAboveTheDisplaySize) {
    // A 2400x1600-point request on a 1512x982-point display.
    EXPECT_EQ(
        offscreenFramebufferSize(ivec2(2400, 1600), ivec2(1512, 982), ivec2(3024, 1964)),
        ivec2(4800, 3200)
    );
}

TEST(WindowFramebufferScaleTest, NonPositiveMeasurementReadsAsUnitScale) {
    // A minimized Windows window reports a 0x0 framebuffer.
    EXPECT_EQ(measureFramebufferScale(ivec2(0, 0), ivec2(0, 0)), vec2(1.0f));
    EXPECT_EQ(measureFramebufferScale(ivec2(1280, 720), ivec2(0, 1440)), vec2(1.0f, 2.0f));
}

TEST(WindowFramebufferScaleTest, FractionalScaleRoundsHalfUp) {
    EXPECT_EQ(framebufferSizeForExtent(ivec2(3, 5), vec2(1.5f)), ivec2(5, 8));
}

// Drives a real IRGLFWWindow. Skips where no GL 4.5 context can be made, the
// same gate the GPU suites use.
class WindowOffscreenSurfaceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        if (!glfwInit()) {
            GTEST_SKIP() << "glfwInit failed — no display / headless host without a GPU.";
        }
        for (int i = 0; i < IRWindow::kNumWindowHints; ++i) {
            const auto &[hint, value] = IRWindow::getWindowHint(i);
            glfwWindowHint(hint, value);
        }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow *probe = glfwCreateWindow(16, 16, "ir-window-surface-probe", nullptr, nullptr);
        const bool available = probe != nullptr;
        if (probe != nullptr) {
            glfwDestroyWindow(probe);
        }
        glfwTerminate();
        if (!available) {
            GTEST_SKIP() << "No window with the engine's context hints on this host.";
        }
        s_callbackCount = 0;
        s_callbackSize = ivec2(0);
    }

    static void recordFramebufferSize(GLFWwindow *, int width, int height) {
        ++s_callbackCount;
        s_callbackSize = ivec2(width, height);
    }

    static ivec2 framebufferSize(IRWindow::IRGLFWWindow &window) {
        ivec2 size{0};
        window.getFramebufferSize(size.x, size.y);
        return size;
    }

    static ivec2 osScaleProbe(IRWindow::IRGLFWWindow &window, ivec2 extent) {
        ivec2 osWindow{0};
        ivec2 osFramebuffer{0};
        glfwGetWindowSize(window.getRawWindow(), &osWindow.x, &osWindow.y);
        glfwGetFramebufferSize(window.getRawWindow(), &osFramebuffer.x, &osFramebuffer.y);
        return offscreenFramebufferSize(extent, osWindow, osFramebuffer);
    }

    static inline int s_callbackCount = 0;
    static inline ivec2 s_callbackSize{0};
};

TEST_F(WindowOffscreenSurfaceTest, ExtentIsTheRequestNotTheOsWindow) {
    // Taller than any desktop work area, so every clamping OS shrinks it.
    const ivec2 request{2560, 4000};
    IRWindow::IRGLFWWindow window{request, false, 0, "", WindowMode::OFFSCREEN};

    ivec2 windowSize{0};
    window.getWindowSize(windowSize.x, windowSize.y);
    EXPECT_EQ(windowSize, request);
    EXPECT_EQ(framebufferSize(window), osScaleProbe(window, request));
    EXPECT_GE(framebufferSize(window).y, request.y);
}

TEST_F(WindowOffscreenSurfaceTest, SetWindowSizeResizesSynchronously) {
    IRWindow::IRGLFWWindow window{ivec2(640, 360), false, 0, "", WindowMode::OFFSCREEN};
    window.setCallbackFramebufferSize(recordFramebufferSize);
    // The hidden OS window's own framebuffer events never reach the callback.
    EXPECT_EQ(glfwSetFramebufferSizeCallback(window.getRawWindow(), nullptr), nullptr);

    const ivec2 resized{1280, 960};
    IRWindow::setWindowSize(resized);

    ivec2 windowSize{0};
    IRWindow::getWindowSize(windowSize);
    EXPECT_EQ(windowSize, resized);
    EXPECT_EQ(s_callbackCount, 1);
    EXPECT_EQ(s_callbackSize, framebufferSize(window));
    EXPECT_EQ(s_callbackSize, osScaleProbe(window, resized));
}

#ifndef IR_RELEASE
TEST_F(WindowOffscreenSurfaceTest, SetWindowSizeRejectsAnEmptyExtent) {
    IRWindow::IRGLFWWindow window{ivec2(640, 360), false, 0, "", WindowMode::OFFSCREEN};
    window.setCallbackFramebufferSize(recordFramebufferSize);
    EXPECT_THROW(window.setWindowSize(ivec2(0, 360)), std::runtime_error);
    EXPECT_EQ(s_callbackCount, 0);
    ivec2 windowSize{0};
    window.getWindowSize(windowSize.x, windowSize.y);
    EXPECT_EQ(windowSize, ivec2(640, 360));
}
#endif

TEST_F(WindowOffscreenSurfaceTest, PresentedModesRegisterTheCallbackWithGlfw) {
    IRWindow::IRGLFWWindow window{ivec2(640, 360), false, 0, "", WindowMode::HIDDEN};
    window.setCallbackFramebufferSize(recordFramebufferSize);
    EXPECT_EQ(
        glfwSetFramebufferSizeCallback(window.getRawWindow(), nullptr),
        &WindowOffscreenSurfaceTest::recordFramebufferSize
    );
}

} // namespace
