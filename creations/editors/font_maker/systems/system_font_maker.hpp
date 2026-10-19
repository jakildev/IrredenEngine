#ifndef FONT_MAKER_SYSTEM_FONT_MAKER_H
#define FONT_MAKER_SYSTEM_FONT_MAKER_H

#include "../font_editor.hpp"
#include "../glyph_raster.hpp"

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gui_canvas.hpp>
#include <irreden/render/layout.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/widgets.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The font maker's two systems: an UPDATE system that polls the widgets and
// turns clicks on the character table and edit grid into editor actions, and
// a RENDER system that rasterizes the table, the magnified grid and the
// sample strip from the working bitmaps onto the GUI canvas every frame.
namespace FontMaker {

using IRMath::Color;
using IRMath::ivec2;

constexpr Color kPanelColor{20, 22, 30, 255};
constexpr Color kCellColor{34, 37, 48, 255};
constexpr Color kCellEmptyColor{38, 44, 84, 255};
constexpr Color kCellMissingColor{116, 30, 34, 255};
constexpr Color kCellMalformedColor{130, 82, 20, 255};
constexpr Color kCellSelectedColor{226, 186, 70, 255};
constexpr Color kFlagColor{240, 84, 70, 255};
constexpr Color kDirtyColor{90, 200, 120, 255};
constexpr Color kGlyphColor{226, 230, 238, 255};
constexpr Color kGridLeftColor{50, 55, 70, 255};
constexpr Color kGridRightColor{36, 40, 52, 255};
constexpr Color kGridHoverColor{96, 104, 128, 255};
constexpr Color kSampleLargeColor{226, 230, 238, 255};
constexpr Color kSampleSmallColor{150, 210, 236, 255};

constexpr int kTableColumns = 16;
constexpr ivec2 kTableOrigin{4, 42};
constexpr ivec2 kTableCell{10, 16};
constexpr int kSampleTop = 240;
constexpr int kSampleHeight = 78;
// Below this canvas width the grid sits under the table; at or above it the
// grid takes the right-hand column.
constexpr int kWideCanvasWidth = 480;

struct Layout {
    ivec2 canvasSize_{0};
    ivec2 gridRegion_{0};
    ivec2 gridRegionSize_{0};
    ivec2 gridOrigin_{0};
    int gridScale_ = 1;
    ivec2 sampleRegion_{0};
    ivec2 sampleRegionSize_{0};
    ivec2 sampleLargeOrigin_[2]{};
    ivec2 sampleSmallOrigin_[2]{};
};

struct Widgets {
    IREntity::EntityId setLarge_ = IREntity::kNullEntity;
    IREntity::EntityId setSmall_ = IREntity::kNullEntity;
    IREntity::EntityId save_ = IREntity::kNullEntity;
    IREntity::EntityId revert_ = IREntity::kNullEntity;
    IREntity::EntityId reload_ = IREntity::kNullEntity;
    IREntity::EntityId scale1_ = IREntity::kNullEntity;
    IREntity::EntityId scale2_ = IREntity::kNullEntity;
    IREntity::EntityId composite_ = IREntity::kNullEntity;
    IREntity::EntityId sampleText_ = IREntity::kNullEntity;
    IREntity::EntityId info_[4]{};
    IREntity::EntityId status_ = IREntity::kNullEntity;
};

// What the RENDER system drew last frame, for hit testing and assertions.
struct Frame {
    LatticeImage table_;
    LatticeImage grid_;
    LatticeImage sample_;
    std::vector<IRRender::GlyphTexelFinding> findings_;
    std::vector<char> duplicates_;
    std::vector<bool> flaggedTexels_;
};

struct App {
    Editor editor_;
    Widgets widgets_;
    Layout layout_;
    Frame frame_;
    ivec2 hoverTexel_{-1};
    IRRender::RectFillScratch scratch_;
};

inline IRComponents::C_TriangleCanvasTextures &guiCanvas() {
    return IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(
        IRRender::getCanvas("gui")
    );
}

inline ivec2 tableCellOrigin(int index) {
    return kTableOrigin + ivec2(index % kTableColumns, index / kTableColumns) * kTableCell;
}

inline int tableIndexAt(ivec2 texel, int cellCount) {
    const ivec2 local = texel - kTableOrigin;
    if (local.x < 0 || local.y < 0) {
        return -1;
    }
    const ivec2 cell = local / kTableCell;
    if (cell.x >= kTableColumns) {
        return -1;
    }
    const int index = cell.y * kTableColumns + cell.x;
    return index < cellCount ? index : -1;
}

inline int tableIndexOf(const Editor &editor, char c) {
    for (std::size_t i = 0; i < editor.canonical_.size(); ++i) {
        if (editor.canonical_[i] == c) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline Layout computeLayout(const Editor &editor, ivec2 canvasSize) {
    Layout layout;
    layout.canvasSize_ = canvasSize;
    const GlyphSet &set = editor.activeSet();
    const int width = set.loaded() ? set.source_.width_ : IRRender::kGlyphWidth;
    const int height = set.loaded() ? set.source_.height_ : IRRender::kGlyphHeight;
    if (canvasSize.x >= kWideCanvasWidth) {
        layout.gridRegion_ = ivec2(330, kTableOrigin.y);
        layout.gridRegionSize_ = ivec2(canvasSize.x - 334, canvasSize.y - kTableOrigin.y - 4);
    } else {
        layout.gridRegion_ = ivec2(4, 112);
        layout.gridRegionSize_ = ivec2(kTableColumns * kTableCell.x, kSampleTop - 4 - 112);
    }
    // A texel at lattice scale s spans s columns and the 2s - 1 rows centred
    // on row r * s, so the glyph needs width * s by (height + 1) * s texels.
    layout.gridScale_ = IRMath::clamp(
        IRMath::min(
            (layout.gridRegionSize_.x - 1) / width,
            layout.gridRegionSize_.y / (height + 1)
        ),
        1,
        24
    );
    layout.gridOrigin_ = IRRender::parityAlignedPosition(
        layout.gridRegion_ + ivec2(0, layout.gridScale_ - 1),
        canvasSize
    );

    layout.sampleRegion_ = ivec2(4, kSampleTop);
    layout.sampleRegionSize_ = ivec2(IRMath::min(canvasSize.x, 320) - 8, kSampleHeight);
    const int rowTops[4] = {2, 18, 46, 58};
    for (int scale = 1; scale <= 2; ++scale) {
        layout.sampleLargeOrigin_[scale - 1] = IRRender::parityAlignedPosition(
            layout.sampleRegion_ + ivec2(2, rowTops[scale - 1] + scale - 1),
            canvasSize
        );
        layout.sampleSmallOrigin_[scale - 1] = IRRender::parityAlignedPosition(
            layout.sampleRegion_ + ivec2(2, rowTops[scale + 1] + scale - 1),
            canvasSize
        );
    }
    return layout;
}

// The canvas texel at the middle of source texel (col, row)'s footprint on
// the edit grid: on its source row, half a scale in from its left column.
inline ivec2 gridAimTexel(const Layout &layout, int col, int row) {
    return layout.gridOrigin_ + ivec2(col, row) * layout.gridScale_ +
           ivec2(layout.gridScale_ / 2, 0);
}

inline Color cellColor(GlyphState state) {
    switch (state) {
    case GlyphState::EMPTY:
        return kCellEmptyColor;
    case GlyphState::MISSING:
        return kCellMissingColor;
    case GlyphState::MALFORMED:
        return kCellMalformedColor;
    case GlyphState::OK:
        break;
    }
    return kCellColor;
}

inline void rasterTable(App &app) {
    const Editor &editor = app.editor_;
    const GlyphSet &set = editor.activeSet();
    const std::vector<TableCell> cells = editor.tableCells();
    const int rows = (static_cast<int>(cells.size()) + kTableColumns - 1) / kTableColumns;
    LatticeImage &image = app.frame_.table_;
    image.reset(kTableOrigin, ivec2(kTableColumns, rows) * kTableCell, kPanelColor);

    std::vector<IRRender::GlyphBitmap> bitmaps;
    std::vector<std::size_t> bitmapCell;
    std::vector<bool> flagged(cells.size(), false);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].state_ != GlyphState::OK) {
            continue;
        }
        const IRRender::GlyphBitmap bitmap = set.bitmap(cells[i].glyph_);
        flagged[i] = !IRRender::glyphTexelFindings(set.source_.ruleSet(), bitmap).empty();
        bitmaps.push_back(bitmap);
        bitmapCell.push_back(i);
    }
    for (const auto &[a, b] : IRRender::duplicateBitmapPairs(bitmaps)) {
        flagged[bitmapCell[a]] = true;
        flagged[bitmapCell[b]] = true;
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const TableCell &cell = cells[i];
        const ivec2 origin = tableCellOrigin(static_cast<int>(i));
        const bool selected = cell.char_ == editor.selected_;
        for (int y = 0; y < kTableCell.y - 1; ++y) {
            for (int x = 0; x < kTableCell.x - 1; ++x) {
                const bool edge =
                    x == 0 || y == 0 || x == kTableCell.x - 2 || y == kTableCell.y - 2;
                image.set(
                    origin + ivec2(x, y),
                    selected && edge ? kCellSelectedColor : cellColor(cell.state_),
                    static_cast<int>(i)
                );
            }
        }
        if (cell.glyph_ >= 0 && cell.state_ != GlyphState::MALFORMED) {
            const ivec2 glyphOrigin = IRRender::parityAlignedPosition(
                origin + ivec2(2, kTableCell.y - 3 - set.source_.height_),
                app.layout_.canvasSize_
            );
            rasterGlyphRows(
                image,
                set.working_[cell.glyph_],
                set.source_.width_,
                glyphOrigin,
                1,
                kGlyphColor
            );
        }
        if (flagged[i]) {
            for (int x = 1; x < kTableCell.x - 2; ++x) {
                image.set(origin + ivec2(x, kTableCell.y - 3), kFlagColor, static_cast<int>(i));
            }
        }
        if (set.dirty(cell.glyph_)) {
            image.set(origin + ivec2(kTableCell.x - 3, 1), kDirtyColor, static_cast<int>(i));
            image.set(origin + ivec2(kTableCell.x - 4, 1), kDirtyColor, static_cast<int>(i));
        }
    }
}

