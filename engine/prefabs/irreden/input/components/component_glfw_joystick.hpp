/*
 * Project: Irreden Engine
 * File: component_GLFW_joystick.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_GLFW_JOYSTICK_H
#define COMPONENT_GLFW_JOYSTICK_H

namespace IRComponents {

        struct C_GLFWJoystick {
            int joystickId_;

            C_GLFWJoystick(int joystickId)
            :   joystickId_(joystickId)
            {

            }

            C_GLFWJoystick()
            :   joystickId_(-1)
            {

            }

        };

} // namespace IRECS

#endif /* COMPONENT_GLFW_JOYSTICK_H */
