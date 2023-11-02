/*
 * Project: Irreden Engine
 * File: ir_glfw_window.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_GLFW_WINDOW_H
#define IR_GLFW_WINDOW_H

// Style guideline?
// Modules on top
// internal files second
// foreign files third
// cpp files last
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <irreden/ir_math.hpp>
#include <irreden/input/ir_input_types.hpp>

#include <vector>
#include <functional>
#include <unordered_map>
#include <string>
#include <queue>


using namespace IRMath;

namespace IRInput {

    struct IRGLFWJoystickInfo {
        int joystickId_;
        std::string joystickName_;
        bool isGamepad_;

        IRGLFWJoystickInfo(
            int joystickId,
            std::string joystickName,
            bool isGamepad
        )
        :   joystickId_{joystickId}
        ,   joystickName_{joystickName}
        ,   isGamepad_{isGamepad}
        {}

    };

    const std::pair<int, int> kWindowHints[] = {
        {GLFW_CONTEXT_VERSION_MAJOR, 4},
        {GLFW_CONTEXT_VERSION_MINOR, 6},
        {GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE},
        {GLFW_DOUBLEBUFFER, GLFW_TRUE},

    };

    const int kNumWindowHints =
        sizeof(kWindowHints) /
        sizeof(kWindowHints[0]);

    class IRGLFWWindow {
    public:
        IRGLFWWindow(
            ivec2 initWindowSize
        );
        ~IRGLFWWindow();

        void getUpdateWindowSize(int& width, int& height);
        void getUpdateCursorPos(double& posX, double& posY);
        int getKeyStatus(int key);
        int getMouseButtonStatus(int button);
        GLFWgamepadstate getGamepadState(int gamepad);

        int joystickPresent(int joystick);
        std::string getJoystickName(int joystick);
        bool joystickIsGamepad(int joystick);

        int shouldClose();
        void setShouldClose();
        void setWindowMonitor(); // TODO
        void setWindowUserPointer(void* pointer);

        void swapBuffers();
        void pollEvents();

        void setCallbackError(GLFWerrorfun callbackFunction);
        void setCallbackFramebufferSize(GLFWframebuffersizefun callbackFunction);
        void setCallbackKey(GLFWkeyfun callbackFunction);
        void setCallbackMouseButton(GLFWmousebuttonfun callbackFunction);
        void setCallbackScroll(GLFWscrollfun callbackFunction);

        void addKeyPressedToProcess(int key);
        void addKeyReleasedToProcess(int key);
        void addMouseButtonPressedToProcess(int button);
        void addMouseButtonReleasedToProcess(int button);
        void addScrollToProcess(double xoffset, double yoffset);

        inline std::queue<int>& getKeysPressedToProcess() {
            return m_keysPressedToProcess;
        }
        inline std::queue<int>& getKeysReleasedToProcess() {
            return m_keysReleasedToProcess;
        }
        inline std::queue<int>& getMouseButtonsPressedToProcess() {
            return m_mouseButtonsPressedToProcess;
        }
        inline std::queue<int>& getMouseButtonsReleasedToProcess() {
            return m_mouseButtonsReleasedToProcess;
        }
        inline std::queue<std::pair<double, double>>& getScrollsToProcess() {
            return m_scrollsToProcess;
        }

    private:
        GLFWwindow* m_window;
        std::vector<GLFWmonitor*> m_monitors;
        std::queue<int> m_keysPressedToProcess;
        std::queue<int> m_keysReleasedToProcess;
        std::queue<int> m_mouseButtonsPressedToProcess;
        std::queue<int> m_mouseButtonsReleasedToProcess;
        std::queue<std::pair<double, double>> m_scrollsToProcess;
    };

    void irglfwCallback_error(int error, const char* msg);
    void irglfwCallback_framebuffer_size(GLFWwindow* window, int width, int height);
    void irglfwCallback_key(
        GLFWwindow* window,
        int key,
        int scancode,
        int action,
        int mods
    );
    void irglfwCallback_mouseButton(
        GLFWwindow* window,
        int button,
        int action,
        int mods
    );
    void irglfwCallback_scroll(
        GLFWwindow* window,
        double xoffset,
        double yoffset
    );

} // namespace IRInput

#endif /* IR_GLFW_WINDOW_H */
