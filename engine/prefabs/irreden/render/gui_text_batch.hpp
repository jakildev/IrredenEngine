#ifndef GUI_TEXT_BATCH_H
#define GUI_TEXT_BATCH_H

// Batched GUI text on the compute path.
//
// All GUI text — free-text overlays (TEXT_TO_TRIXEL) and widget
// labels/values/titles — is laid out CPU-side into a vector of
// GlyphDrawCommand, then rasterized by ONE compute dispatch that reads the
// font-atlas SSBO and writes only lit glyph texels onto the GUI canvas.
//
// This replaces the per-glyph, per-pixel `setTrixel` path (IRRender::
// renderText → renderGlyph) that issued two 1x1 subImage2D uploads per lit
// glyph pixel — thousands of CPU→GPU micro-uploads per frame for a screen of
// widget text. The compute path also scales by `fontSize` for free (the
// shader upscales each glyph by the per-command styleFlags value).
//
// The font-atlas / command / program GPU resources are the named resources
// created by System<TEXT_TO_TRIXEL>::create() ("FontDataBuffer",
// "GlyphDrawCommandBuffer", "TextToTrixelProgram"). dispatchGuiText reuses
// them, so TEXT_TO_TRIXEL must precede any widget render system in the RENDER
// pipeline. That ordering is already mandatory: TEXT_TO_TRIXEL clears the GUI
// canvas (and uploads the font buffer) in its beginTick, and widgets must
// paint after the clear. Each text-bearing render system queues its glyphs in
// its per-entity ticks and calls dispatchGuiText once in its endTick, so the
// design needs no new pipeline stage and no creation-side pipeline edits.

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace IRPrefab::GuiText {

// Max glyph commands rasterized by a single compute dispatch. Matches the
// GlyphDrawCommandBuffer capacity created in System<TEXT_TO_TRIXEL>.
constexpr int kMaxGlyphCommands = 8192;
constexpr int kFontTableEntries = 128;
constexpr int kFontRowsPerGlyph = IRRender::kGlyphHeight;
constexpr int kFontBufferSize = kFontTableEntries * kFontRowsPerGlyph;
constexpr IRMath::ivec2 kGuiOverlayPadding = IRMath::ivec2(24, 24);

// Packs the fixed 7x11 glyph bitmaps into a flat uint32 SSBO (one row per
// uint, low 7 bits used). Uploaded once into "FontDataBuffer".
inline std::vector<std::uint32_t> buildFontBuffer() {
    std::vector<std::uint32_t> buf(kFontBufferSize, 0);
    for (int ascii = 0; ascii < kFontTableEntries; ++ascii) {
        const IRRender::Glyph *g = IRRender::getGlyph(static_cast<char>(ascii));
        if (!g)
            continue;
        for (int row = 0; row < kFontRowsPerGlyph; ++row) {
            buf[ascii * kFontRowsPerGlyph + row] = static_cast<std::uint32_t>((*g)[row]);
        }
    }
    return buf;
}

inline int glyphWidth(int fontSize) { return IRRender::kGlyphWidth * fontSize; }
inline int glyphHeight(int fontSize) { return IRRender::kGlyphHeight * fontSize; }
inline int glyphStepX(int fontSize) { return IRRender::kGlyphStepX * fontSize; }
inline int glyphStepY(int fontSize) { return IRRender::kGlyphStepY * fontSize; }
inline int glyphSpacingX(int fontSize) { return IRRender::kGlyphSpacingX * fontSize; }
inline int glyphSpacingY(int fontSize) { return IRRender::kGlyphSpacingY * fontSize; }

inline int nextWordWidth(const std::string &text, std::size_t i, int fontSize) {
    int width = 0;
    const int stepX = glyphStepX(fontSize);
    while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
        width += stepX;
        i++;
    }
    return width;
}

