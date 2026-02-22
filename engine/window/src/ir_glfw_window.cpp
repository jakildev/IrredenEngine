#include <irreden/ir_window.hpp>

#include <irreden/ir_profile.hpp>
// #include <irreden/ir_window_types.hpp>
#include <irreden/window/ir_glfw_window.hpp>

#include <cstddef>
#include <utility>

namespace IRWindow {

namespace {
void logMonitors(const std::vector<GLFWmonitor *> &monitors) {
    IRE_LOG_INFO("Discovered {} display monitors", monitors.size());
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        GLFWmonitor *monitor = monitors[i];
        const char *name = glfwGetMonitorName(monitor);
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        int monitorX = 0;
        int monitorY = 0;
        glfwGetMonitorPos(monitor, &monitorX, &monitorY);

        IRE_LOG_INFO(
            "Monitor {}: '{}' @ ({}, {}), mode={}x{} {}Hz",
            i,
            name != nullptr ? name : "<unnamed>",
            monitorX,
            monitorY,
            mode != nullptr ? mode->width : 0,
            mode != nullptr ? mode->height : 0,
            mode != nullptr ? mode->refreshRate : 0
        );
    }
}

GLFWmonitor *getPreferredMonitor(
    const std::vector<GLFWmonitor *> &monitors, int monitorIndex, const std::string &monitorName
) {
    if (monitors.empty()) {
        return nullptr;
    }

    if (!monitorName.empty()) {
        for (GLFWmonitor *monitor : monitors) {
            const char *name = glfwGetMonitorName(monitor);
            if (name != nullptr && monitorName == name) {
                return monitor;
            }
        }
        IRE_LOG_WARN(
            "Monitor name '{}' not found. Falling back to index/default monitor.",
            monitorName
        );
    }

    if (monitorIndex >= 0 && monitorIndex < static_cast<int>(monitors.size())) {
        return monitors[monitorIndex];
    }
    if (monitorIndex >= 0) {
        IRE_LOG_WARN(
            "Monitor index {} is out of range [0, {}). Falling back to primary monitor.",
            monitorIndex,
            monitors.size()
        );
    }

    return monitors[0];
}
} // namespace

// TODO: implement multiple sub-windows if necessary
IRGLFWWindow::IRGLFWWindow(
    ivec2 windowSize, bool fullscreen, int monitorIndex, std::string monitorName
)
    : m_initWindowSize{windowSize}
    , m_isFullscreen{fullscreen}
    , m_monitorIndex{monitorIndex}
    , m_monitorName{std::move(monitorName)} {
    setCallbackError(irglfwCallback_error);

    int status = glfwInit();
    IR_ASSERT(status, "Failed to initalize glfw.");

    for (int i = 0; i < kNumWindowHints; ++i) {
        glfwWindowHint(kWindowHints[i].first, kWindowHints[i].second);
    }

    int numMonitors;
    GLFWmonitor **monitors = glfwGetMonitors(&numMonitors);
    for (int i = 0; i < numMonitors; i++) {
        m_monitors.push_back(monitors[i]);
    }
    logMonitors(m_monitors);

    GLFWmonitor *selectedMonitor =
        m_isFullscreen ? getPreferredMonitor(m_monitors, m_monitorIndex, m_monitorName) : nullptr;
    m_window = glfwCreateWindow(
        windowSize.x,
        windowSize.y,
        "IRREDEN GAME ENGINE",
        m_isFullscreen ? selectedMonitor : NULL, // Cant do this in debug mode with breakpoints
        // NULL,
        NULL
    );

    IR_ASSERT(m_window != nullptr, "Failed to create window: glfwCreateWindow returned null");

    glfwMakeContextCurrent(m_window);

    setWindowMonitor();

    glfwSwapInterval(0); // Remove for vsync?

    // This needs to get put back somewhere for openGL impl
    // status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    // IR_ASSERT(status, "Failed to initalize GLAD");

    setWindowUserPointer(this);
    // setWindowIcon();

    // setCallbackFramebufferSize(openGLCallback_framebuffer_size);
    setCallbackKey(irglfwCallback_key);
    setCallbackMouseButton(irglfwCallback_mouseButton);
    setCallbackScroll(irglfwCallback_scroll);

    g_irglfwWindow = this;
    IRE_LOG_INFO("Created IRGLFWWindow.");
}

IRGLFWWindow::~IRGLFWWindow() {
    glfwDestroyWindow(m_window);
    IRE_LOG_INFO("Destroyed GLFW Window");

    glfwTerminate();
    IRE_LOG_INFO("Terminated GLFW");
}

int IRGLFWWindow::shouldClose() {
    return glfwWindowShouldClose(m_window);
}

void IRGLFWWindow::setShouldClose() {
    glfwSetWindowShouldClose(m_window, true);
}

void IRGLFWWindow::setWindowMonitor() {
    GLFWmonitor *monitor = getPreferredMonitor(m_monitors, m_monitorIndex, m_monitorName);
    if (monitor == nullptr) {
        IRE_LOG_WARN("No monitors detected, skipping monitor placement.");
        return;
    }

    if (m_isFullscreen) {
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        IR_ASSERT(mode != nullptr, "Failed to get video mode for selected monitor.");
        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        return;
    }

    int monitorX = 0;
    int monitorY = 0;
    glfwGetMonitorPos(monitor, &monitorX, &monitorY);
    glfwSetWindowPos(m_window, monitorX + 50, monitorY + 50);
    glfwSetWindowSize(m_window, m_initWindowSize.x, m_initWindowSize.y);
}