inline void rasterGrid(App &app) {
    const Editor &editor = app.editor_;
    const GlyphSet &set = editor.activeSet();
    const Layout &layout = app.layout_;
    LatticeImage &image = app.frame_.grid_;
    // Last frame's owners decide the hovered texel, before the reset clears them.
    const int hovered = image.ownerAt(app.hoverTexel_);
    image.reset(layout.gridRegion_, layout.gridRegionSize_, kPanelColor);
    app.frame_.findings_ = editor.selectedFindings();
    app.frame_.duplicates_ = editor.duplicatesOf(editor.selected_);
    const int glyph = editor.selectedGlyph();
    if (!set.loaded()) {
        return;
    }
    const bool editable = glyph >= 0 && set.source_.glyphs_[glyph].malformed_.empty();
    std::vector<bool> &flaggedTexels = app.frame_.flaggedTexels_;
    flaggedTexels.assign(static_cast<std::size_t>(set.source_.width_ * set.source_.height_), false);
    for (const IRRender::GlyphTexelFinding &finding : app.frame_.findings_) {
        flaggedTexels[static_cast<std::size_t>(finding.row_ * set.source_.width_ + finding.col_)] =
            true;
    }
    for (int row = 0; row < set.source_.height_; ++row) {
        for (int col = 0; col < set.source_.width_; ++col) {
            const int owner = row * set.source_.width_ + col;
            const bool lit =
                editable &&
                IRRender::glyphRowPixel(set.working_[glyph][row], set.source_.width_, col);
            const bool flagged = flaggedTexels[static_cast<std::size_t>(owner)];
            Color color = (col + row) % 2 == 0 ? kGridLeftColor : kGridRightColor;
            if (lit) {
                color = flagged ? kFlagColor : kGlyphColor;
            } else if (flagged) {
                color = kFlagColor;
            } else if (owner == hovered) {
                color = kGridHoverColor;
            }
            forEachLatticeTexel(layout.gridOrigin_, col, row, layout.gridScale_, [&](ivec2 texel) {
                image.set(texel, color, owner);
            });
        }
    }
}

