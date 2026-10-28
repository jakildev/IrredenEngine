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
#include <irreden/ir_profile.hpp>

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/voxel_text.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gui_rect.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IRPrefab::GuiText {

// Max glyph commands rasterized by a single compute dispatch: the
// GlyphDrawCommandBuffer capacity created in System<TEXT_TO_TRIXEL>. Longer
// command lists dispatch in chunks of this size (dispatchGlyphCommands).
constexpr int kMaxGlyphCommands = 8192;
constexpr int kFontTableEntries = 128;
constexpr int kFontRowsPerGlyph = IRRender::kGlyphHeight;
constexpr int kSmallFontRowsPerGlyph = IRRender::kSmallGlyphHeight;
// The LARGE block (kFontTableEntries glyphs of kFontRowsPerGlyph rows), the
// SMALL block (kFontTableEntries glyphs of kSmallFontRowsPerGlyph rows), then
// one solid cell (kFontRowsPerGlyph all-lit rows) that draws any block up to
// the shader's cell size: voxel text paints its face runs with it.
constexpr int kSmallFontBlockStart = kFontTableEntries * kFontRowsPerGlyph;
constexpr int kSolidCellStart = kSmallFontBlockStart + kFontTableEntries * kSmallFontRowsPerGlyph;
constexpr int kFontBufferSize = kSolidCellStart + kFontRowsPerGlyph;
constexpr std::uint32_t kSolidCellRow = 0x7FFFu;
constexpr IRMath::ivec2 kGuiOverlayPadding = IRMath::ivec2(24, 24);

// c_text_to_trixel runs one thread per texel of the large cell, and the
// command carries each cell dimension in four bits.
static_assert(
    IRRender::kSmallGlyphWidth <= IRRender::kGlyphWidth &&
        IRRender::kSmallGlyphHeight <= IRRender::kGlyphHeight,
    "every glyph cell must fit the text shader's (kGlyphWidth, kGlyphHeight) workgroup"
);
static_assert(IRRender::kGlyphWidth <= 15 && IRRender::kGlyphHeight <= 15);
static_assert(
    IRRender::kVoxelTextMaxRun.x == IRRender::kGlyphWidth &&
        IRRender::kVoxelTextMaxRun.y == IRRender::kGlyphHeight,
    "a voxel text run must fill at most one text-shader glyph cell"
);

// The font-buffer row where `c`'s `size` glyph starts.
inline std::uint32_t firstFontRow(IRRender::TextSize size, char c) {
    const auto ascii = static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    if (size == IRRender::TextSize::SMALL) {
        return kSmallFontBlockStart + ascii * kSmallFontRowsPerGlyph;
    }
    return ascii * kFontRowsPerGlyph;
}

// Packs both glyph sets into a flat uint32 SSBO (one row per uint, low
// cell-width bits used). Uploaded once into "FontDataBuffer".
inline std::vector<std::uint32_t> buildFontBuffer() {
    std::vector<std::uint32_t> buf(kFontBufferSize, 0);
    for (IRRender::TextSize size : {IRRender::TextSize::LARGE, IRRender::TextSize::SMALL}) {
        for (int ascii = 0; ascii < kFontTableEntries; ++ascii) {
            const char c = static_cast<char>(ascii);
            const std::span<const std::uint8_t> rows = IRRender::glyphRows(size, c);
            const std::uint32_t first = firstFontRow(size, c);
            for (std::size_t row = 0; row < rows.size(); ++row) {
                buf[first + row] = static_cast<std::uint32_t>(rows[row]);
            }
        }
    }
    for (int row = 0; row < kFontRowsPerGlyph; ++row) {
        buf[kSolidCellStart + row] = kSolidCellRow;
    }
    return buf;
}

// The styleFlags cell bits for `size`; the shader draws only inside the cell.
inline std::uint32_t glyphCellStyleBits(IRRender::TextSize size) {
    const IRRender::GlyphMetrics metrics = IRRender::glyphMetrics(size);
    return (static_cast<std::uint32_t>(metrics.width_) << IRRender::kGlyphStyleCellWidthShift) |
           (static_cast<std::uint32_t>(metrics.height_) << IRRender::kGlyphStyleCellHeightShift);
}

inline int glyphWidth(int fontSize, IRRender::TextSize size = IRRender::TextSize::LARGE) {
    return IRRender::glyphMetrics(size).width_ * fontSize;
}
inline int glyphHeight(int fontSize, IRRender::TextSize size = IRRender::TextSize::LARGE) {
    return IRRender::glyphMetrics(size).height_ * fontSize;
}
inline int glyphStepX(int fontSize, IRRender::TextSize size = IRRender::TextSize::LARGE) {
    return IRRender::glyphMetrics(size).stepX_ * fontSize;
}
inline int glyphStepY(int fontSize, IRRender::TextSize size = IRRender::TextSize::LARGE) {
    return IRRender::glyphMetrics(size).stepY_ * fontSize;
}
inline int glyphSpacingX(int fontSize, IRRender::TextSize size = IRRender::TextSize::LARGE) {
    return IRRender::glyphMetrics(size).spacingX_ * fontSize;
}
inline int glyphSpacingY(int fontSize, IRRender::TextSize size = IRRender::TextSize::LARGE) {
    return IRRender::glyphMetrics(size).spacingY_ * fontSize;
}

inline int textRunWidth(
    std::string_view text, int fontSize, IRRender::TextSize size = IRRender::TextSize::LARGE
) {
    if (text.empty()) {
        return 0;
    }
    return static_cast<int>(text.size()) * glyphStepX(fontSize, size) -
           glyphSpacingX(fontSize, size);
}

inline GuiRect textBounds(
    IRMath::ivec2 origin,
    IRMath::ivec2 canvasSize,
    std::string_view text,
    int fontSize,
    IRRender::TextSize size = IRRender::TextSize::LARGE
) {
    return {
        IRRender::parityAlignedPosition(origin, canvasSize),
        IRMath::ivec2(textRunWidth(text, fontSize, size), glyphHeight(fontSize, size))
    };
}

inline int nextWordWidth(
    std::string_view text,
    std::size_t i,
    int fontSize,
    IRRender::TextSize size = IRRender::TextSize::LARGE
) {
    return IRRender::nextWordWidth(text, i, size) * fontSize;
}

// Horizontal / vertical offset that places a `width` x `height` block in a
// `refW` x `refH` box, rounded down to even so the block keeps the lattice
// parity of its origin.
inline IRMath::ivec2 alignedBlockOffset(
    int width,
    int height,
    int refW,
    int refH,
    IRComponents::TextAlignH alignH,
    IRComponents::TextAlignV alignV
) {
    int offsetX = 0;
    if (alignH == IRComponents::TextAlignH::CENTER)
        offsetX = (refW - width) / 2;
    else if (alignH == IRComponents::TextAlignH::RIGHT)
        offsetX = refW - width;
    int offsetY = 0;
    if (alignV == IRComponents::TextAlignV::CENTER)
        offsetY = (refH - height) / 2;
    else if (alignV == IRComponents::TextAlignV::BOTTOM)
        offsetY = refH - height;
    return IRMath::ivec2((offsetX / 2) * 2, (offsetY / 2) * 2);
}

// CPU-side layout: appends one GlyphDrawCommand per visible glyph of `text`
// to `commands`, handling newlines, word-wrap (wrapWidth), and H/V box
// alignment (boxWidth/boxHeight, defaulting to the canvas extent). Each
// command carries packed position, the glyph's first font-buffer row, packed
// color, the GUI-text distance, and `fontSize` plus the `size` cell in
// styleFlags. TEXT_TO_TRIXEL and the widget render systems share this path.
inline void expandTextToCommands(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    std::string_view text,
    IRMath::ivec2 position,
    IRMath::ivec2 canvasSize,
    IRMath::Color color,
    int wrapWidth,
    IRComponents::TextAlignH alignH = IRComponents::TextAlignH::LEFT,
    IRComponents::TextAlignV alignV = IRComponents::TextAlignV::TOP,
    int boxWidth = 0,
    int boxHeight = 0,
    int fontSize = 2,
    IRRender::TextSize size = IRRender::TextSize::LARGE
) {
    IRMath::ivec2 aligned = IRRender::parityAlignedPosition(position, canvasSize);
    IRMath::ivec2 cursor = aligned;
    const int gw = glyphWidth(fontSize, size);
    const int stepX = glyphStepX(fontSize, size);
    const int stepY = glyphStepY(fontSize, size);
    const int spacX = glyphSpacingX(fontSize, size);
    const int spacY = glyphSpacingY(fontSize, size);
    const std::uint32_t styleFlags =
        static_cast<std::uint32_t>(fontSize) | glyphCellStyleBits(size);
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
            int upcoming = nextWordWidth(text, i + 1, fontSize, size);
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

        IRRender::GlyphDrawCommand cmd;
        cmd.positionPacked = IRRender::packGlyphPosition(cursor);
        cmd.firstFontRow = firstFontRow(size, c);
        cmd.colorPacked = packed;
        cmd.distance = static_cast<std::uint32_t>(IRRender::kGuiTextDistance);
        cmd.styleFlags = styleFlags;
        commands.push_back(cmd);
        lines.back().count_++;
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

    for (auto &line : lines) {
        const IRMath::ivec2 offset =
            alignedBlockOffset(line.width_, totalTextH, refW, refH, alignH, alignV);
        const int roundedX = offset.x;
        const int roundedY = offset.y;

        for (std::size_t j = 0; j < line.count_; ++j) {
            auto &cmd = commands[line.firstIndex_ + j];
            cmd.positionPacked = IRRender::packGlyphPosition(
                IRRender::unpackGlyphPosition(cmd.positionPacked) + IRMath::ivec2(roundedX, roundedY)
            );
        }
    }
}

// One solid-cell command covering `size` texels at `pos` (the glyph cell
// the text shader fills whole); `size` fits a glyph cell.
inline IRRender::GlyphDrawCommand
solidCellCommand(IRMath::ivec2 pos, IRMath::ivec2 size, std::uint32_t colorPacked, int distance) {
    IRRender::GlyphDrawCommand cmd;
    cmd.positionPacked = IRRender::packGlyphPosition(pos);
    cmd.firstFontRow = static_cast<std::uint32_t>(kSolidCellStart);
    cmd.colorPacked = colorPacked;
    cmd.distance = static_cast<std::uint32_t>(distance);
    cmd.styleFlags = 1u |
                     (static_cast<std::uint32_t>(size.x) << IRRender::kGlyphStyleCellWidthShift) |
                     (static_cast<std::uint32_t>(size.y) << IRRender::kGlyphStyleCellHeightShift);
    return cmd;
}

// Appends one solid-cell command per run of `layout` (layoutVoxelText),
// coloured by the run's face. Each line aligns on its own in the
// boxWidth x boxHeight box (canvas extent when 0), as flat text does.
inline void expandVoxelTextToCommands(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    const IRRender::VoxelTextLayout &layout,
    IRMath::ivec2 position,
    IRMath::ivec2 canvasSize,
    const IRRender::VoxelFaceColors &colors,
    IRComponents::TextAlignH alignH = IRComponents::TextAlignH::LEFT,
    IRComponents::TextAlignV alignV = IRComponents::TextAlignV::TOP,
    int boxWidth = 0,
    int boxHeight = 0
) {
    const IRMath::ivec2 origin = IRRender::parityAlignedPosition(position, canvasSize);
    const int refW = (boxWidth > 0) ? boxWidth : canvasSize.x;
    const int refH = (boxHeight > 0) ? boxHeight : canvasSize.y;
    const std::uint32_t packedByFace[3] = {
        colors.forFace(IRMath::kXFace).toPackedRGBA(),
        colors.forFace(IRMath::kYFace).toPackedRGBA(),
        colors.forFace(IRMath::kZFace).toPackedRGBA(),
    };
    for (const IRRender::VoxelTextRun &run : layout.runs_) {
        const IRMath::ivec2 offset = alignedBlockOffset(
            layout.lineWidths_[static_cast<std::size_t>(run.line_)],
            layout.size_.y,
            refW,
            refH,
            alignH,
            alignV
        );
        commands.push_back(solidCellCommand(
            origin + offset + run.pos_, run.size_, packedByFace[run.face_], IRRender::kGuiTextDistance
        ));
    }
}

// Appends a voxel run's face runs (layoutVoxelRun) as solid-cell commands
// at `position`, the lit group in `lit` and the rest in `unlit`.
inline void expandVoxelRunToCommands(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    const IRRender::VoxelTextLayout &layout,
    IRMath::ivec2 position,
    IRMath::ivec2 canvasSize,
    const IRRender::VoxelFaceColors &lit,
    const IRRender::VoxelFaceColors &unlit
) {
    const IRMath::ivec2 origin = IRRender::parityAlignedPosition(position, canvasSize);
    const std::uint32_t packedByFace[6] = {
        lit.forFace(IRMath::kXFace).toPackedRGBA(),
        lit.forFace(IRMath::kYFace).toPackedRGBA(),
        lit.forFace(IRMath::kZFace).toPackedRGBA(),
        unlit.forFace(IRMath::kXFace).toPackedRGBA(),
        unlit.forFace(IRMath::kYFace).toPackedRGBA(),
        unlit.forFace(IRMath::kZFace).toPackedRGBA(),
    };
    for (const IRRender::VoxelTextRun &run : layout.runs_) {
        commands.push_back(solidCellCommand(
            origin + run.pos_, run.size_, packedByFace[run.face_], IRRender::kGuiTextDistance
        ));
    }
}

// Convenience append for call sites that already hold the GUI canvas size
// (widget render systems, editor overlays). No-op for empty text.
inline void queueGuiText(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    std::string_view text,
    IRMath::ivec2 pos,
    IRMath::ivec2 canvasSize,
    IRMath::Color color,
    int fontSize,
    IRComponents::TextAlignH alignH = IRComponents::TextAlignH::LEFT,
    IRComponents::TextAlignV alignV = IRComponents::TextAlignV::TOP,
    int boxWidth = 0,
    int boxHeight = 0,
    int wrapWidth = 0,
    IRRender::TextSize size = IRRender::TextSize::LARGE
) {
    if (text.empty())
        return;
    expandTextToCommands(
        commands,
        text,
        pos,
        canvasSize,
        color,
        wrapWidth,
        alignH,
        alignV,
        boxWidth,
        boxHeight,
        fontSize,
        size
    );
}

// Marks every queued glyph for lattice-aware scaling when `canvas` displays
// lattice triangles; a rectangular (full-resolution) canvas keeps block scaling.
inline void stampGlyphLayout(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    const IRComponents::C_TriangleCanvasTextures &canvas
) {
    if (canvas.renderedSampleLayout_ != IRRender::TrixelSampleLayout::LOCAL_TRIANGLES)
        return;
    for (auto &cmd : commands)
        cmd.styleFlags |= IRRender::kGlyphStyleLatticeScale;
}

// Rasterizes every command with the bound text program onto the bound colour
// and distance images, uploading through `cmdBuf` (kMaxGlyphCommands capacity,
// bound at kBufferIndex_GlyphDrawCommands) and dispatching one chunk at a time.
// Re-uploading after a queued dispatch is safe on both backends: GL orders
// buffer updates after prior reads, and Metal orphans an encoded buffer.
inline void dispatchGlyphCommands(
    const IRRender::Buffer &cmdBuf, std::span<const IRRender::GlyphDrawCommand> commands
) {
    for (std::size_t first = 0; first < commands.size(); first += kMaxGlyphCommands) {
        const std::size_t count =
            IRMath::min(commands.size() - first, static_cast<std::size_t>(kMaxGlyphCommands));
        cmdBuf.subData(0, count * sizeof(IRRender::GlyphDrawCommand), commands.data() + first);
        IRRender::device()->dispatchCompute(static_cast<std::uint32_t>(count), 1, 1);
        IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
    }
}

// Uploads `commands` to the shared glyph-command SSBO and rasterizes every
// queued glyph onto the GUI canvas (dispatchGlyphCommands), then clears
// `commands`. No-op (and clears) when the shared text resources
// or the GUI canvas are absent — e.g. a creation that runs widget systems
// without TEXT_TO_TRIXEL, which would not show free text either. That no-op is
// why the resource lookups are `getNamedResourceOrNull`: the asserting
// `getNamedResource` cannot report an absent resource, only throw on it. Call
// from a render system's endTick, after queuing that system's
// text in its ticks.
inline void dispatchGuiText(std::vector<IRRender::GlyphDrawCommand> &commands) {
    if (commands.empty())
        return;

    auto *program =
        IRRender::getNamedResourceOrNull<IRRender::ShaderProgram>("TextToTrixelProgram");
    auto *cmdBuf = IRRender::getNamedResourceOrNull<IRRender::Buffer>("GlyphDrawCommandBuffer");
    const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
    if (program == nullptr || cmdBuf == nullptr || guiCanvas == IREntity::kNullEntity) {
        commands.clear();
        return;
    }
    auto &canvas = IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);

    stampGlyphLayout(commands, canvas);
    program->use();
    canvas.getTextureColorsForGeometryWrite()
        ->bindAsImage(0, IRRender::TextureAccess::WRITE_ONLY, IRRender::TextureFormat::RGBA8);
    canvas.getTextureDistances()
        ->bindAsImage(1, IRRender::TextureAccess::WRITE_ONLY, IRRender::TextureFormat::R32I);
    dispatchGlyphCommands(*cmdBuf, commands);

    commands.clear();
}

} // namespace IRPrefab::GuiText

#endif /* GUI_TEXT_BATCH_H */
