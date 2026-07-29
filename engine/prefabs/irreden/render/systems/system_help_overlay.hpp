#ifndef SYSTEM_HELP_OVERLAY_H
#define SYSTEM_HELP_OVERLAY_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_help_overlay.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/help_overlay_state.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/widget_theme.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace IRSystem {

// Outer padding from the canvas top-left corner. The perf-stats overlay owns
// the top-right; these two are the GUI canvas's two reserved corners.
inline constexpr IRMath::ivec2 kHelpOverlayPadding{12, 12};

// Inner padding between the text block and the background panel edge.
inline constexpr IRMath::ivec2 kHelpOverlayBgPadding{8, 6};

// Column at which the description starts, in characters from the line's left
// edge. Wide enough for "SHIFT+BACKSPACE" (15) plus a separating space, so
// every description lands on the same x — a ragged second column is much
// harder to scan than a slightly wide gutter.
inline constexpr int kHelpOverlayBindingColumnChars = 16;

// Font scale for overlay text. The trixel font is uppercase-only and 7x11 at
// scale 1; the GUI canvas is `mainCanvasSize / guiScale`, so scale 1 keeps a
// ~40-command list inside a 480-trixel canvas.
inline constexpr int kHelpOverlayFontSize = 1;

// Registry-driven command help overlay (#2550).
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

    // Text is rebuilt only when the command registry actually grew — without
    // that signal, a command registered after the first visible frame would
    // never appear. `kUnbuiltGeneration` forces the first build even against a
    // world with zero registered commands.
    static constexpr std::uint32_t kUnbuiltGeneration = ~std::uint32_t{0};
    std::uint32_t cachedGeneration_ = kUnbuiltGeneration;
    std::string text_;
    int lineCount_ = 0;
    int maxLineChars_ = 0;

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
        if (generation != cachedGeneration_) {
            buildText();
            cachedGeneration_ = generation;
        }
        if (text_.empty()) {
            return;
        }

        paintBackground();

        IRPrefab::GuiText::queueGuiText(
            textCmds_,
            text_,
            kHelpOverlayPadding,
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
        return registerSystem<HELP_OVERLAY, IRComponents::C_HelpOverlayState>("HelpOverlay");
    }

  private:
    // One line per registered PRESSED binding, key column left-aligned to a
    // fixed width so the descriptions form a readable second column:
    //
    //   COMMANDS
    //
    //   ESC             CLOSE WINDOW - CLOSE THE WINDOW AND EXIT
    //   W               CAMERA UP - PAN THE CAMERA UP WHILE HELD
    //   F1              TOGGLE HELP - SHOW OR HIDE THIS COMMAND LIST
    //
    // The header deliberately does NOT name the close key: the toggle binds
    // through the same named path as everything else, so it appears as its own
    // row above with whatever key this creation chose (F1 by default, but
    // random_voxels uses G and two others use backtick). A hardcoded
    // "(F1 TO CLOSE)" header would be wrong in three of the five adopters.
    //
    // Built only on a registry-generation change, so this allocates at most
    // once per registration burst rather than per frame.
    void buildText() {
        const auto &registrations = IRCommand::getCommandManager().getCommandRegistrations();

        text_.clear();
        lineCount_ = 0;
        maxLineChars_ = 0;
        if (registrations.empty()) {
            return;
        }

        text_.reserve(registrations.size() * 64 + 32);
        appendLine("COMMANDS");
        appendLine("");

        for (const auto &registration : registrations) {
            std::string line = IRCommand::modifierString(registration.requiredModifiers) +
                               IRCommand::keyButtonToString(registration.button);
            // Pad to the description column. A longer binding simply pushes
            // its own description right by one space rather than truncating.
            while (static_cast<int>(line.size()) < kHelpOverlayBindingColumnChars - 1) {
                line += ' ';
            }
            line += ' ';
            line += registration.name;
            if (!registration.description.empty()) {
                line += " - ";
                line += registration.description;
            }
            appendLine(line);
        }
    }

    void appendLine(const std::string &line) {
        text_ += line;
        text_ += '\n';
        ++lineCount_;
        maxLineChars_ = IRMath::max(maxLineChars_, static_cast<int>(line.size()));
    }

    // Panel behind the text. `measureText` has no fontSize parameter (it
    // measures at scale 1), so the extents are derived from the character
    // counts collected during the build and scaled here — keeping the two in
    // sync through one multiply rather than a second measuring pass.
    void paintBackground() {
        const int textW = maxLineChars_ * IRRender::kGlyphStepX * kHelpOverlayFontSize;
        // Standard N-line block height: N-1 inter-line steps plus one glyph
        // row. `kGlyphStepY` includes the line gap, so using it for the last
        // row too would pad the panel by that gap.
        const int textH = (lineCount_ - 1) * IRRender::kGlyphStepY * kHelpOverlayFontSize +
                          IRRender::kGlyphHeight * kHelpOverlayFontSize;

        const IRMath::ivec2 bgPos{
            kHelpOverlayPadding.x - kHelpOverlayBgPadding.x,
            kHelpOverlayPadding.y - kHelpOverlayBgPadding.y
        };
        const IRMath::ivec2 bgSize{
            textW + 2 * kHelpOverlayBgPadding.x,
            textH + 2 * kHelpOverlayBgPadding.y
        };

        IRRender::fillRect(
            *canvas_,
            bgPos,
            bgSize,
            theme_.panelBackground_,
            IRRender::kWidgetBackgroundDistance,
            bgScratch_
        );
        IRRender::drawBorder(
            *canvas_,
            bgPos,
            bgSize,
            theme_.borderIdle_,
            IRRender::kWidgetBorderDistance,
            theme_.borderThickness_,
            bgScratch_
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HELP_OVERLAY_H */