inline void rasterSample(App &app, const std::string &text) {
    const Editor &editor = app.editor_;
    const Layout &layout = app.layout_;
    LatticeImage &image = app.frame_.sample_;
    image.reset(layout.sampleRegion_, layout.sampleRegionSize_, kPanelColor);
    for (int scale = 1; scale <= 2; ++scale) {
        rasterText(
            image,
            editor.sets_[Editor::kLargeSlot],
            text,
            layout.sampleLargeOrigin_[scale - 1],
            scale,
            kSampleLargeColor
        );
        rasterText(
            image,
            editor.sets_[Editor::kSmallSlot],
            text,
            layout.sampleSmallOrigin_[scale - 1],
            scale,
            kSampleSmallColor
        );
    }
}

// Uploads the part of `image` inside the canvas at the widget border
// distance: one subImage2D per channel.
inline void uploadImage(
    IRComponents::C_TriangleCanvasTextures &canvas,
    const LatticeImage &image,
    IRRender::RectFillScratch &scratch
) {
    ivec2 lo, hi;
    if (!IRRender::clipRectToCanvas(image.origin_, image.size_, canvas.size_, lo, hi)) {
        return;
    }
    const ivec2 size = hi - lo;
    scratch.colors_.resize(static_cast<std::size_t>(size.x * size.y));
    scratch.distances_.assign(scratch.colors_.size(), IRRender::kWidgetBorderDistance);
    for (int y = 0; y < size.y; ++y) {
        const ivec2 local = ivec2(lo.x, lo.y + y) - image.origin_;
        std::copy_n(
            image.colors_.begin() + static_cast<std::ptrdiff_t>(local.y * image.size_.x + local.x),
            size.x,
            scratch.colors_.begin() + static_cast<std::ptrdiff_t>(y * size.x)
        );
    }
    canvas.getTextureColorsForGeometryWrite()->subImage2D(
        lo.x,
        lo.y,
        size.x,
        size.y,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        scratch.colors_.data()
    );
    canvas.textureTriangleDistances_.second->subImage2D(
        lo.x,
        lo.y,
        size.x,
        size.y,
        IRRender::PixelDataFormat::RED_INTEGER,
        IRRender::PixelDataType::INT32,
        scratch.distances_.data()
    );
}

