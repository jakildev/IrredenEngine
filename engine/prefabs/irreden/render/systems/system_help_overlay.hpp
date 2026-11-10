#ifndef SYSTEM_HELP_OVERLAY_H
#define SYSTEM_HELP_OVERLAY_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_help_overlay.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/help_overlay_layout.hpp>
#include <irreden/render/help_overlay_state.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/widget_theme.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IRSystem {

// Outer padding from the canvas top-left corner. The perf-stats overlay owns
// the top-right and the perf HUD defaults to the bottom-right, so the three
// engine overlays never share a corner.
inline constexpr IRMath::ivec2 kHelpOverlayPadding{12, 12};

// Inner padding between the text block and the background panel edge.
inline constexpr IRMath::ivec2 kHelpOverlayBgPadding{8, 6};

// Column at which the description starts, in characters from the line's left
// edge. Wide enough for "SHIFT+BACKSPACE" (15) plus a separating space, so
// every description lands on the same x — a ragged second column is much
// harder to scan than a slightly wide gutter.
inline constexpr int kHelpOverlayBindingColumnChars = 16;

// Font scale for overlay text. The trixel font is uppercase-only and 6x11 at
// scale 1; the GUI canvas is `mainCanvasSize / guiScale`, so scale 1 keeps a
// ~40-command list inside a 480-trixel canvas.
inline constexpr int kHelpOverlayFontSize = 1;

