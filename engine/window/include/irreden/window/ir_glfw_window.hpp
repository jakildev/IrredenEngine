#ifndef IR_GLFW_WINDOW_H
#define IR_GLFW_WINDOW_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace IRMath;

namespace IRWindow {

/// Describes a connected GLFW joystick / gamepad device.
struct IRGLFWJoystickInfo {
    int joystickId_;
    std::string joystickName_;
    bool isGamepad_;

    IRGLFWJoystickInfo(int joystickId, std::string joystickName, bool isGamepad)
        : joystickId_{joystickId}
        , joystickName_{joystickName}
        , isGamepad_{isGamepad} {}
};

/// How the engine window is presented at creation. `NORMAL` is the human
/// default: shown, focused, placed on the preferred monitor. The other modes
/// exist for unattended (fleet / agent) launches, so a demo never takes the
/// screen from whoever is using the host:
///   - `BACKGROUND`: shown but never activated or focused. The window lands
///     behind whatever is in front, and on macOS the process runs as an
///     accessory app (no Dock icon, no Cmd-Tab entry).
///   - `HIDDEN`: never shown. The GL context / Metal layer, the framebuffer,
///     synthetic input, and screenshot readback all work unchanged; only the
///     on-screen surface is missing.
///   - `OFFSCREEN`: `HIDDEN` plus an engine-owned screen target. The final
///     composite renders into a framebuffer the render backend allocates at
///     the window's framebuffer size, screenshots read that back, and nothing
///     is ever presented: no swapchain, no drawable, no dependence on how the
///     OS schedules an unmapped surface. Metal paces frames to the primary
///     display's refresh so per-second cost matches a presented run. The
///     launcher's default for capture runs.
/// Minimizing is deliberately not a mode: a minimized window reports a 0x0
/// framebuffer on Windows, which skips every screenshot. Resolved as
/// `config.window_mode` < `IR_WINDOW_MODE` env var < `--window-mode`; the
/// string spellings are shared with IRArgs through @ref windowModeName.
enum class WindowMode {
    NORMAL,
    BACKGROUND,
    HIDDEN,
    OFFSCREEN,
};

/// The `--window-mode` / `config.window_mode` spelling of @p mode.
const char *windowModeName(WindowMode mode);
/// Parses a `--window-mode` / `config.window_mode` spelling (exact, lower
/// case); `std::nullopt` for anything outside the accepted set.
std::optional<WindowMode> parseWindowMode(std::string_view name);

/// GLFW window hints for Metal / Vulkan backends (no GL context).
inline constexpr std::pair<int, int> kNoApiWindowHints[] = {
    {GLFW_CLIENT_API, GLFW_NO_API},
};

/// GLFW window hints for OpenGL 4.5 core profile with double-buffering.
///
/// 4.5 is the floor across our supported hosts. Native Linux/macOS GPU
/// drivers and Windows-native expose 4.6, but WSLg's Mesa-d3d12 driver
/// caps at 4.5 — and the engine uses no 4.6-only features (no SPIR-V,
/// no `gl_HelperInvocation`, no 4.6 extensions). Targeting 4.5 keeps the
/// shader floor at GLSL 4.50 and lets a single binary boot on every host.
inline constexpr std::pair<int, int> kOpenGLWindowHints[] = {
    {GLFW_CONTEXT_VERSION_MAJOR, 4},
    {GLFW_CONTEXT_VERSION_MINOR, 5},
    {GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE},
    {GLFW_DOUBLEBUFFER, GLFW_TRUE},
};

// These checks gate GLFW/OpenGL setup, not shared renderer conventions.
/// Number of GLFW hints active for the current backend.
inline constexpr int kNumWindowHints =
    IRPlatform::kIsOpenGL
        ? static_cast<int>(sizeof(kOpenGLWindowHints) / sizeof(kOpenGLWindowHints[0]))
        : static_cast<int>(sizeof(kNoApiWindowHints) / sizeof(kNoApiWindowHints[0]));

/// Returns the GLFW hint at @p index for the current backend.
inline const std::pair<int, int> &getWindowHint(int index) {
    if constexpr (IRPlatform::kIsOpenGL) {
        return kOpenGLWindowHints[index];
    } else {
        return kNoApiWindowHints[index];
    }
}

/// Owns the GLFW window, the GL context (OpenGL builds), fullscreen state,
/// and the per-frame event queues that `InputManager` drains each frame.
class IRGLFWWindow {
  public:
    /// @p mode selects the presentation (see @ref WindowMode). Fullscreen is
    /// only honored in NORMAL mode; the other modes log and fall back to a
    /// window of @p initWindowSize.
    IRGLFWWindow(
        ivec2 initWindowSize,
        bool fullscreen,
        int monitorIndex,
        std::string monitorName,
        WindowMode mode = WindowMode::NORMAL
    );
    ~IRGLFWWindow();