// What the sample strip drew: a hash of where the LARGE texels are, and the
// LARGE and SMALL texel counts.
struct SampleStats {
    std::uint64_t largeHash_ = 1469598103934665603ull;
    int largeLit_ = 0;
    int smallLit_ = 0;
};

inline SampleStats sampleStats(const LatticeImage &sample) {
    SampleStats stats;
    for (const Color &color : sample.colors_) {
        const std::uint32_t packed = color.toPackedRGBA();
        const bool large = packed == kSampleLargeColor.toPackedRGBA();
        stats.largeHash_ = (stats.largeHash_ ^ (large ? 1u : 0u)) * 1099511628211ull;
        stats.largeLit_ += large ? 1 : 0;
        stats.smallLit_ += packed == kSampleSmallColor.toPackedRGBA() ? 1 : 0;
    }
    return stats;
}

inline std::string findingsText(const Frame &frame) {
    if (frame.findings_.empty() && frame.duplicates_.empty()) {
        return "RULES OK";
    }
    std::string text;
    for (const IRRender::GlyphTexelFinding &finding : frame.findings_) {
        if (text.size() > 40) {
            text += " ...";
            break;
        }
        std::string name = IRRender::glyphRuleName(finding.rule_);
        for (char &c : name) {
            c = static_cast<char>(c == '_' ? ' ' : c);
        }
        text +=
            name + " " + std::to_string(finding.col_) + "," + std::to_string(finding.row_) + " ";
    }
    for (char other : frame.duplicates_) {
        text += "SAME AS " + characterName(other) + " ";
    }
    return text;
}