// CPU-side layout: appends one GlyphDrawCommand per visible glyph of `text`
// to `commands`, handling newlines, word-wrap (wrapWidth), and H/V box
// alignment (boxWidth/boxHeight, defaulting to the canvas extent). Each
// command carries packed position, glyph index, packed color, the GUI-text
// distance, and `fontSize` in styleFlags. This is the prior in-system layout
// implementation, moved here unchanged so both TEXT_TO_TRIXEL and the widget
// render systems share one code path.
inline void expandTextToCommands(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    const std::string &text,
    IRMath::ivec2 position,
    IRMath::ivec2 canvasSize,
    IRMath::Color color,
    int wrapWidth,
    IRComponents::TextAlignH alignH = IRComponents::TextAlignH::LEFT,
    IRComponents::TextAlignV alignV = IRComponents::TextAlignV::TOP,
    int boxWidth = 0,
    int boxHeight = 0,
    int fontSize = 2
) {
    IRMath::ivec2 aligned = IRRender::parityAlignedPosition(position, canvasSize);
    IRMath::ivec2 cursor = aligned;
    const int gw = glyphWidth(fontSize);
    const int stepX = glyphStepX(fontSize);
    const int stepY = glyphStepY(fontSize);
    const int spacX = glyphSpacingX(fontSize);
    const int spacY = glyphSpacingY(fontSize);
    const int effectiveWrap =
        (wrapWidth > 0) ? aligned.x + wrapWidth : (wrapWidth < 0 ? canvasSize.x : 0);
    std::uint32_t packed = color.toPackedRGBA();

    const std::size_t firstCmd = commands.size();

    struct LineInfo {
        std::size_t firstIndex_;
        std::size_t count_;
        int width_;
    };
    std::vector<LineInfo> lines;
    lines.push_back({firstCmd, 0, 0});

    for (std::size_t i = 0; i < text.size(); ++i) {
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
            IRRender::GlyphDrawCommand cmd;
            cmd.positionPacked = static_cast<std::uint32_t>(cursor.x & 0xFFFF) |
                                 (static_cast<std::uint32_t>(cursor.y & 0xFFFF) << 16);
            cmd.glyphIndex = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
            cmd.colorPacked = packed;
            cmd.distance = static_cast<std::uint32_t>(IRRender::kGuiTextDistance);
            cmd.styleFlags = static_cast<std::uint32_t>(fontSize);
            commands.push_back(cmd);
            lines.back().count_++;
        }
        cursor.x += stepX;
    }

    lines.back().width_ = cursor.x - aligned.x;
    if (lines.back().width_ > 0)
        lines.back().width_ -= spacX;

    if (alignH == IRComponents::TextAlignH::LEFT && alignV == IRComponents::TextAlignV::TOP)
        return;

    const int refW = (boxWidth > 0) ? boxWidth : canvasSize.x;
    const int refH = (boxHeight > 0) ? boxHeight : canvasSize.y;
    const int totalTextH = static_cast<int>(lines.size()) * stepY - spacY;

    int offsetY = 0;
    if (alignV == IRComponents::TextAlignV::CENTER)
        offsetY = (refH - totalTextH) / 2;
    else if (alignV == IRComponents::TextAlignV::BOTTOM)
        offsetY = refH - totalTextH;

    for (auto &line : lines) {
        int offsetX = 0;
        if (alignH == IRComponents::TextAlignH::CENTER)
            offsetX = (refW - line.width_) / 2;
        else if (alignH == IRComponents::TextAlignH::RIGHT)
            offsetX = refW - line.width_;

        int roundedX = (offsetX / 2) * 2;
        int roundedY = (offsetY / 2) * 2;

        for (std::size_t j = 0; j < line.count_; ++j) {
            auto &cmd = commands[line.firstIndex_ + j];
            int cx = static_cast<int>(cmd.positionPacked & 0xFFFF) + roundedX;
            int cy = static_cast<int>(cmd.positionPacked >> 16) + roundedY;
            cmd.positionPacked = static_cast<std::uint32_t>(cx & 0xFFFF) |
                                 (static_cast<std::uint32_t>(cy & 0xFFFF) << 16);
        }
    }
}

// Convenience append for call sites that already hold the GUI canvas size
// (widget render systems, editor overlays). No-op for empty text.
inline void queueGuiText(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    const std::string &text,
    IRMath::ivec2 pos,
    IRMath::ivec2 canvasSize,
    IRMath::Color color,
    int fontSize,
    IRComponents::TextAlignH alignH = IRComponents::TextAlignH::LEFT,
    IRComponents::TextAlignV alignV = IRComponents::TextAlignV::TOP,
    int boxWidth = 0,
    int boxHeight = 0,
    int wrapWidth = 0
) {
    if (text.empty())
        return;
    expandTextToCommands(
        commands, text, pos, canvasSize, color, wrapWidth, alignH, alignV, boxWidth, boxHeight,
        fontSize
    );
}

// Uploads `commands` to the shared glyph-command SSBO and dispatches the text
// compute shader once, rasterizing every queued glyph onto the GUI canvas,
// then clears `commands`. No-op (and clears) when the shared text resources
// or the GUI canvas are absent — e.g. a creation that runs widget systems
// without TEXT_TO_TRIXEL, which would not show free text either. Call from a
// render system's endTick, after queuing that system's text in its ticks.
inline void dispatchGuiText(std::vector<IRRender::GlyphDrawCommand> &commands) {
    if (commands.empty())
        return;

    auto *program = IRRender::getNamedResource<IRRender::ShaderProgram>("TextToTrixelProgram");
    auto *cmdBuf = IRRender::getNamedResource<IRRender::Buffer>("GlyphDrawCommandBuffer");
    const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
    if (program == nullptr || cmdBuf == nullptr || guiCanvas == IREntity::kNullEntity) {
        commands.clear();
        return;
    }
    auto &canvas = IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);

    int count = static_cast<int>(commands.size());
    if (count > kMaxGlyphCommands)
        count = kMaxGlyphCommands;

    program->use();
    cmdBuf->subData(0, count * sizeof(IRRender::GlyphDrawCommand), commands.data());
    canvas.getTextureColors()->bindAsImage(
        0, IRRender::TextureAccess::WRITE_ONLY, IRRender::TextureFormat::RGBA8
    );
    canvas.getTextureDistances()->bindAsImage(
        1, IRRender::TextureAccess::WRITE_ONLY, IRRender::TextureFormat::R32I
    );
    IRRender::device()->dispatchCompute(count, 1, 1);
    IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);

    commands.clear();
}

} // namespace IRPrefab::GuiText

#endif /* GUI_TEXT_BATCH_H */