void IRGLFWWindow::setWindowUserPointer(void *pointer) {
    glfwSetWindowUserPointer(m_window, pointer);
}

void IRGLFWWindow::swapBuffers() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    glfwSwapBuffers(m_window);
}

void IRGLFWWindow::pollEvents() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    glfwPollEvents();
}

void IRGLFWWindow::getWindowSize(int &width, int &height) {
    glfwGetWindowSize(m_window, &width, &height);
}

void IRGLFWWindow::getCursorPosition(double &posX, double &posY) {
    glfwGetCursorPos(m_window, &posX, &posY);
}

int IRGLFWWindow::getKeyStatus(int key) {
    return glfwGetKey(m_window, key);
}

GLFWgamepadstate IRGLFWWindow::getGamepadState(int gamepad) {
    GLFWgamepadstate state;
    int res = glfwGetGamepadState(gamepad, &state);
    IR_ASSERT(res == GLFW_TRUE, "Error getting gamepad state!");
    return state;
}

void IRGLFWWindow::addKeyPressedToProcess(int key) {
    m_keysPressedToProcess.push(key);
}

void IRGLFWWindow::addKeyReleasedToProcess(int key) {
    m_keysReleasedToProcess.push(key);
}

void IRGLFWWindow::addScrollToProcess(double xoffset, double yoffset) {
    m_scrollsToProcess.push({xoffset, yoffset});
}

void IRGLFWWindow::addMouseButtonPressedToProcess(int button) {
    m_mouseButtonsPressedToProcess.push(button);
}
void IRGLFWWindow::addMouseButtonReleasedToProcess(int button) {
    m_mouseButtonsReleasedToProcess.push(button);
}

int IRGLFWWindow::getMouseButtonStatus(int button) {
    return glfwGetMouseButton(m_window, button);
}

int IRGLFWWindow::joystickPresent(int joystick) {
    int res = glfwJoystickPresent(joystick);
    if (res != GLFW_TRUE) {
        IRE_LOG_INFO("Joystick {} not present", joystick);
        return GLFW_FALSE;
    }
    IRE_LOG_INFO("Joystick {} present", joystick);
    return GLFW_TRUE;
}

std::string IRGLFWWindow::getJoystickName(int joystick) {
    return glfwGetJoystickName(joystick);
}

bool IRGLFWWindow::joystickIsGamepad(int joystick) {
    return glfwJoystickIsGamepad(joystick);
}

void IRGLFWWindow::setCallbackError(GLFWerrorfun callbackFunction) {
    glfwSetErrorCallback(callbackFunction);
}

void IRGLFWWindow::setCallbackFramebufferSize(
    GLFWframebuffersizefun framebufferSizeCallbackFunction
) {
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallbackFunction);
}

void IRGLFWWindow::setCallbackKey(GLFWkeyfun keyCallbackFunction) {
    glfwSetKeyCallback(m_window, keyCallbackFunction);
}
void IRGLFWWindow::setCallbackMouseButton(GLFWmousebuttonfun mouseButtonCallbackFunction) {
    glfwSetMouseButtonCallback(m_window, mouseButtonCallbackFunction);
}

void IRGLFWWindow::setCallbackScroll(GLFWscrollfun scrollCallbackFunction) {
    glfwSetScrollCallback(m_window, scrollCallbackFunction);
}

void IRGLFWWindow::setWindowIcon(const GLFWimage *image) const {
    glfwSetWindowIcon(m_window, 1, image);
}

// callback functions

void irglfwCallback_error(int error, const char *msg) {
    IRE_LOG_ERROR("GLFW error {}: {}", error, msg);
}

void irglfwCallback_mouseButton(GLFWwindow *window, int button, int action, int mods) {
    IRGLFWWindow *irglfwWindow = static_cast<IRGLFWWindow *>(glfwGetWindowUserPointer(window));
    if (action == GLFW_PRESS) {
        irglfwWindow->addMouseButtonPressedToProcess(button);
        IRE_LOG_INFO("Mouse button {} pressed", button);
    }
    if (action == GLFW_RELEASE) {
        irglfwWindow->addMouseButtonReleasedToProcess(button);
        IRE_LOG_INFO("Mouse button {} released", button);
    }
}

void irglfwCallback_key(GLFWwindow *window, int key, int scancode, int action, int mods) {
    // get key press queues from user pointer
    IRGLFWWindow *irglfwWindow = static_cast<IRGLFWWindow *>(glfwGetWindowUserPointer(window));
    if (action == GLFW_PRESS) {
        // If entity creation and such was syncronous then
        // this wouldnt be much of a problem...
        irglfwWindow->addKeyPressedToProcess(key);
        IRE_LOG_INFO("Key {} pressed", key);
    }
    if (action == GLFW_RELEASE) {
        irglfwWindow->addKeyReleasedToProcess(key);
        IRE_LOG_INFO("Key {} released", key);
    }
}

void irglfwCallback_scroll(GLFWwindow *window, double xoffset, double yoffset) {
    IRGLFWWindow *irglfwWindow = static_cast<IRGLFWWindow *>(glfwGetWindowUserPointer(window));
    irglfwWindow->addScrollToProcess(xoffset, yoffset);
    IRE_LOG_INFO("Scroll: xoffset: {}, yoffset: {}", xoffset, yoffset);
}

} // namespace IRWindow
