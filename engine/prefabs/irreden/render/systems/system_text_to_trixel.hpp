#ifndef SYSTEM_TEXT_TO_TRIXEL_H
#define SYSTEM_TEXT_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_command.hpp>

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <vector>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

namespace IRSystem {

constexpr int kMaxGlyphCommands = 8192;
constexpr int kFontTableEntries = 128;
constexpr int kFontRowsPerGlyph = IRRender::kGlyphHeight;
constexpr int kFontBufferSize = kFontTableEntries * kFontRowsPerGlyph;
constexpr ivec2 kGuiOverlayPadding = ivec2(24, 24);

inline std::vector<uint32_t> buildFontBuffer() {
    std::vector<uint32_t> buf(kFontBufferSize, 0);
    for (int ascii = 0; ascii < kFontTableEntries; ++ascii) {
        const IRRender::Glyph *g = IRRender::getGlyph(static_cast<char>(ascii));
        if (!g)
            continue;
        for (int row = 0; row < kFontRowsPerGlyph; ++row) {
            buf[ascii * kFontRowsPerGlyph + row] = static_cast<uint32_t>((*g)[row]);
        }
    }
    return buf;
}

inline int glyphWidth(int fontSize) {
    return IRRender::kGlyphWidth * fontSize;
}
inline int glyphHeight(int fontSize) {
    return IRRender::kGlyphHeight * fontSize;
}
inline int glyphStepX(int fontSize) {
    return IRRender::kGlyphStepX * fontSize;
}
inline int glyphStepY(int fontSize) {
    return IRRender::kGlyphStepY * fontSize;
}
inline int glyphSpacingX(int fontSize) {
    return IRRender::kGlyphSpacingX * fontSize;
}
inline int glyphSpacingY(int fontSize) {
    return IRRender::kGlyphSpacingY * fontSize;
}

inline int nextWordWidth(const std::string &text, size_t i, int fontSize) {
    int width = 0;
    const int stepX = glyphStepX(fontSize);
    while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
        width += stepX;
        i++;
    }
    return width;
}

inline void expandTextToCommands(
    std::vector<GlyphDrawCommand> &commands,
    const std::string &text,
    ivec2 position,
    ivec2 canvasSize,
    Color color,
    int wrapWidth,
    TextAlignH alignH = TextAlignH::LEFT,
    TextAlignV alignV = TextAlignV::TOP,
    int boxWidth = 0,
    int boxHeight = 0,
    int fontSize = 2
) {
    ivec2 aligned = IRRender::parityAlignedPosition(position, canvasSize);
    ivec2 cursor = aligned;
    const int gw = glyphWidth(fontSize);
    const int stepX = glyphStepX(fontSize);
    const int stepY = glyphStepY(fontSize);
    const int spacX = glyphSpacingX(fontSize);
    const int spacY = glyphSpacingY(fontSize);
    const int effectiveWrap =
        (wrapWidth > 0) ? aligned.x + wrapWidth : (wrapWidth < 0 ? canvasSize.x : 0);
    uint32_t packed = color.toPackedRGBA();

    const size_t firstCmd = commands.size();

    struct LineInfo {
        size_t firstIndex_;
        size_t count_;
        int width_;
    };
    std::vector<LineInfo> lines;
    lines.push_back({firstCmd, 0, 0});

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            lines.back().width_ = cursor.x - aligned.x;
            if (lines.back().width_ > 0)
                lines.back().width_ -= spacX;
            cursor.x = aligned.x;
            cursor.y += stepY;
            lines.push_back({commands.size(), 0, 0});
            continue;
        }

        if (effectiveWrap > 0 && c == ' ') {
            int upcoming = nextWordWidth(text, i + 1, fontSize);
            if (cursor.x + stepX + upcoming > effectiveWrap) {
                lines.back().width_ = cursor.x - aligned.x;
                if (lines.back().width_ > 0)
                    lines.back().width_ -= spacX;
                cursor.x = aligned.x;
                cursor.y += stepY;
                lines.push_back({commands.size(), 0, 0});
                continue;
            }
        }

        if (effectiveWrap > 0 && cursor.x + gw > effectiveWrap && c != ' ') {
            lines.back().width_ = cursor.x - aligned.x;
            if (lines.back().width_ > 0)
                lines.back().width_ -= spacX;
            cursor.x = aligned.x;
            cursor.y += stepY;
            lines.push_back({commands.size(), 0, 0});
        }

        const IRRender::Glyph *glyph = IRRender::getGlyph(c);
        if (glyph) {
            GlyphDrawCommand cmd;
            cmd.positionPacked = static_cast<uint32_t>(cursor.x & 0xFFFF) |
                                 (static_cast<uint32_t>(cursor.y & 0xFFFF) << 16);
            cmd.glyphIndex = static_cast<uint32_t>(static_cast<unsigned char>(c));
            cmd.colorPacked = packed;
            cmd.distance = static_cast<uint32_t>(IRRender::kGuiTextDistance);
            cmd.styleFlags = static_cast<uint32_t>(fontSize);
            commands.push_back(cmd);
            lines.back().count_++;
        }
        cursor.x += stepX;
    }

    lines.back().width_ = cursor.x - aligned.x;
    if (lines.back().width_ > 0)
        lines.back().width_ -= spacX;

    if (alignH == TextAlignH::LEFT && alignV == TextAlignV::TOP)
        return;

    const int refW = (boxWidth > 0) ? boxWidth : canvasSize.x;
    const int refH = (boxHeight > 0) ? boxHeight : canvasSize.y;
    const int totalTextH = static_cast<int>(lines.size()) * stepY - spacY;

    int offsetY = 0;
    if (alignV == TextAlignV::CENTER)
        offsetY = (refH - totalTextH) / 2;
    else if (alignV == TextAlignV::BOTTOM)
        offsetY = refH - totalTextH;

    for (auto &line : lines) {
        int offsetX = 0;
        if (alignH == TextAlignH::CENTER)
            offsetX = (refW - line.width_) / 2;
        else if (alignH == TextAlignH::RIGHT)
            offsetX = refW - line.width_;

        int roundedX = (offsetX / 2) * 2;
        int roundedY = (offsetY / 2) * 2;

        for (size_t j = 0; j < line.count_; ++j) {
            auto &cmd = commands[line.firstIndex_ + j];
            int cx = static_cast<int>(cmd.positionPacked & 0xFFFF) + roundedX;
            int cy = static_cast<int>(cmd.positionPacked >> 16) + roundedY;
            cmd.positionPacked =
                static_cast<uint32_t>(cx & 0xFFFF) | (static_cast<uint32_t>(cy & 0xFFFF) << 16);
        }
    }
}

