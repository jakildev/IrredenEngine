#ifndef IR_GLFW_WINDOW_H
#define IR_GLFW_WINDOW_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <queue>
#include <string>
#include <utility>
#include <vector>

using namespace IRMath;

namespace IRWindow {

struct IRGLFWJoystickInfo {
    int joystickId_;
    std::string joystickName_;
    bool isGamepad_;

    IRGLFWJoystickInfo(int joystickId, std::string joystickName, bool isGamepad)
        : joystickId_{joystickId}
        , joystickName_{joystickName}
        , isGamepad_{isGamepad} {}
};

inline constexpr std::pair<int, int> kNoApiWindowHints[] = {
    {GLFW_CLIENT_API, GLFW_NO_API},
};

inline constexpr std::pair<int, int> kOpenGLWindowHints[] = {
    {GLFW_CONTEXT_VERSION_MAJOR, 4},
    {GLFW_CONTEXT_VERSION_MINOR, 6},
    {GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE},
    {GLFW_DOUBLEBUFFER, GLFW_TRUE},
};

inline constexpr int kNumWindowHints = IRPlatform::kIsOpenGL
    ? static_cast<int>(sizeof(kOpenGLWindowHints) / sizeof(kOpenGLWindowHints[0]))
    : static_cast<int>(sizeof(kNoApiWindowHints) / sizeof(kNoApiWindowHints[0]));

inline const std::pair<int, int> &getWindowHint(int index) {
    if constexpr (IRPlatform::kIsOpenGL) {
        return kOpenGLWindowHints[index];
    } else {
        return kNoApiWindowHints[index];
    }
}

class IRGLFWWindow {
  public:
    IRGLFWWindow(ivec2 initWindowSize, bool fullscreen, int monitorIndex, std::string monitorName);
    ~IRGLFWWindow();

    void getWindowSize(int &width, int &height);
    void getFramebufferSize(int &width, int &height);
    void getCursorPosition(double &posX, double &posY);
    int getKeyStatus(int key);
    int getMouseButtonStatus(int button);
    GLFWgamepadstate getGamepadState(int gamepad);

    int joystickPresent(int joystick);
    std::string getJoystickName(int joystick);
    bool joystickIsGamepad(int joystick);

    int shouldClose();
    void setShouldClose();
    void setWindowMonitor(); // TODO
    void setWindowUserPointer(void *pointer);
    GLFWwindow *getRawWindow() const;

    void swapBuffers();
    void pollEvents();

    void setCallbackError(GLFWerrorfun callbackFunction);
    void setCallbackFramebufferSize(GLFWframebuffersizefun callbackFunction);
    void setCallbackKey(GLFWkeyfun callbackFunction);
    void setCallbackMouseButton(GLFWmousebuttonfun callbackFunction);
    void setCallbackScroll(GLFWscrollfun callbackFunction);
    void setWindowIcon(const GLFWimage *image) const;

    void addKeyPressedToProcess(int key);
    void addKeyReleasedToProcess(int key);
    void addMouseButtonPressedToProcess(int button);
    void addMouseButtonReleasedToProcess(int button);
    void addScrollToProcess(double xoffset, double yoffset);

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

  private:
    GLFWwindow *m_window;
    std::vector<GLFWmonitor *> m_monitors;
    ivec2 m_initWindowSize;
    bool m_isFullscreen;
    int m_monitorIndex;
    std::string m_monitorName;
    std::queue<int> m_keysPressedToProcess;
    std::queue<int> m_keysReleasedToProcess;
    std::queue<int> m_mouseButtonsPressedToProcess;
    std::queue<int> m_mouseButtonsReleasedToProcess;
    std::queue<std::pair<double, double>> m_scrollsToProcess;
};

void irglfwCallback_error(int error, const char *msg);
void irglfwCallback_key(GLFWwindow *window, int key, int scancode, int action, int mods);
void irglfwCallback_mouseButton(GLFWwindow *window, int button, int action, int mods);
void irglfwCallback_scroll(GLFWwindow *window, double xoffset, double yoffset);

} // namespace IRWindow

#endif /* IR_GLFW_WINDOW_H */