inline std::string aliasText(const Editor &editor) {
    const GlyphSet &set = editor.activeSet();
    const int glyph = editor.selectedGlyph();
    std::string aliases;
    for (int ascii = 0; ascii < 128; ++ascii) {
        const char c = static_cast<char>(ascii);
        if (c != editor.selected_ && glyph >= 0 && set.source_.glyphFor(c) == glyph) {
            aliases += c;
        }
    }
    return aliases;
}

inline void refreshLabels(App &app) {
    const Editor &editor = app.editor_;
    const GlyphSet &set = editor.activeSet();
    const int glyph = editor.selectedGlyph();
    const std::string setName = editor.active_ == Editor::kSmallSlot ? "SMALL" : "LARGE";
    std::string header = set.loaded() ? set.source_.path_.filename().string() : "NO HEADER";
    if (editor.custom_[editor.active_]) {
        header = "CUSTOM " + header;
    }
    IRPrefab::Widget::setLabelText(app.widgets_.info_[0], setName + " " + header);
    std::string charLine = "CHAR " + characterName(editor.selected_);
    if (glyph >= 0) {
        charLine += " = " + set.source_.prefix_ + set.source_.glyphs_[glyph].name_;
    }
    const std::string aliases = aliasText(editor);
    if (!aliases.empty()) {
        charLine += " ALSO " + aliases;
    }
    IRPrefab::Widget::setLabelText(app.widgets_.info_[1], charLine);
    std::string state = glyphStateName(set.state(editor.selected_));
    for (char &c : state) {
        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    if (set.dirty(glyph)) {
        state += " UNSAVED";
    }
    if (glyph >= 0 && !set.source_.glyphs_[glyph].malformed_.empty()) {
        state += ": " + set.source_.glyphs_[glyph].malformed_;
    }
    IRPrefab::Widget::setLabelText(app.widgets_.info_[2], state);
    IRPrefab::Widget::setLabelText(app.widgets_.info_[3], findingsText(app.frame_));
    IRPrefab::Widget::setLabelText(app.widgets_.status_, editor.status_);
    IRPrefab::Widget::setButtonLabel(
        app.widgets_.composite_,
        IRPrefab::GuiCanvas::isPixelated() ? "PIXEL" : "SCREEN"
    );
}

inline void pollWidgets(App &app) {
    namespace Widget = IRPrefab::Widget;
    Editor &editor = app.editor_;
    if (Widget::wasClicked(app.widgets_.setLarge_)) {
        editor.active_ = Editor::kLargeSlot;
    }
    if (Widget::wasClicked(app.widgets_.setSmall_)) {
        editor.active_ = Editor::kSmallSlot;
    }
    if (Widget::wasClicked(app.widgets_.save_)) {
        editor.saveSelected();
    }
    if (Widget::wasClicked(app.widgets_.revert_)) {
        editor.revertSelected();
        editor.status_ = "REVERTED " + characterName(editor.selected_);
    }
    if (Widget::wasClicked(app.widgets_.reload_)) {
        const std::filesystem::path path = editor.activeSet().source_.path_;
        if (!path.empty()) {
            editor.openHeader(path, editor.custom_[editor.active_]);
        }
    }
    if (Widget::wasClicked(app.widgets_.scale1_)) {
        IRRender::setGuiScale(1);
    }
    if (Widget::wasClicked(app.widgets_.scale2_)) {
        IRRender::setGuiScale(2);
    }
    if (Widget::wasClicked(app.widgets_.composite_)) {
        IRPrefab::GuiCanvas::setPixelated(!IRPrefab::GuiCanvas::isPixelated());
    }
}

// Left press on a table cell selects its character; on the grid it flips the
// source texel whose triangle was hit.
inline void handlePointer(App &app) {
    const IRMath::vec2 mouse = IRPrefab::Layout::mousePositionInGuiTrixels();
    app.hoverTexel_ =
        ivec2(static_cast<int>(IRMath::floor(mouse.x)), static_cast<int>(IRMath::floor(mouse.y)));
    if (!IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED)) {
        return;
    }
    Editor &editor = app.editor_;
    const int cell = tableIndexAt(app.hoverTexel_, static_cast<int>(editor.canonical_.size()));
    if (cell >= 0) {
        editor.selected_ = editor.canonical_[cell];
        return;
    }
    const int owner = app.frame_.grid_.ownerAt(app.hoverTexel_);
    const int width = editor.activeSet().source_.width_;
    if (owner >= 0 && width > 0 && editor.toggle(owner % width, owner / width)) {
        editor.status_ = "TOGGLED " + characterName(editor.selected_) + " " +
                         std::to_string(owner % width) + "," + std::to_string(owner / width);
    }
}

