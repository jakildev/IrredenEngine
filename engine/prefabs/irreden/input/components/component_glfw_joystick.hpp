#ifndef COMPONENT_GLFW_JOYSTICK_H
#define COMPONENT_GLFW_JOYSTICK_H

namespace IRComponents {

struct C_GLFWJoystick {
    int joystickId_;

    C_GLFWJoystick(int joystickId)
        : joystickId_(joystickId) {}

    C_GLFWJoystick()
        : joystickId_(-1) {}
};

} // namespace IRComponents

#endif /* COMPONENT_GLFW_JOYSTICK_H */
