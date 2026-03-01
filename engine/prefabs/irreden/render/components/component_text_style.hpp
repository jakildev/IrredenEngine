#ifndef COMPONENT_TEXT_STYLE_H
#define COMPONENT_TEXT_STYLE_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

enum class TextAlignH : int { LEFT = 0, CENTER = 1, RIGHT = 2 };
enum class TextAlignV : int { TOP = 0, CENTER = 1, BOTTOM = 2 };

struct C_TextStyle {
    IRMath::Color color_ = IRMath::IRColors::kWhite;
    int wrapWidth_ = 0;    // 0=no wrap, -1=canvas edge, >0=pixel width
    TextAlignH alignH_ = TextAlignH::LEFT;
    TextAlignV alignV_ = TextAlignV::TOP;
    int boxWidth_ = 0;     // 0=canvas width, >0=explicit trixel width
    int boxHeight_ = 0;    // 0=canvas height, >0=explicit trixel height

    C_TextStyle(
        IRMath::Color color,
        int wrapWidth = 0,
        TextAlignH alignH = TextAlignH::LEFT,
        TextAlignV alignV = TextAlignV::TOP,
        int boxWidth = 0,
        int boxHeight = 0
    )
        : color_{color}
        , wrapWidth_{wrapWidth}
        , alignH_{alignH}
        , alignV_{alignV}
        , boxWidth_{boxWidth}
        , boxHeight_{boxHeight} {}

    C_TextStyle()
        : C_TextStyle{IRMath::IRColors::kWhite, 0} {}
};

} // namespace IRComponents

#endif /* COMPONENT_TEXT_STYLE_H */