template <> struct System<TEXT_TO_TRIXEL> {
    ShaderProgram *textProgram_ = nullptr;
    Buffer *fontDataBuf_ = nullptr;
    Buffer *glyphCmdBuf_ = nullptr;
    std::vector<GlyphDrawCommand> drawCommands_;
    std::string commandList_;
    bool fontUploaded_ = false;

    // Cached once per frame in beginTick; the gui canvas entity is permanent so
    // the pointer remains valid through endTick without re-resolving.
    EntityId guiCanvas_ = IREntity::kNullEntity;
    C_TriangleCanvasTextures *canvasTextures_ = nullptr;

    void tick(
        const C_TextSegment &text,
        const C_GuiPosition &guiPos,
        const C_GuiElement &,
        const C_TextStyle &style
    ) {
        expandTextToCommands(
            drawCommands_,
            text.text_,
            guiPos.pos_,
            canvasTextures_->size_,
            style.color_,
            style.wrapWidth_,
            style.alignH_,
            style.alignV_,
            style.boxWidth_,
            style.boxHeight_,
            style.fontSize_
        );
    }

    void beginTick() {
        textProgram_->use();

        if (!fontUploaded_) {
            auto fontData = buildFontBuffer();
            fontDataBuf_->subData(0, fontData.size() * sizeof(uint32_t), fontData.data());
            fontUploaded_ = true;
        }

        guiCanvas_ = IRRender::getCanvas("gui");
        canvasTextures_ = &IREntity::getComponent<C_TriangleCanvasTextures>(guiCanvas_);
        canvasTextures_->clear();

        drawCommands_.clear();

        if (IRRender::isGuiVisible()) {
            if (commandList_.empty()) {
                commandList_ = IRCommand::buildCommandListText();
            }
            expandTextToCommands(
                drawCommands_,
                commandList_,
                kGuiOverlayPadding,
                canvasTextures_->size_,
                IRMath::IRColors::kWhite,
                0
            );
        }
    }

    void endTick() {
        if (drawCommands_.empty())
            return;

        int count = static_cast<int>(drawCommands_.size());
        if (count > kMaxGlyphCommands) {
            count = kMaxGlyphCommands;
        }

        glyphCmdBuf_->subData(0, count * sizeof(GlyphDrawCommand), drawCommands_.data());

        canvasTextures_->getTextureColors()
            ->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        canvasTextures_->getTextureDistances()
            ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);

        IRRender::device()->dispatchCompute(count, 1, 1);
        // TODO: Look over all barriers and try and make the minimum necessary to speed up rendering
        IRRender::device()->memoryBarrier(BarrierType::ALL);
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "TextToTrixelProgram",
            std::vector{ShaderStage{IRRender::kFileCompTextToTrixel, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "FontDataBuffer",
            nullptr,
            kFontBufferSize * sizeof(uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_FontData
        );
        IRRender::createNamedResource<Buffer>(
            "GlyphDrawCommandBuffer",
            nullptr,
            kMaxGlyphCommands * sizeof(GlyphDrawCommand),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_GlyphDrawCommands
        );

        SystemId systemId =
            registerSystem<TEXT_TO_TRIXEL, C_TextSegment, C_GuiPosition, C_GuiElement, C_TextStyle>(
                "TextToTrixel"
            );
        auto *p = getSystemParams<System<TEXT_TO_TRIXEL>>(systemId);
        p->textProgram_ = IRRender::getNamedResource<ShaderProgram>("TextToTrixelProgram");
        p->fontDataBuf_ = IRRender::getNamedResource<Buffer>("FontDataBuffer");
        p->glyphCmdBuf_ = IRRender::getNamedResource<Buffer>("GlyphDrawCommandBuffer");
        IRRender::tagGpuStage(systemId, "textToTrixel");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TEXT_TO_TRIXEL_H */
