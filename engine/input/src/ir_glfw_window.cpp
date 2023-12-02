/*
 * Project: Irreden Engine
 * File: ir_glfw_window.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/input/ir_glfw_window.hpp>

#include <iostream>

namespace IRInput {

    // TODO: implement multiple sub-windows if necessary
    IRGLFWWindow::IRGLFWWindow(
        ivec2 windowSize
    )
    {
        setCallbackError(irglfwCallback_error);

        int status = glfwInit();
        IR_ASSERT(status, "Failed to initalize glfw.");

        for(int i = 0; i < kNumWindowHints; ++i) {
            glfwWindowHint(kWindowHints[i].first, kWindowHints[i].second);
        }

        int numMonitors;
        GLFWmonitor** monitors = glfwGetMonitors(&numMonitors);
        for(int i = 0; i < numMonitors; i++) {
            m_monitors.push_back(monitors[i]);
        }

        m_window = glfwCreateWindow(
            windowSize.x,
            windowSize.y,
            "IRREDEN GAME ENGINE",
            // m_monitors[0], // Cant do this in debug mode with breakpoints
            NULL,
            NULL
        );

        // glfwSetWindowPos(m_window, 50, 50);

        IR_ASSERT(m_window != nullptr,
            "Failed to create window: glfwCreateWindow returned null"
        );

        glfwMakeContextCurrent(m_window);

        // glfwSwapInterval(0); // Remove for vsync?

        status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        IR_ASSERT(status, "Failed to initalize GLAD");

        setWindowUserPointer(this);
        // setWindowIcon();

        setCallbackFramebufferSize(irglfwCallback_framebuffer_size);
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

    void IRGLFWWindow:: setWindowMonitor() {
        // TODO
    }

    void IRGLFWWindow::setWindowUserPointer(void* pointer) {
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

    void IRGLFWWindow::getWindowSize(int& width, int& height) {
        glfwGetWindowSize(m_window, &width, &height);
    }

    void IRGLFWWindow::getCursorPosition(double& posX, double& posY) {
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
        if(res != GLFW_TRUE) {
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
    )
    {
        glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallbackFunction);
    }

    void IRGLFWWindow::setCallbackKey(
        GLFWkeyfun keyCallbackFunction
    )
    {

        glfwSetKeyCallback(m_window, keyCallbackFunction);
    }
    void IRGLFWWindow::setCallbackMouseButton(
        GLFWmousebuttonfun mouseButtonCallbackFunction
    )
    {
        glfwSetMouseButtonCallback(m_window, mouseButtonCallbackFunction);
    }

    void IRGLFWWindow::setCallbackScroll(
        GLFWscrollfun scrollCallbackFunction
    )
    {
        glfwSetScrollCallback(m_window, scrollCallbackFunction);
    }

    void IRGLFWWindow::setWindowIcon(
        const GLFWimage* image
    )   const
    {
        glfwSetWindowIcon(m_window, 1, image);
    }


    // callback functions

    void irglfwCallback_error(int error, const char* msg) {
        IRE_LOG_ERROR("GLFW error {}: {}", error, msg);
    }

    void irglfwCallback_framebuffer_size(GLFWwindow* window, int width, int height)
    {
        glViewport(0, 0, width, height);
        IRE_LOG_INFO("Resized viewport to {}x{}", width, height);
    }

    void irglfwCallback_mouseButton(
        GLFWwindow* window,
        int button,
        int action,
        int mods
    )
    {
        IRGLFWWindow* irglfwWindow = static_cast<IRGLFWWindow*>(
            glfwGetWindowUserPointer(window)
        );
        if(action == GLFW_PRESS) {
            irglfwWindow->addMouseButtonPressedToProcess(button);
            IRE_LOG_INFO("Mouse button {} pressed", button);
        }
        if(action == GLFW_RELEASE) {
            irglfwWindow->addMouseButtonReleasedToProcess(button);
            IRE_LOG_INFO("Mouse button {} released", button);
        }

    }

    void irglfwCallback_key(
        GLFWwindow* window,
        int key,
        int scancode,
        int action,
        int mods
    )
    {
        // get key press queues from user pointer
        IRGLFWWindow* irglfwWindow = static_cast<IRGLFWWindow*>(
            glfwGetWindowUserPointer(window)
        );
        if(action == GLFW_PRESS) {
            // If entity creation and such was syncronous then
            // this wouldnt be much of a problem...
            irglfwWindow->addKeyPressedToProcess(key);
            IRE_LOG_INFO("Key {} pressed", key);
        }
        if(action == GLFW_RELEASE) {
            irglfwWindow->addKeyReleasedToProcess(key);
            IRE_LOG_INFO("Key {} released", key);
        }
    }

    void irglfwCallback_scroll(
        GLFWwindow* window,
        double xoffset,
        double yoffset
    )
    {
        IRGLFWWindow* irglfwWindow = static_cast<IRGLFWWindow*>(
            glfwGetWindowUserPointer(window)
        );
        irglfwWindow->addScrollToProcess(xoffset, yoffset);
        IRE_LOG_INFO("Scroll: xoffset: {}, yoffset: {}", xoffset, yoffset);
    }

} // namespace IRInput

