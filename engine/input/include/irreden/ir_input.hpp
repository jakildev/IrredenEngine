/*
 * Project: Irreden Engine
 * File: ir_input.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_INPUT_H
#define IR_INPUT_H

#include <irreden/input/ir_input_types.hpp>
#include <irreden/input/ir_glfw_window.hpp>

namespace IRInput {

    // GLOBAL IR_INPUT API
    bool checkKeyMouseButton(
        KeyMouseButtons button,
        ButtonStatuses buttonStatus
    );
}

#endif /* IR_INPUT_H */