// Registry-driven command help overlay.
//
// Iterates the `C_HelpOverlayState` singleton, so visibility arrives through
// dense-column iteration rather than a per-frame singleton lookup, and the
// system's whole cost when hidden is that one-row tick plus two early
// returns — no string build, no fillRect, no glyph batching.
//
// Content comes from `CommandManager`'s registration list, which every
// `createCommand<NAME>(...)` populates automatically. The camera-control
// bundle therefore appears, described, in any creation that calls
// `registerStandardKeyboardCommands()` with zero per-demo wiring.
//
// Pipeline placement is a hard invariant: register AFTER `TEXT_TO_TRIXEL`
// (it clears the GUI canvas and owns TextToTrixelProgram /
// GlyphDrawCommandBuffer, which `dispatchGuiText` reuses) and BEFORE
// `TRIXEL_TO_FRAMEBUFFER`. `IRPrefab::HelpOverlay::systems()` splices the
// pair in the right order for creations that don't already have text.
template <> struct System<HELP_OVERLAY> {
    // Resolved once per VISIBLE frame in endTick (see the beginTick note).
    // Re-resolved rather than cached across frames so a canvas swap is picked
    // up; null until the overlay is first opened.
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;

    // Text is rebuilt only when one of its two inputs changed: the command
    // registry grew — without that signal, a command registered after the
    // first visible frame would never appear — or the GUI canvas changed
    // dimensions, which determine wrapping and column count. `kUnbuiltGeneration`
    // forces the first build even against a world with zero registered
    // commands.
    static constexpr std::uint32_t kUnbuiltGeneration = ~std::uint32_t{0};
    std::uint32_t cachedGeneration_ = kUnbuiltGeneration;
    IRMath::ivec2 cachedCanvasSize_{};
    IRPrefab::HelpOverlay::WrappedText layout_;
    IRPrefab::HelpOverlay::WrappedText secondColumn_;
    int secondColumnOffset_ = 0;

    // Background panel as last painted, in GUI-canvas trixels. Zero-sized
    // until the overlay is first drawn.
    IRMath::ivec2 panelPos_ = IRMath::ivec2(0);
    IRMath::ivec2 panelSize_ = IRMath::ivec2(0);

    bool visible_ = false;

    // Glyph commands batched on the most recent frame — 0 whenever the overlay
    // is hidden. `dispatchGuiText` clears the command vector as it uploads, so
    // this is the only after-the-fact evidence that the overlay actually
    // emitted geometry rather than merely believing itself visible. Read by the
    // headless GUI test (`IRPrefab::HelpOverlay::lastGlyphCommandCount()`);
    // one integer store per visible frame.
    int lastGlyphCommandCount_ = 0;

    IRRender::RectFillScratch bgScratch_;
    std::vector<IRRender::GlyphDrawCommand> textCmds_;

    // Hidden-frame cost is this one store, the one-row tick below, and a
    // single branch in endTick. The GUI-canvas lookup and the theme copy
    // deliberately do NOT live here: they would run every frame regardless of
    // visibility, which is what "zero-cost while hidden" rules out. They are
    // resolved in endTick, past the gate, still exactly once per visible frame.
    void beginTick() {
        visible_ = false;
    }

    void tick(const IRComponents::C_HelpOverlayState &state) {
        visible_ = state.visible_;
    }

    void endTick() {
        lastGlyphCommandCount_ = 0;
        if (!visible_) {
            return;
        }

        const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        if (guiCanvas == IREntity::kNullEntity) {
            return;
        }
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();

        const std::uint32_t generation = IRCommand::getCommandManager().getRegistrationGeneration();
        if (generation != cachedGeneration_ || canvas_->size_ != cachedCanvasSize_) {
            buildText();
            cachedGeneration_ = generation;
            cachedCanvasSize_ = canvas_->size_;
        }
        if (layout_.text_.empty()) {
            return;
        }

        paintBackground();

        // `layout_` is already wrapped to the canvas, so the batch's own
        // wrapWidth stays at its default 0: wrapping twice would re-break
        // lines that sit exactly at the budget.
        IRPrefab::GuiText::queueGuiText(
            textCmds_,
            layout_.text_,
            kHelpOverlayPadding,
            canvas_->size_,
            theme_.textIdle_,
            kHelpOverlayFontSize
        );
        if (!secondColumn_.text_.empty())
            IRPrefab::GuiText::queueGuiText(
                textCmds_,
                secondColumn_.text_,
                kHelpOverlayPadding + IRMath::ivec2(secondColumnOffset_, 0),
                canvas_->size_,
                theme_.textIdle_,
                kHelpOverlayFontSize
            );
        lastGlyphCommandCount_ = static_cast<int>(textCmds_.size());
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        // Registration-time singleton touches, for the same main-thread
        // reason `widget_theme.hpp` documents: `singleton<T>()`'s first call
        // is a `createEntity`, which must not first run inside a beginTick.
        // Touching the state singleton here is also what puts the row in the
        // archetype this system iterates, so the tick fires from frame 1.
        IRPrefab::HelpOverlay::ensureStateSingleton();
        IRPrefab::Widget::ensureThemeSingleton();
        return registerSystem<
            HELP_OVERLAY,
            IRComponents::C_HelpOverlayState,
            AlsoWrites<IRComponents::C_TriangleCanvasTextures>>("HelpOverlay");
    }

  private:
    void buildText() {
        const std::span rows{IRCommand::getCommandManager().getCommandRegistrations()};
        layout_.clear();
        secondColumn_.clear();
        secondColumnOffset_ = 0;
        if (rows.empty())
            return;

        constexpr int charWidth = IRRender::kGlyphStepX * kHelpOverlayFontSize;
        const int fullWidth =
            IRMath::max(1, (canvas_->size_.x - 2 * kHelpOverlayPadding.x) / charWidth);
        const auto writeColumn = [](IRPrefab::HelpOverlay::WrappedText &output,
                                    auto registrations,
                                    int width,
                                    bool title) {
            output.clear();
            if (title) {
                output.appendRow("", "COMMANDS", 0, width);
                output.appendLine("");
            }
            std::string body;
            for (const auto &row : registrations) {
                body = row.name;
                if (!row.description.empty())
                    body += " - " + row.description;
                output.appendRow(
                    IRCommand::modifierString(row.requiredModifiers) +
                        IRCommand::keyButtonToString(row.button),
                    body,
                    kHelpOverlayBindingColumnChars,
                    width
                );
            }
        };
        writeColumn(layout_, rows, fullWidth, true);
        const int singleHeight =
            IRRender::textBlockHeight(layout_.lineCount_, kHelpOverlayFontSize);
        constexpr int gapChars = 3;
        const int columnWidth = (fullWidth - gapChars) / 2;
        if (singleHeight <= canvas_->size_.y - 2 * kHelpOverlayPadding.y || rows.size() < 2 ||
            columnWidth <
                kHelpOverlayBindingColumnChars + IRPrefab::HelpOverlay::kMinHangingLineChars)
            return;

        // Split at a command boundary so a wrapped description always stays
        // with its shortcut. Keep the one-column wrap if splitting makes it taller.
        const auto split = rows.size() / 2;
        writeColumn(layout_, rows.first(split), columnWidth, true);
        writeColumn(secondColumn_, rows.subspan(split), columnWidth, false);
        const auto height = IRRender::textBlockHeight(
            IRMath::max(layout_.lineCount_, secondColumn_.lineCount_),
            kHelpOverlayFontSize
        );
        if (height >= singleHeight) {
            writeColumn(layout_, rows, fullWidth, true);
            secondColumn_.clear();
            return;
        }
        secondColumnOffset_ = (columnWidth + gapChars) * charWidth;
    }

    // Panel behind the text. `measureText` has no fontSize parameter (it
    // measures at scale 1), so the extents are derived from the character
    // counts collected during the build and scaled here — keeping the two in
    // sync through one multiply rather than a second measuring pass.
    void paintBackground() {
        constexpr int charWidth = IRRender::kGlyphStepX * kHelpOverlayFontSize;
        const int textW = IRMath::max(
            layout_.maxLineChars_ * charWidth,
            secondColumnOffset_ + secondColumn_.maxLineChars_ * charWidth
        );
        const int textH = IRRender::textBlockHeight(
            IRMath::max(layout_.lineCount_, secondColumn_.lineCount_),
            kHelpOverlayFontSize
        );

        panelPos_ = IRMath::ivec2{
            kHelpOverlayPadding.x - kHelpOverlayBgPadding.x,
            kHelpOverlayPadding.y - kHelpOverlayBgPadding.y
        };
        panelSize_ =
            IRMath::ivec2{textW + 2 * kHelpOverlayBgPadding.x, textH + 2 * kHelpOverlayBgPadding.y};

        IRRender::fillRect(
            *canvas_,
            panelPos_,
            panelSize_,
            theme_.panelBackground_,
            IRRender::kWidgetBackgroundDistance,
            bgScratch_
        );
        IRRender::drawBorder(
            *canvas_,
            panelPos_,
            panelSize_,
            theme_.borderIdle_,
            IRRender::kWidgetBorderDistance,
            theme_.borderThickness_,
            bgScratch_
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HELP_OVERLAY_H */