inline IRSystem::SystemId createInputSystem(App &app) {
    return IRSystem::createSystem<IRComponents::C_GuiElement>(
        "FontMakerInput",
        [](const IRComponents::C_GuiElement &) {},
        [&app]() {
            pollWidgets(app);
            handlePointer(app);
            refreshLabels(app);
        }
    );
}

inline IRSystem::SystemId createRenderSystem(App &app) {
    return IRSystem::createSystem<IRComponents::C_GuiElement>(
        "FontMakerRender",
        [](const IRComponents::C_GuiElement &) {},
        nullptr,
        [&app]() {
            IRComponents::C_TriangleCanvasTextures &canvas = guiCanvas();
            app.layout_ = computeLayout(app.editor_, canvas.size_);
            rasterTable(app);
            rasterGrid(app);
            rasterSample(app, IRPrefab::Widget::textInputValue(app.widgets_.sampleText_));
            for (const LatticeImage *image :
                 {&app.frame_.table_, &app.frame_.grid_, &app.frame_.sample_}) {
                uploadImage(canvas, *image, app.scratch_);
            }
        }
    );
}

inline void createWidgets(App &app) {
    namespace Widget = IRPrefab::Widget;
    constexpr auto kSmall = IRRender::TextSize::SMALL;
    Widgets &w = app.widgets_;
    w.setLarge_ = Widget::makeButton(ivec2(4, 4), ivec2(40, 14), "LARGE", kSmall);
    w.setSmall_ = Widget::makeButton(ivec2(48, 4), ivec2(40, 14), "SMALL", kSmall);
    w.save_ = Widget::makeButton(ivec2(96, 4), ivec2(36, 14), "SAVE", kSmall);
    w.revert_ = Widget::makeButton(ivec2(136, 4), ivec2(48, 14), "REVERT", kSmall);
    w.reload_ = Widget::makeButton(ivec2(188, 4), ivec2(48, 14), "RELOAD", kSmall);
    w.scale1_ = Widget::makeButton(ivec2(4, 22), ivec2(52, 14), "SCALE 1", kSmall);
    w.scale2_ = Widget::makeButton(ivec2(60, 22), ivec2(52, 14), "SCALE 2", kSmall);
    w.composite_ = Widget::makeButton(ivec2(116, 22), ivec2(52, 14), "SCREEN", kSmall);
    for (int i = 0; i < 4; ++i) {
        w.info_[i] = Widget::makeLabel(ivec2(172, 44 + 12 * i), "", Color{0, 0, 0, 0}, kSmall);
    }
    w.sampleText_ = Widget::makeTextInput(ivec2(4, 320), ivec2(200, 16), "HELLO, WORLD 2468?", 40);
    w.status_ = Widget::makeLabel(ivec2(4, 342), "", Color{0, 0, 0, 0}, kSmall);
}

} // namespace FontMaker

#endif /* FONT_MAKER_SYSTEM_FONT_MAKER_H */
