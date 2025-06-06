/*
 * Project: Irreden Engine
 * File: ir_input_types.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_INPUT_TYPES_H
#define IR_INPUT_TYPES_H

// #include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <unordered_map>

namespace IRInput {
     enum InputDevices {
        kKeyboard,
        kGamepad,
        kMouse,
        kMidi
    };

    enum ButtonStatuses {
        NOT_HELD,
        PRESSED,
        HELD,
        RELEASED,
        PRESSED_AND_RELEASED
    };

    enum InputTypes {
        KEY_MOUSE,
        GAMEPAD,
        MIDI_NOTE,
        MIDI_CC
    };

    enum OutputTypes {
        kMidiOutNotePressed,
        kMidiOutNoteReleased
    };

    // Key mouse buttons would be another good place to use
    // entity parent child relationships. Each key can be an
    // entity, and could have a parent keyboard controller, etc...
    enum KeyMouseButtons {
        kNullButton = 0,
        kKeyButtonMinus,
        kKeyButtonEqual,
        kKeyButtonLeftControl,
        kKeyButtonRightControl,
        kKeyButtonEnter,
        kKeyButtonTab,
        kKeyButtonBackspace,
        kKeyButtonInsert,
        kKeyButtonDelete,
        kKeyButtonEscape,
        kKeyButtonUp,
        kKeyButtonDown,
        kKeyButtonLeft,
        kKeyButtonRight,
        kKeyButtonPageUp,
        kKeyButtonPageDown,
        kKeyButtonHome,
        kKeyButtonEnd,
        kKeyButtonCapsLock,
        kKeyButtonScrollLock,
        kKeyButtonNumLock,
        kKeyButtonPrintScreen,
        kKeyButtonPause,
        kKeyButtonF1,
        kKeyButtonF2,
        kKeyButtonF3,
        kKeyButtonF4,
        kKeyButtonF5,
        kKeyButtonF6,
        kKeyButtonF7,
        kKeyButtonF8,
        kKeyButtonF9,
        kKeyButtonF10,
        kKeyButtonF11,
        kKeyButtonF12,
        kKeyButtonF13,
        kKeyButtonF14,
        kKeyButtonF15,
        kKeyButtonF16,
        kKeyButtonF17,
        kKeyButtonF18,
        kKeyButtonF19,
        kKeyButtonF20,
        kKeyButtonF21,
        kKeyButtonF22,
        kKeyButtonF23,
        kKeyButtonF24,
        kKeyButtonF25,
        kKeyButtonKP0,
        kKeyButtonKP1,
        kKeyButtonKP2,
        kKeyButtonKP3,
        kKeyButtonKP4,
        kKeyButtonKP5,
        kKeyButtonKP6,
        kKeyButtonKP7,
        kKeyButtonKP8,
        kKeyButtonKP9,
        kKeyButtonKPDecimal,
        kKeyButtonKPDivide,
        kKeyButtonKPMultiply,
        kKeyButtonKPSubtract,
        kKeyButtonKPAdd,
        kKeyButtonKPEnter,
        kKeyButtonKPEqual,
        kKeyButtonLeftShift,
        kKeyButtonRightShift,
        kKeyButtonLeftAlt,
        kKeyButtonRightAlt,
        kKeyButtonLeftSuper,
        kKeyButtonRightSuper,
        kKeyButtonMenu,
        kKeyButtonSpace,
        kKeyButtonApostrophe,
        kKeyButtonComma,
        kKeyButtonPeriod,
        kKeyButtonSlash,
        kKeyButtonSemicolon,
        kKeyButtonBackslash,
        kKeyButtonLeftBracket,
        kKeyButtonRightBracket,
        kKeyButtonGraveAccent,
        kKeyButtonWorld1,
        kKeyButtonWorld2,
        kKeyButtonA,
        kKeyButtonB,
        kKeyButtonC,
        kKeyButtonD,
        kKeyButtonE,
        kKeyButtonF,
        kKeyButtonG,
        kKeyButtonH,
        kKeyButtonI,
        kKeyButtonJ,
        kKeyButtonK,
        kKeyButtonL,
        kKeyButtonM,
        kKeyButtonN,
        kKeyButtonO,
        kKeyButtonP,
        kKeyButtonQ,
        kKeyButtonR,
        kKeyButtonS,
        kKeyButtonT,
        kKeyButtonU,
        kKeyButtonV,
        kKeyButtonW,
        kKeyButtonX,
        kKeyButtonY,
        kKeyButtonZ,
        kKeyButton0,
        kKeyButton1,
        kKeyButton2,
        kKeyButton3,
        kKeyButton4,
        kKeyButton5,
        kKeyButton6,
        kKeyButton7,
        kKeyButton8,
        kKeyButton9,

        kMouseButtonNull,
        kMouseButtonLeft,
        kMouseButtonRight,
        kMouseButtonMiddle,

        kNumKeyMouseButtons
    };

    enum GamepadButtons {
        kGamepadButtonA,
        kGamepadButtonB,
        kGamepadButtonX,
        kGamepadButtonY,
        kGamepadButtonLeftBumper,
        kGamepadButtonRightBumper,
        kGamepadButtonBack,
        kGamepadButtonStart,
        kGamepadButtonGuide,
        kGamepadButtonLeftThumb,
        kGamepadButtonRightThumb,
        kGamepadButtonDPadUp,
        kGamepadButtonDPadRight,
        kGamepadButtonDPadDown,
        kGamepadButtonDPadLeft,

        kNumGamepadButtons
    };

    enum GamepadAxes {
        kGamepadAxisLeftX,
        kGamepadAxisLeftY,
        kGamepadAxisRightX,
        kGamepadAxisRightY,
        kGamepadAxisLeftTrigger,
        kGamepadAxisRightTrigger,

        kNumGamepadAxes
    };

    const std::unordered_map<int, KeyMouseButtons> kMapGLFWtoIRKeyMouseButtons = {
        // Key
        {GLFW_KEY_UNKNOWN, kNullButton},
        {GLFW_KEY_SPACE, kKeyButtonSpace},
        {GLFW_KEY_APOSTROPHE, kKeyButtonApostrophe},
        {GLFW_KEY_COMMA, kKeyButtonComma},
        {GLFW_KEY_MINUS, kKeyButtonMinus},
        {GLFW_KEY_EQUAL, kKeyButtonEqual},
        {GLFW_KEY_PERIOD, kKeyButtonPeriod},
        {GLFW_KEY_SLASH, kKeyButtonSlash},
        {GLFW_KEY_SEMICOLON, kKeyButtonSemicolon},
        {GLFW_KEY_LEFT_CONTROL, kKeyButtonLeftControl},
        {GLFW_KEY_RIGHT_CONTROL, kKeyButtonRightControl},
        {GLFW_KEY_ESCAPE, kKeyButtonEscape},
        {GLFW_KEY_ENTER, kKeyButtonEnter},
        {GLFW_KEY_TAB, kKeyButtonTab},
        {GLFW_KEY_BACKSPACE, kKeyButtonBackspace},
        {GLFW_KEY_INSERT, kKeyButtonInsert},
        {GLFW_KEY_DELETE, kKeyButtonDelete},
        {GLFW_KEY_UP, kKeyButtonUp},
        {GLFW_KEY_DOWN, kKeyButtonDown},
        {GLFW_KEY_LEFT, kKeyButtonLeft},
        {GLFW_KEY_RIGHT, kKeyButtonRight},
        {GLFW_KEY_PAGE_UP, kKeyButtonPageUp},
        {GLFW_KEY_PAGE_DOWN, kKeyButtonPageDown},
        {GLFW_KEY_HOME, kKeyButtonHome},
        {GLFW_KEY_END, kKeyButtonEnd},
        {GLFW_KEY_CAPS_LOCK, kKeyButtonCapsLock},
        {GLFW_KEY_SCROLL_LOCK, kKeyButtonScrollLock},
        {GLFW_KEY_NUM_LOCK, kKeyButtonNumLock},
        {GLFW_KEY_PRINT_SCREEN, kKeyButtonPrintScreen},
        {GLFW_KEY_PAUSE, kKeyButtonPause},
        {GLFW_KEY_F1, kKeyButtonF1},
        {GLFW_KEY_F2, kKeyButtonF2},
        {GLFW_KEY_F3, kKeyButtonF3},
        {GLFW_KEY_F4, kKeyButtonF4},
        {GLFW_KEY_F5, kKeyButtonF5},
        {GLFW_KEY_F6, kKeyButtonF6},
        {GLFW_KEY_F7, kKeyButtonF7},
        {GLFW_KEY_F8, kKeyButtonF8},
        {GLFW_KEY_F9, kKeyButtonF9},
        {GLFW_KEY_F10, kKeyButtonF10},
        {GLFW_KEY_F11, kKeyButtonF11},
        {GLFW_KEY_F12, kKeyButtonF12},
        {GLFW_KEY_F13, kKeyButtonF13},
        {GLFW_KEY_F14, kKeyButtonF14},
        {GLFW_KEY_F15, kKeyButtonF15},
        {GLFW_KEY_F16, kKeyButtonF16},
        {GLFW_KEY_F17, kKeyButtonF17},
        {GLFW_KEY_F18, kKeyButtonF18},
        {GLFW_KEY_F19, kKeyButtonF19},
        {GLFW_KEY_F20, kKeyButtonF20},
        {GLFW_KEY_F21, kKeyButtonF21},
        {GLFW_KEY_F22, kKeyButtonF22},
        {GLFW_KEY_F23, kKeyButtonF23},
        {GLFW_KEY_F24, kKeyButtonF24},
        {GLFW_KEY_F25, kKeyButtonF25},
        {GLFW_KEY_KP_0, kKeyButtonKP0},
        {GLFW_KEY_KP_1, kKeyButtonKP1},
        {GLFW_KEY_KP_2, kKeyButtonKP2},
        {GLFW_KEY_KP_3, kKeyButtonKP3},
        {GLFW_KEY_KP_4, kKeyButtonKP4},
        {GLFW_KEY_KP_5, kKeyButtonKP5},
        {GLFW_KEY_KP_6, kKeyButtonKP6},
        {GLFW_KEY_KP_7, kKeyButtonKP7},
        {GLFW_KEY_KP_8, kKeyButtonKP8},
        {GLFW_KEY_KP_9, kKeyButtonKP9},
        {GLFW_KEY_KP_DECIMAL, kKeyButtonKPDecimal},
        {GLFW_KEY_KP_DIVIDE, kKeyButtonKPDivide},
        {GLFW_KEY_KP_MULTIPLY, kKeyButtonKPMultiply},
        {GLFW_KEY_KP_SUBTRACT, kKeyButtonKPSubtract},
        {GLFW_KEY_KP_ADD, kKeyButtonKPAdd},
        {GLFW_KEY_KP_ENTER, kKeyButtonKPEnter},
        {GLFW_KEY_KP_EQUAL, kKeyButtonKPEqual},
        {GLFW_KEY_LEFT_SHIFT, kKeyButtonLeftShift},
        {GLFW_KEY_RIGHT_SHIFT, kKeyButtonRightShift},
        {GLFW_KEY_LEFT_ALT, kKeyButtonLeftAlt},
        {GLFW_KEY_RIGHT_ALT, kKeyButtonRightAlt},
        {GLFW_KEY_LEFT_SUPER, kKeyButtonLeftSuper},
        {GLFW_KEY_RIGHT_SUPER, kKeyButtonRightSuper},
        {GLFW_KEY_MENU, kKeyButtonMenu},


        {GLFW_KEY_LEFT_BRACKET, kKeyButtonLeftBracket},
        {GLFW_KEY_BACKSLASH, kKeyButtonBackslash},
        {GLFW_KEY_RIGHT_BRACKET, kKeyButtonRightBracket},
        {GLFW_KEY_GRAVE_ACCENT, kKeyButtonGraveAccent},
        {GLFW_KEY_WORLD_1, kKeyButtonWorld1},
        {GLFW_KEY_WORLD_2, kKeyButtonWorld2},
        {GLFW_KEY_A, kKeyButtonA},
        {GLFW_KEY_B, kKeyButtonB},
        {GLFW_KEY_C, kKeyButtonC},
        {GLFW_KEY_D, kKeyButtonD},
        {GLFW_KEY_E, kKeyButtonE},
        {GLFW_KEY_F, kKeyButtonF},
        {GLFW_KEY_G, kKeyButtonG},
        {GLFW_KEY_H, kKeyButtonH},
        {GLFW_KEY_I, kKeyButtonI},
        {GLFW_KEY_J, kKeyButtonJ},
        {GLFW_KEY_K, kKeyButtonK},
        {GLFW_KEY_L, kKeyButtonL},
        {GLFW_KEY_M, kKeyButtonM},
        {GLFW_KEY_N, kKeyButtonN},
        {GLFW_KEY_O, kKeyButtonO},
        {GLFW_KEY_P, kKeyButtonP},
        {GLFW_KEY_Q, kKeyButtonQ},
        {GLFW_KEY_R, kKeyButtonR},
        {GLFW_KEY_S, kKeyButtonS},
        {GLFW_KEY_T, kKeyButtonT},
        {GLFW_KEY_U, kKeyButtonU},
        {GLFW_KEY_V, kKeyButtonV},
        {GLFW_KEY_W, kKeyButtonW},
        {GLFW_KEY_X, kKeyButtonX},
        {GLFW_KEY_Y, kKeyButtonY},
        {GLFW_KEY_Z, kKeyButtonZ},
        {GLFW_KEY_0, kKeyButton0},
        {GLFW_KEY_1, kKeyButton1},
        {GLFW_KEY_2, kKeyButton2},
        {GLFW_KEY_3, kKeyButton3},
        {GLFW_KEY_4, kKeyButton4},
        {GLFW_KEY_5, kKeyButton5},
        {GLFW_KEY_6, kKeyButton6},
        {GLFW_KEY_7, kKeyButton7},
        {GLFW_KEY_8, kKeyButton8},
        {GLFW_KEY_9, kKeyButton9},

        // Mouse
        {GLFW_MOUSE_BUTTON_LEFT, kMouseButtonLeft},
        {GLFW_MOUSE_BUTTON_RIGHT, kMouseButtonRight},
        {GLFW_MOUSE_BUTTON_MIDDLE, kMouseButtonMiddle}
    };

    const std::unordered_map<int, GamepadButtons> kGLFWtoGamepadButtons = {
        {GLFW_GAMEPAD_BUTTON_A, kGamepadButtonA},
        {GLFW_GAMEPAD_BUTTON_B, kGamepadButtonB},
        {GLFW_GAMEPAD_BUTTON_X, kGamepadButtonX},
        {GLFW_GAMEPAD_BUTTON_Y, kGamepadButtonY},
        {GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, kGamepadButtonLeftBumper},
        {GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, kGamepadButtonRightBumper},
        {GLFW_GAMEPAD_BUTTON_BACK, kGamepadButtonBack},
        {GLFW_GAMEPAD_BUTTON_START, kGamepadButtonStart},
        {GLFW_GAMEPAD_BUTTON_GUIDE, kGamepadButtonGuide},
        {GLFW_GAMEPAD_BUTTON_LEFT_THUMB, kGamepadButtonLeftThumb},
        {GLFW_GAMEPAD_BUTTON_RIGHT_THUMB, kGamepadButtonRightThumb},
        {GLFW_GAMEPAD_BUTTON_DPAD_UP, kGamepadButtonDPadUp},
        {GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, kGamepadButtonDPadRight},
        {GLFW_GAMEPAD_BUTTON_DPAD_DOWN, kGamepadButtonDPadDown},
        {GLFW_GAMEPAD_BUTTON_DPAD_LEFT, kGamepadButtonDPadLeft}
    };

    const std::unordered_map<int, GamepadAxes> kGLFWtoGamepadAxes = {
        {GLFW_GAMEPAD_AXIS_LEFT_X, kGamepadAxisLeftX},
        {GLFW_GAMEPAD_AXIS_LEFT_Y, kGamepadAxisLeftY},
        {GLFW_GAMEPAD_AXIS_RIGHT_X, kGamepadAxisRightX},
        {GLFW_GAMEPAD_AXIS_RIGHT_Y, kGamepadAxisRightY},
        {GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, kGamepadAxisLeftTrigger},
        {GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, kGamepadAxisRightTrigger}
    };

}

#endif /* IR_INPUT_TYPES_H */