    /// The presentation mode this window was created with.
    WindowMode getWindowMode() const;

    /// Logical window size in screen coordinates (may differ from framebuffer under HiDPI).
    void getWindowSize(int &width, int &height);
    /// Physical framebuffer size in pixels — use this for viewport and render-target sizing.
    void getFramebufferSize(int &width, int &height);
    /// Raw GLFW cursor position in window-space pixels.
    void getCursorPosition(double &posX, double &posY);
    /// Raw GLFW key status (`GLFW_PRESS`, `GLFW_RELEASE`, `GLFW_REPEAT`).
    int getKeyStatus(int key);
    /// Raw GLFW mouse-button status.
    int getMouseButtonStatus(int button);
    /// Full GLFW gamepad state snapshot for @p gamepad (axes + buttons).
    GLFWgamepadstate getGamepadState(int gamepad);

    /// Returns `GLFW_TRUE` if @p joystick is connected.
    int joystickPresent(int joystick);
    /// Human-readable name of the connected joystick.
    std::string getJoystickName(int joystick);
    /// Returns `true` if @p joystick has a known gamepad mapping.
    bool joystickIsGamepad(int joystick);

    /// Returns non-zero if GLFW has flagged the window for closing.
    int shouldClose();
    /// Flags the window for closing on the next `gameLoop()` iteration.
    void setShouldClose();
    void setWindowMonitor();
    void setWindowUserPointer(void *pointer);
    /// Returns the raw `GLFWwindow*` for operations not wrapped by this class.
    GLFWwindow *getRawWindow() const;

    /// Swaps front/back buffers; called by `World::gameLoop()` after each render frame.
    void swapBuffers();
    /// Pumps the GLFW event queue; must be called once per frame.
    void pollEvents();

    /// @name GLFW callback registration (called during engine init)
    /// @{
    void setCallbackError(GLFWerrorfun callbackFunction);
    void setCallbackFramebufferSize(GLFWframebuffersizefun callbackFunction);
    void setCallbackKey(GLFWkeyfun callbackFunction);
    void setCallbackMouseButton(GLFWmousebuttonfun callbackFunction);
    void setCallbackScroll(GLFWscrollfun callbackFunction);
    void setWindowIcon(const GLFWimage *image) const;
    /// @}

    /// @name Event-queue enqueue helpers (called by GLFW callbacks)
    /// @{
    void addKeyPressedToProcess(int key);
    void addKeyReleasedToProcess(int key);
    void addMouseButtonPressedToProcess(int button);
    void addMouseButtonReleasedToProcess(int button);
    void addScrollToProcess(double xoffset, double yoffset);
    /// @}

    /// @name Event-queue drain accessors (called by InputManager each frame)
    /// @{
    inline std::queue<int> &getKeysPressedToProcess() {
        return m_keysPressedToProcess;
    }
    inline std::queue<int> &getKeysReleasedToProcess() {
        return m_keysReleasedToProcess;
    }
    inline std::queue<int> &getMouseButtonsPressedToProcess() {
        return m_mouseButtonsPressedToProcess;
    }
    inline std::queue<int> &getMouseButtonsReleasedToProcess() {
        return m_mouseButtonsReleasedToProcess;
    }
    inline std::queue<std::pair<double, double>> &getScrollsToProcess() {
        return m_scrollsToProcess;
    }
    /// @}

  private:
    GLFWwindow *m_window;
    std::vector<GLFWmonitor *> m_monitors;
    ivec2 m_initWindowSize;
    bool m_isFullscreen;
    int m_monitorIndex;
    std::string m_monitorName;
    WindowMode m_windowMode;
    std::queue<int> m_keysPressedToProcess;
    std::queue<int> m_keysReleasedToProcess;
    std::queue<int> m_mouseButtonsPressedToProcess;
    std::queue<int> m_mouseButtonsReleasedToProcess;
    std::queue<std::pair<double, double>> m_scrollsToProcess;
};

/// GLFW error callback — logs the error message.
void irglfwCallback_error(int error, const char *msg);
/// GLFW key callback — enqueues key press/release events via `g_irglfwWindow`.
void irglfwCallback_key(GLFWwindow *window, int key, int scancode, int action, int mods);
/// GLFW mouse-button callback — enqueues button press/release events.
void irglfwCallback_mouseButton(GLFWwindow *window, int button, int action, int mods);
/// GLFW scroll callback — enqueues scroll delta events.
void irglfwCallback_scroll(GLFWwindow *window, double xoffset, double yoffset);

} // namespace IRWindow

#endif /* IR_GLFW_WINDOW_H */
