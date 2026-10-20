#ifndef COMPONENT_TEXT_STYLE_H
#define COMPONENT_TEXT_STYLE_H

#include <irreden/ir_math.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/voxel_text.hpp>

namespace IRComponents {

enum class TextAlignH : int { LEFT = 0, CENTER = 1, RIGHT = 2 };
enum class TextAlignV : int { TOP = 0, CENTER = 1, BOTTOM = 2 };

struct C_TextStyle {
    IRMath::Color color_ = IRMath::IRColors::kWhite;
    int wrapWidth_ = 0; // 0=no wrap, -1=canvas edge, >0=pixel width
    TextAlignH alignH_ = TextAlignH::LEFT;
    TextAlignV alignV_ = TextAlignV::TOP;
    int boxWidth_ = 0;  // 0=canvas width, >0=explicit trixel width
    int boxHeight_ = 0; // 0=canvas height, >0=explicit trixel height
    int fontSize_ = 2;  // output pixels per glyph-bitmap trixel (1=smallest, 2=normal)
    IRRender::TextSize textSize_ = IRRender::TextSize::LARGE; // glyph set; scaled by fontSize_
    // voxel_.voxelSize_ > 0 draws isometric voxel letters (voxel_text.hpp)
    // coloured per face by faceColors_; color_, fontSize_, textSize_ and
    // wrapWidth_ then do not apply.
    IRRender::VoxelTextStyle voxel_{};
    IRRender::VoxelFaceColors faceColors_{};

    C_TextStyle(IRMath::Color color, int wrapWidth = 0)
        : color_{color}
        , wrapWidth_{wrapWidth} {}

    C_TextStyle(
        IRMath::Color color,
        int wrapWidth,
        TextAlignH alignH,
        TextAlignV alignV,
        int boxWidth,
        int boxHeight
    )
        : C_TextStyle{color, wrapWidth, alignH, alignV, boxWidth, boxHeight, 2} {}

    C_TextStyle(
        IRMath::Color color,
        int wrapWidth,
        TextAlignH alignH,
        TextAlignV alignV,
        int boxWidth,
        int boxHeight,
        int fontSize,
        IRRender::TextSize textSize = IRRender::TextSize::LARGE
    )
        : color_{color}
        , wrapWidth_{wrapWidth}
        , alignH_{alignH}
        , alignV_{alignV}
        , boxWidth_{boxWidth}
        , boxHeight_{boxHeight}
        , fontSize_{fontSize}
        , textSize_{textSize} {}

    C_TextStyle()
        : C_TextStyle{IRMath::IRColors::kWhite, 0} {}

    static C_TextStyle voxel(
        const IRRender::VoxelFaceColors &faceColors,
        int voxelSize = 1,
        IRRender::VoxelTextFacing facing = IRRender::VoxelTextFacing::CAMERA,
        int depth = 1
    ) {
        C_TextStyle style;
        style.voxel_ = IRRender::VoxelTextStyle{voxelSize, depth, facing};
        style.faceColors_ = faceColors;
        return style;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_TEXT_STYLE_H */
