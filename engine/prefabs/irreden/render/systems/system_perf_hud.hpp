#ifndef SYSTEM_PERF_HUD_H
#define SYSTEM_PERF_HUD_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_perf_hud.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/perf_hud_state.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/widget_theme.hpp>

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace IRSystem {

// Outer padding from the anchored canvas corner.
inline constexpr IRMath::ivec2 kPerfHudPadding{12, 12};

// Inner padding between the content block and the panel edge.
inline constexpr IRMath::ivec2 kPerfHudBgPadding{6, 4};

inline constexpr int kPerfHudFontSize = 1;

// Both text lines are formatted to exactly this many characters, so the
// content width is fixed and every value column stays put as digits change.
inline constexpr int kPerfHudLineChars = 16;
inline constexpr int kPerfHudTextLines = 2;

// The frame-time graph is one column per frame and as wide as the text, so
// the window it shows — and the window MIN and DROP are measured over — is
// this many frames: about two seconds at 60 Hz.
inline constexpr int kPerfHudGraphWidth =
    kPerfHudLineChars * IRRender::kGlyphStepX * kPerfHudFontSize;
inline constexpr int kPerfHudGraphHeight = 16;
inline constexpr int kPerfHudGraphGap = 4;
inline constexpr int kPerfHudHistoryFrames = kPerfHudGraphWidth;

// A full-height bar is this many frame budgets; the budget line sits at one.
inline constexpr int kPerfHudGraphBudgets = 2;
static_assert(
    kPerfHudGraphHeight % kPerfHudGraphBudgets == 0,
    "the budget line must land on a whole graph row"
);

// EMA smoothing and display refresh cadence for the two headline numbers,
// the perf-stats overlay's constants: the EMA settles in ~250 ms at 60 Hz
// and the digits rebuild every N frames so they do not twitch.
inline constexpr double kPerfHudEmaAlpha = 0.15;
inline constexpr int kPerfHudRefreshFrames = 4;

// Health bands as a fraction of target: frames per second against the fixed
// target, or the frame budget against a measured frame time.
inline constexpr float kPerfHudGoodFraction = 0.95f;
inline constexpr float kPerfHudWarnFraction = 0.75f;

// Dropped frames inside the window that still read as a warning rather than
// a failure.
inline constexpr int kPerfHudWarnDrops = 2;

inline constexpr IRMath::Color kPerfHudGood{96, 220, 96, 255};
inline constexpr IRMath::Color kPerfHudWarn{240, 200, 64, 255};
inline constexpr IRMath::Color kPerfHudBad{240, 80, 80, 255};
inline constexpr IRMath::Color kPerfHudGraphBackground{14, 16, 22, 255};

// Player-facing frame-rate HUD.
//
// Two text lines and a frame-time graph, anchored to a GUI-canvas corner:
//
//   FPS  60  MIN  58      FPS: smoothed frames per second
//   MS 16.7  DROP  0      MIN: lowest FPS in the graph window
//   ▂▁▂▁▃▂▁▁▁▂▁▁▁▂▁▁▂     MS: smoothed frame time; DROP: frames in the
//                         window past the engine's dropped-frame
//                         threshold (strictly over two budgets)
//
// Every value and every bar is coloured by how it stands against the fixed
// frame budget, so a player reads the state of the frame at a glance without
// knowing the numbers. The perf-stats overlay is the developer's view of the
// same frame: per-stage GPU timing, cull counters, always on while
// registered. This one is a toggle and reads nothing the engine does not
// already measure every frame — registering it enables no timing collection.
//
// Iterates the `C_PerfHudState` singleton, so visibility arrives through
// dense-column iteration. Hidden, the system's cost is the one-row tick plus
// one frame-time store into the history ring: the ring records every frame
// so the graph already holds the seconds that made the player open it.
//
// Pipeline placement is the help overlay's contract: register AFTER
// `TEXT_TO_TRIXEL` (it clears the GUI canvas and owns the text GPU resources
// `dispatchGuiText` reuses) and BEFORE `TRIXEL_TO_FRAMEBUFFER`.
// `IRPrefab::PerfHud::systems()` is the splice.
template <> struct System<PERF_HUD> {
    // Resolved once per VISIBLE frame in endTick, so a canvas swap is picked
    // up; null until the HUD is first shown.
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;

    bool visible_ = false;
    IRComponents::PerfHudCorner corner_ = IRComponents::PerfHudCorner::BOTTOM_RIGHT;

    // RENDER wall-clock frame times, one per frame; `historyHead_` is the
    // next slot to write.
    std::array<float, kPerfHudHistoryFrames> historyMs_{};
    int historyHead_ = 0;
    int historyCount_ = 0;

    // Smoothed headline values. Zeroed while hidden so a reopen seeds from the
    // live frame instead of converging from a stale average.
    double fpsEma_ = 0.0;
    double frameMsEma_ = 0.0;
    int frameCounter_ = 0;

    // The two formatted lines, newline-separated, and the colour each value
    // segment was graded at the last rebuild.
    std::string text_;
    IRMath::Color fpsColor_ = kPerfHudGood;
    IRMath::Color minFpsColor_ = kPerfHudGood;
    IRMath::Color frameMsColor_ = kPerfHudGood;
    IRMath::Color dropsColor_ = kPerfHudGood;

    // Background panel as last painted, in GUI-canvas trixels. Zero-sized
    // until the HUD is first drawn.
    IRMath::ivec2 panelPos_ = IRMath::ivec2(0);
    IRMath::ivec2 panelSize_ = IRMath::ivec2(0);

    // Glyph commands batched on the most recent frame — 0 whenever hidden.
    // `dispatchGuiText` clears the command vector as it uploads, so this is
    // the after-the-fact evidence that the HUD drew rather than merely
    // believed itself visible (`IRPrefab::PerfHud::lastGlyphCommandCount()`).
    int lastGlyphCommandCount_ = 0;

    IRRender::RectFillScratch scratch_;
    std::vector<IRRender::GlyphDrawCommand> textCmds_;
    std::array<IRMath::Color, kPerfHudGraphWidth * kPerfHudGraphHeight> graphPixels_{};

    void beginTick() {
        visible_ = false;
    }

    void tick(const IRComponents::C_PerfHudState &state) {
        visible_ = state.visible_;
        corner_ = state.corner_;
    }

    void endTick() {
        recordFrame(static_cast<float>(IRTime::deltaTime(IRTime::Events::RENDER) * 1000.0));
        lastGlyphCommandCount_ = 0;
        if (!visible_) {
            fpsEma_ = 0.0;
            frameMsEma_ = 0.0;
            text_.clear();
            return;
        }

        const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        if (guiCanvas == IREntity::kNullEntity) {
            return;
        }
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();

        updateSmoothedValues();
        if (frameCounter_ % kPerfHudRefreshFrames == 0 || text_.empty()) {
            buildText();
        }
        ++frameCounter_;

        // One parity alignment for the whole block: the glyph batch aligns
        // each segment's origin itself, and every segment offset is an even
        // number of trixels, so the graph and the text share one shift.
        const IRMath::ivec2 origin =
            IRRender::parityAlignedPosition(contentOrigin(), canvas_->size_);
        paintPanel(origin);
        paintGraph(origin + IRMath::ivec2(0, textBlockHeight() + kPerfHudGraphGap));
        queueText(origin);
        lastGlyphCommandCount_ = static_cast<int>(textCmds_.size());
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        // Registration-time singleton touches, for the main-thread reason
        // `widget_theme.hpp` documents; touching the state singleton here is
        // also what puts the row in the archetype this system iterates.
        IRPrefab::PerfHud::ensureStateSingleton();
        IRPrefab::Widget::ensureThemeSingleton();
        return registerSystem<
            PERF_HUD,
            IRComponents::C_PerfHudState,
            AlsoWrites<IRComponents::C_TriangleCanvasTextures>>("PerfHud");
    }

    static int textBlockHeight() {
        return IRRender::textBlockHeight(kPerfHudTextLines, kPerfHudFontSize);
    }

    static IRMath::ivec2 contentSize() {
        return IRMath::ivec2(
            kPerfHudGraphWidth,
            textBlockHeight() + kPerfHudGraphGap + kPerfHudGraphHeight
        );
    }

    // The calculations below read only the history ring and the smoothed
    // values, never the clock or the canvas, so they are public for the unit
    // tests that pin them (`test/render/perf_hud_test.cpp`).

    static IRMath::Color healthColor(float fractionOfTarget) {
        if (fractionOfTarget >= kPerfHudGoodFraction) {
            return kPerfHudGood;
        }
        if (fractionOfTarget >= kPerfHudWarnFraction) {
            return kPerfHudWarn;
        }
        return kPerfHudBad;
    }

    static IRMath::Color dropColor(int drops) {
        if (drops == 0) {
            return kPerfHudGood;
        }
        if (drops <= kPerfHudWarnDrops) {
            return kPerfHudWarn;
        }
        return kPerfHudBad;
    }

    void recordFrame(float frameMs) {
        historyMs_[historyHead_] = frameMs;
        historyHead_ = (historyHead_ + 1) % kPerfHudHistoryFrames;
        if (historyCount_ < kPerfHudHistoryFrames) {
            ++historyCount_;
        }
    }

    // Frame time `agoFrames` frames back; 0 is the frame being drawn.
    float sampleMs(int agoFrames) const {
        return historyMs_
            [(historyHead_ - 1 - agoFrames + kPerfHudHistoryFrames) % kPerfHudHistoryFrames];
    }

    // Column layout the two format strings produce; `queueText` colours the
    // value runs by these character offsets, so the two stay together:
    //
    //   0123456789012345
    //   FPS  60  MIN  58
    //   MS 16.7  DROP  0
    void buildText() {
        const float budgetMs = IRRender::kFrameTimeBudgetMs;
        const float targetFps = 1000.0f / budgetMs;
        const float dropMs = budgetMs * static_cast<float>(kPerfHudGraphBudgets);

        float maxMs = 0.0f;
        int drops = 0;
        for (int i = 0; i < historyCount_; ++i) {
            const float ms = sampleMs(i);
            maxMs = IRMath::max(maxMs, ms);
            if (ms > dropMs) {
                ++drops;
            }
        }
        const float minFps = maxMs > 0.0f ? 1000.0f / maxMs : 0.0f;
        const float fps = static_cast<float>(fpsEma_);
        const float frameMs = static_cast<float>(frameMsEma_);

        fpsColor_ = healthColor(fps / targetFps);
        minFpsColor_ = healthColor(minFps / targetFps);
        frameMsColor_ = healthColor(frameMs > 0.0f ? budgetMs / frameMs : 1.0f);
        dropsColor_ = dropColor(drops);

        // The trixel font is uppercase-only, so labels are spelled uppercase.
        // Width specifiers pin every value to a fixed column; the clamps keep
        // an absurd reading from overflowing its column.
        char line[kPerfHudLineChars + 1];
        std::snprintf(
            line,
            sizeof(line),
            "FPS%4.0f  MIN%4.0f",
            IRMath::min(fps, 9999.0f),
            IRMath::min(minFps, 9999.0f)
        );
        text_.assign(line);
        text_ += '\n';
        std::snprintf(
            line,
            sizeof(line),
            "MS%5.1f  DROP%3d",
            IRMath::min(frameMs, 999.9f),
            IRMath::min(drops, 999)
        );
        text_ += line;
    }

    // One bar per frame, newest at the right edge, height proportional to
    // frame time with the budget line at one budget, into `graphPixels_`
    // (row 0 is the top).
    void buildGraph() {
        const float budgetMs = IRRender::kFrameTimeBudgetMs;
        const float fullBarMs = budgetMs * static_cast<float>(kPerfHudGraphBudgets);

        graphPixels_.fill(kPerfHudGraphBackground);
        const int budgetRow = kPerfHudGraphHeight - 1 - kPerfHudGraphHeight / kPerfHudGraphBudgets;
        for (int x = 0; x < kPerfHudGraphWidth; ++x) {
            graphPixels_[budgetRow * kPerfHudGraphWidth + x] = theme_.borderIdle_;
        }
        for (int i = 0; i < historyCount_; ++i) {
            const float ms = sampleMs(i);
            if (ms <= 0.0f) {
                continue;
            }
            const int x = kPerfHudGraphWidth - 1 - i;
            const int barHeight = IRMath::clamp(
                static_cast<int>(IRMath::ceil(ms / fullBarMs * kPerfHudGraphHeight)),
                1,
                kPerfHudGraphHeight
            );
            const IRMath::Color color = healthColor(budgetMs / ms);
            for (int y = kPerfHudGraphHeight - barHeight; y < kPerfHudGraphHeight; ++y) {
                graphPixels_[y * kPerfHudGraphWidth + x] = color;
            }
        }
    }

  private:
    void updateSmoothedValues() {
        fpsEma_ = IRMath::seededEma(fpsEma_, IRTime::renderFps(), kPerfHudEmaAlpha);
        frameMsEma_ = IRMath::seededEma(frameMsEma_, sampleMs(0), kPerfHudEmaAlpha);
    }

    IRMath::ivec2 contentOrigin() const {
        using IRComponents::PerfHudCorner;
        const IRMath::ivec2 size = contentSize();
        const IRMath::ivec2 canvas = canvas_->size_;
        const bool right =
            corner_ == PerfHudCorner::TOP_RIGHT || corner_ == PerfHudCorner::BOTTOM_RIGHT;
        const bool bottom =
            corner_ == PerfHudCorner::BOTTOM_LEFT || corner_ == PerfHudCorner::BOTTOM_RIGHT;
        return IRMath::ivec2(
            right ? canvas.x - kPerfHudPadding.x - size.x : kPerfHudPadding.x,
            bottom ? canvas.y - kPerfHudPadding.y - size.y : kPerfHudPadding.y
        );
    }

    void paintPanel(IRMath::ivec2 origin) {
        panelPos_ = origin - kPerfHudBgPadding;
        panelSize_ = contentSize() + 2 * kPerfHudBgPadding;
        IRRender::fillRect(
            *canvas_,
            panelPos_,
            panelSize_,
            theme_.panelBackground_,
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );
        IRRender::drawBorder(
            *canvas_,
            panelPos_,
            panelSize_,
            theme_.borderIdle_,
            IRRender::kWidgetBorderDistance,
            theme_.borderThickness_,
            scratch_
        );
    }

    // The graph is one pixel block uploaded with a single `blitRect`, rather
    // than one `fillRect` per bar: a bar is a few texels, and the per-call
    // upload would dominate.
    void paintGraph(IRMath::ivec2 pos) {
        buildGraph();
        IRRender::blitRect(
            *canvas_,
            pos,
            IRMath::ivec2(kPerfHudGraphWidth, kPerfHudGraphHeight),
            graphPixels_.data(),
            kPerfHudGraphWidth,
            IRRender::kWidgetLabelDistance,
            scratch_
        );
    }

    void queueSegment(
        IRMath::ivec2 origin,
        std::string_view line,
        int row,
        int first,
        int count,
        IRMath::Color color
    ) {
        IRPrefab::GuiText::queueGuiText(
            textCmds_,
            line.substr(first, count),
            origin + IRMath::ivec2(
                         first * IRRender::kGlyphStepX * kPerfHudFontSize,
                         row * IRRender::kGlyphStepY * kPerfHudFontSize
                     ),
            canvas_->size_,
            color,
            kPerfHudFontSize
        );
    }

    // Labels in the theme's text colour, each value run in its health colour.
    void queueText(IRMath::ivec2 origin) {
        const std::string_view text = text_;
        const std::string_view line1 = text.substr(0, kPerfHudLineChars);
        const std::string_view line2 = text.substr(kPerfHudLineChars + 1);
        queueSegment(origin, line1, 0, 0, 3, theme_.textIdle_);
        queueSegment(origin, line1, 0, 3, 4, fpsColor_);
        queueSegment(origin, line1, 0, 9, 3, theme_.textIdle_);
        queueSegment(origin, line1, 0, 12, 4, minFpsColor_);
        queueSegment(origin, line2, 1, 0, 2, theme_.textIdle_);
        queueSegment(origin, line2, 1, 2, 5, frameMsColor_);
        queueSegment(origin, line2, 1, 9, 4, theme_.textIdle_);
        queueSegment(origin, line2, 1, 13, 3, dropsColor_);
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERF_HUD_H */
