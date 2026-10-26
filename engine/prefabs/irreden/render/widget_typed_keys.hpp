#ifndef WIDGET_TYPED_KEYS_H
#define WIDGET_TYPED_KEYS_H

#include <irreden/input/ir_input_types.hpp>

#include <array>

namespace IRPrefab::Widget {

inline constexpr std::array<IRInput::KeyMouseButtons, 45> kTypedKeys = {
    IRInput::kKeyButtonA,           IRInput::kKeyButtonB,
    IRInput::kKeyButtonC,           IRInput::kKeyButtonD,
    IRInput::kKeyButtonE,           IRInput::kKeyButtonF,
    IRInput::kKeyButtonG,           IRInput::kKeyButtonH,
    IRInput::kKeyButtonI,           IRInput::kKeyButtonJ,
    IRInput::kKeyButtonK,           IRInput::kKeyButtonL,
    IRInput::kKeyButtonM,           IRInput::kKeyButtonN,
    IRInput::kKeyButtonO,           IRInput::kKeyButtonP,
    IRInput::kKeyButtonQ,           IRInput::kKeyButtonR,
    IRInput::kKeyButtonS,           IRInput::kKeyButtonT,
    IRInput::kKeyButtonU,           IRInput::kKeyButtonV,
    IRInput::kKeyButtonW,           IRInput::kKeyButtonX,
    IRInput::kKeyButtonY,           IRInput::kKeyButtonZ,
    IRInput::kKeyButton0,           IRInput::kKeyButton1,
    IRInput::kKeyButton2,           IRInput::kKeyButton3,
    IRInput::kKeyButton4,           IRInput::kKeyButton5,
    IRInput::kKeyButton6,           IRInput::kKeyButton7,
    IRInput::kKeyButton8,           IRInput::kKeyButton9,
    IRInput::kKeyButtonSpace,       IRInput::kKeyButtonPeriod,
    IRInput::kKeyButtonComma,       IRInput::kKeyButtonMinus,
    IRInput::kKeyButtonSlash,       IRInput::kKeyButtonEqual,
    IRInput::kKeyButtonLeftBracket, IRInput::kKeyButtonRightBracket,
    IRInput::kKeyButtonApostrophe,
};

inline char typedCharacter(IRInput::KeyMouseButtons key, bool shift) {
    if (key >= IRInput::kKeyButtonA && key <= IRInput::kKeyButtonZ) {
        return static_cast<char>((shift ? 'A' : 'a') + (key - IRInput::kKeyButtonA));
    }
    if (key >= IRInput::kKeyButton0 && key <= IRInput::kKeyButton9) {
        return static_cast<char>('0' + (key - IRInput::kKeyButton0));
    }
    switch (key) {
    case IRInput::kKeyButtonSpace:
        return ' ';
    case IRInput::kKeyButtonPeriod:
        return '.';
    case IRInput::kKeyButtonComma:
        return ',';
    case IRInput::kKeyButtonMinus:
        return '-';
    case IRInput::kKeyButtonSlash:
        return '/';
    case IRInput::kKeyButtonEqual:
        return '=';
    case IRInput::kKeyButtonLeftBracket:
        return shift ? '{' : '[';
    case IRInput::kKeyButtonRightBracket:
        return shift ? '}' : ']';
    case IRInput::kKeyButtonApostrophe:
        return shift ? '"' : '\'';
    default:
        return 0;
    }
}

} // namespace IRPrefab::Widget

#endif /* WIDGET_TYPED_KEYS_H */
