#ifndef SYSTEM_PERF_STATS_OVERLAY_H
#define SYSTEM_PERF_STATS_OVERLAY_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/profile/scope_timer.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_rect.hpp>

#include <array>
#include <cstdio>
#include <string>

namespace IRSystem {

// Outer padding from the canvas top-right corner.
inline constexpr IRMath::ivec2 kPerfStatsPadding{12, 12};

// Inner padding between the text rect and the background panel edge.
inline constexpr IRMath::ivec2 kPerfStatsBgPadding{6, 4};

// Trixel-width budget for the overlay column when the canvas can afford it.
// 22 chars × kGlyphStepX (8) = 176 trixels — sized to fit the formatted
// stage rows ("  STAGE-LABEL-14ch  GGGG.GG"). On a narrower canvas the
// runtime clamps the column to `canvasWidth - 2 * kPerfStatsPadding.x`.
inline constexpr int kPerfStatsColumnTrixels = 176;

// Background panel color. RGBA8, alpha 200/255 (~78%) lets the underlying
// scene tint through enough that the overlay reads as an info layer rather
// than an opaque card.
inline constexpr IRMath::Color kPerfStatsBgColor{0, 0, 0, 200};

// EMA smoothing factor for displayed values. ~250 ms time-constant at 60 Hz
// (95% settle in ~15 frames). Removes the per-frame jitter on FPS / per-stage
// ms without lagging behind a real regression by more than a few hundred ms.
inline constexpr double kPerfStatsEmaAlpha = 0.15;

// Display refresh throttle. The EMA updates every frame; the rendered text
// only rebuilds every N frames so digit columns don't visibly twitch — the
// dominant fix for the "vibrating numbers" complaint.
inline constexpr int kPerfStatsRefreshFrames = 4;

// Sentinel tag placed on the overlay text entity at creation time so the
// system iterates exactly one entity (the text entity) rather than all
// C_Name-bearing entities. beginTick/endTick fire even when zero entities
// match, so the real work in those hooks is unaffected.
struct C_PerfStatsOverlayTag {};

template <> struct System<PERF_STATS_OVERLAY> {
    // Cached GUI canvas pointer + width, refreshed each beginTick to survive
    // a canvas swap. nullptr until the first beginTick.
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    int canvasWidth_ = 0;

    // The text entity we mutate each frame. Created lazily on the first
    // endTick once the GUI canvas exists.
    IREntity::EntityId textEntity_ = IREntity::kNullEntity;

    // Smoothed top-line values.
    double fpsEma_ = 0.0;
    double frameMsEma_ = 0.0;
    double updateFpsEma_ = 0.0;

    // Smoothed CPU phase ms (input / update / render).
    double cpuInputMsEma_ = 0.0;
    double cpuUpdateMsEma_ = 0.0;
    double cpuRenderMsEma_ = 0.0;

    // Per-stage EMA, indexed parallel to IRRender::gpuStageRegistry(). One
    // slot per registry entry — adding a stage to the registry surfaces here
    // automatically with no edit to this system.
    static_assert(
        std::tuple_size_v<std::remove_reference_t<decltype(IRRender::gpuStageRegistry())>> == 20,
        "EMA arrays must be resized to match gpuStageRegistry size"
    );
    std::array<double, 20> gpuStageMsEma_{};
    std::array<double, 20> cpuStageMsEma_{};

    // Refresh throttle counter and the last-built text. Reused on
    // non-refresh frames to keep the displayed digits stable.
    int frameCounter_ = 0;
    std::string lastText_;

    // Scratch buffer for the background rect upload. RectFillScratch keeps
    // its capacity across frames — only the first paint allocates.
    IRRender::RectFillScratch bgScratch_;

    void beginTick() {
        // Implicit enable: PerfStatsOverlay's presence in the pipeline is the
        // user opt-in. The flip runs every frame, so disabling either flag
        // has no effect while PERF_STATS_OVERLAY is in the pipeline; remove
        // the system from the pipeline to disable timing collection.
        IRProfile::cpuFrameHistogram().enabled_ = true;
        IRRender::gpuStageTiming().enabled_ = true;

        const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        canvasWidth_ = canvas_->size_.x;
    }

    void tick(C_PerfStatsOverlayTag &) {}

    void endTick() {
        if (canvas_ == nullptr)
            return;

        updateSmoothedValues();

        // Rebuild the text string only every N frames. EMA still updates
        // every frame (above); the rendered digits stabilize at the refresh
        // cadence (default ~67 ms at 60 Hz).
        if (frameCounter_ % kPerfStatsRefreshFrames == 0 || lastText_.empty()) {
            lastText_ = buildText();
        }
        ++frameCounter_;

        if (textEntity_ == IREntity::kNullEntity) {
            createTextEntity();
        }

        paintBackground();

        // Hand the text + box width to TEXT_TO_TRIXEL (runs immediately
        // after this system in the RENDER pipeline). Glyph pixels overpaint
        // the background; the rest of the panel rect stays. Left-aligned
        // with the runtime-clamped column width: lines anchor at a fixed
        // left edge so columns stay column-aligned regardless of per-line
        // value width.
        IREntity::getComponent<IRComponents::C_TextSegment>(textEntity_).text_ = lastText_;
        auto &style = IREntity::getComponent<IRComponents::C_TextStyle>(textEntity_);
        style.boxWidth_ = columnTrixels();
        // Re-pin the position in case the canvas was resized since entity
        // creation; cheap and the only way to track a runtime canvas swap.
        auto &pos = IREntity::getComponent<IRComponents::C_GuiPosition>(textEntity_);
        pos.pos_ = IRMath::ivec2(leftEdge(), kPerfStatsPadding.y);
    }

    static SystemId create() {
        return registerSystem<PERF_STATS_OVERLAY, C_PerfStatsOverlayTag>("PerfStatsOverlay");
    }

  private:
    static double ema(double prev, double now) {
        // Seed on first sample so the displayed value reaches steady-state in
        // one frame instead of slow-rising from 0.
        return prev <= 0.0 ? now : (kPerfStatsEmaAlpha * now + (1.0 - kPerfStatsEmaAlpha) * prev);
    }

    void updateSmoothedValues() {
        fpsEma_ = ema(fpsEma_, IRTime::renderFps());
        frameMsEma_ = ema(frameMsEma_, IRTime::renderFrameTimeMs());
        updateFpsEma_ = ema(updateFpsEma_, IRTime::updateFps());

        const auto &cpu = IRProfile::cpuFrameHistogram();
        cpuInputMsEma_ = ema(cpuInputMsEma_, cpu.lastFrameMs("input"));
        cpuUpdateMsEma_ = ema(cpuUpdateMsEma_, cpu.lastFrameMs("update"));
        cpuRenderMsEma_ = ema(cpuRenderMsEma_, cpu.lastFrameMs("render"));

        const auto &gpu = IRRender::gpuStageTiming();
        const auto &registry = IRRender::gpuStageRegistry();
        for (std::size_t i = 0; i < registry.size(); ++i) {
            const auto &info = registry[i];
            gpuStageMsEma_[i] = ema(gpuStageMsEma_[i], gpu.*info.field_);
            cpuStageMsEma_[i] = ema(cpuStageMsEma_[i], cpu.lastFrameMs(info.name_));
        }
    }

    // Short display label for a registry stage name. The registry uses
    // camelCase identifiers that are too long for the narrow overlay column
    // (~13 chars at fontSize=1 / 240-trixel canvas). Each branch returns a
    // ≤13-char uppercase label that still reads recognizably. Returns nullptr
    // when no alias is defined — the caller uses %.*s with the raw name then.
    static const char *stageLabel(std::string_view name) {
        if (name == "canvasClear")
            return "CLEAR";
        if (name == "voxelCompact")
            return "VOX-COMPACT";
        if (name == "voxelStage1")
            return "VOX-STAGE1";
        if (name == "voxelStage2")
            return "VOX-STAGE2";
        if (name == "shapeCompact")
            return "SHP-COMPACT";
        if (name == "shapePass0")
            return "SHP-PASS0";
        if (name == "shapePass1")
            return "SHP-PASS1";
        if (name == "textToTrixel")
            return "TEXT";
        if (name == "buildLightOcclusionGrid")
            return "OCCL-GRID";
        if (name == "computeVoxelAO")
            return "VOXEL-AO";
        if (name == "bakeSunShadowMap")
            return "SUNSHADOWMAP";
        if (name == "computeSunShadow")
            return "SUNSHADOW";
        if (name == "computeLightVolume")
            return "LIGHT-VOL";
        if (name == "lightingToTrixel")
            return "LIGHTING";
        if (name == "fogToTrixel")
            return "FOG";
        if (name == "trixelToTrixel")
            return "TRIX-COMP";
        if (name == "trixelToFb")
            return "TRIX-FB";
        if (name == "entityCanvasToFb")
            return "ENT-FB";
        if (name == "resolvePerAxisScreenDepth")
            return "PERAXIS-DEPTH";
        if (name == "fbToScreen")
            return "FB-SCREEN";
        return nullptr;
    }

    std::string buildText() {
        const auto &gpu = IRRender::gpuStageTiming();
        const auto &registry = IRRender::gpuStageRegistry();

        const float budgetMs = IRRender::kFrameTimeBudgetMs;
        const double budgetPct = (frameMsEma_ / budgetMs) * 100.0;

        std::string out;
        out.reserve(1536);

        char line[224];

        // The font is uppercase-only (`getGlyph` in `trixel_font.hpp` folds
        // lowercase to uppercase glyphs). Spell every label uppercase so the
        // intent matches the render. Width specifiers pad every numeric to
        // a fixed column so digit changes don't shift adjacent columns.
        //
        // Target column ≤ 22 chars (176 trixels at fontSize=1) so the panel
        // fits a 240-trixel GUI canvas with kPerfStatsPadding on each side.

        // FRAME — top-line wall-clock + counters.
        std::snprintf(
            line,
            sizeof(line),
            "FRAME\n"
            "  FPS       %4.0f / 60\n"
            "  TIME       %5.1f MS\n"
            "  BUDGET      %4.0f %%\n"
            "  UPDATE     %4.0f HZ\n"
            "  DROPPED    %5u\n"
            "  ENTITIES %8u\n\n",
            fpsEma_,
            frameMsEma_,
            budgetPct,
            updateFpsEma_,
            IRTime::droppedFrames(),
            static_cast<unsigned int>(IREntity::getLiveEntityCount())
        );
        out.append(line);

        // CPU PHASES — World::input/update/render top-level buckets.
        std::snprintf(
            line,
            sizeof(line),
            "CPU PHASES (MS)\n"
            "  INPUT       %6.2f\n"
            "  UPDATE      %6.2f\n"
            "  RENDER      %6.2f\n\n",
            cpuInputMsEma_,
            cpuUpdateMsEma_,
            cpuRenderMsEma_
        );
        out.append(line);

        // GPU STAGES — one row per registry entry. Drives the "implicitly
        // capture all phases" property: the registry is the source of truth;
        // new stages appear here automatically. CPU per-stage data is
        // dropped from the narrow display (kept in the histogram for query)
        // because most stages have no CPU sample (the timing observer
        // samples both, but only a handful of render systems do meaningful
        // CPU work outside the dispatch call itself).
        out.append("GPU STAGES (MS)\n");
        for (std::size_t i = 0; i < registry.size(); ++i) {
            const auto &info = registry[i];
            const char *label = stageLabel(info.name_);
            if (label != nullptr) {
                std::snprintf(line, sizeof(line), "  %-13s %6.2f\n", label, gpuStageMsEma_[i]);
            } else {
                std::snprintf(
                    line,
                    sizeof(line),
                    "  %.*s %6.2f\n",
                    static_cast<int>(info.name_.size()),
                    info.name_.data(),
                    gpuStageMsEma_[i]
                );
            }
            out.append(line);
        }

        // CULL — visible/total counters. Not smoothed; integer counts read
        // fine as raw values, and smoothing would lag the diagnostic.
        //
        // VOX visible is the prior frame's voxel-cull survivor count. In the
        // rotating (per-axis) path it sums the three axis regions, so a voxel
        // exposed on N axes counts N times — a per-axis-work signal, not a 1:1
        // voxel count (see VOXEL_TO_TRIXEL_STAGE_1's cull-diagnostic readback).
        //
        // Settled-vs-transient: these HUD counters and the GPU-stage rows above
        // report the LAST sampled frame, so a single `--auto-screenshot` shot
        // captures the unsettled camera-cut frame (per-axis chunk-bounds rebuild,
        // one-frame occlusion self-disable) and reads low/zero. The settled
        // `--auto-profile` run (its drained running avg/max) is the authoritative
        // perf + cull source; the overlay is a live glance, not a measurement.
        std::snprintf(
            line,
            sizeof(line),
            "\n"
            "CULL\n"
            "  SHAPES V/Z %5u/%u\n"
            "  VOX  %7u/%7u\n"
            "  LIGHTS %5u/%u\n"
            "  CASTERS %5u\n"
            "  FEEDER %5.0f,%5.0f/%5.0f,%5.0f",
            gpu.visibleShapeCount_,
            gpu.shapeGroupsZ_,
            gpu.visibleVoxelCount_,
            gpu.totalVoxelCount_,
            gpu.lightsSeeded_,
            gpu.lightsEligible_,
            gpu.worldPlacedCasterCount_,
            gpu.shadowFeederMin_.x,
            gpu.shadowFeederMin_.y,
            gpu.shadowFeederMax_.x,
            gpu.shadowFeederMax_.y
        );
        out.append(line);

        return out;
    }

    int columnTrixels() const {
        // Clamp to fit narrow canvases — the GUI canvas is
        // `mainCanvasSize / guiScale` (default scale=1), so a 480-trixel
        // main canvas yields a 480-trixel GUI canvas. The overlay's
        // formatted lines truncate cleanly on the right at the canvas edge
        // if the column shrinks below the format's preferred width.
        const int available = canvasWidth_ - 2 * kPerfStatsPadding.x;
        return IRMath::max(8, IRMath::min(available, kPerfStatsColumnTrixels));
    }

    int leftEdge() const {
        return canvasWidth_ - kPerfStatsPadding.x - columnTrixels();
    }

    void createTextEntity() {
        // Anchor the overlay to the right side of the canvas; LEFT alignment
        // within the column keeps every line locked to the same left x so
        // per-line digit widths don't cascade-shift adjacent lines.
        textEntity_ = IREntity::createEntity(
            IRComponents::C_TextSegment{""},
            IRComponents::C_GuiPosition{IRMath::ivec2(leftEdge(), kPerfStatsPadding.y)},
            IRComponents::C_GuiElement{},
            IRComponents::C_TextStyle{
                IRMath::IRColors::kWhite,
                0,
                IRComponents::TextAlignH::LEFT,
                IRComponents::TextAlignV::TOP,
                columnTrixels(),
                0,
                1
            },
            C_PerfStatsOverlayTag{}
        );
    }

    // Left-aligned multi-line text at the top-right of the canvas. The text
    // entity's GuiPosition pins the LEFT edge; `kPerfStatsColumnTrixels`
    // bounds the width budget. The background covers the same rect plus
    // `kPerfStatsBgPadding` on each side. Lines shorter than the column
    // budget still get the full-width panel — looks cleaner than a ragged
    // right edge that follows per-line text width.
    //
    // Coordinates are in canvas (trixel) space. `TEXT_TO_TRIXEL` runs
    // immediately after this system and overpaints glyph pixels into the
    // same space, so the rect appears behind the text with no z-ordering
    // ambiguity.
    void paintBackground() {
        if (canvas_ == nullptr || lastText_.empty())
            return;

        int lineCount = 1;
        for (char c : lastText_) {
            if (c == '\n')
                ++lineCount;
        }

        const int textH = lineCount * IRRender::kGlyphStepY;
        const int colW = columnTrixels();
        const int textLeft = leftEdge();
        const int topEdge = kPerfStatsPadding.y;

        const IRMath::ivec2 bgPos{
            textLeft - kPerfStatsBgPadding.x,
            topEdge - kPerfStatsBgPadding.y
        };
        const IRMath::ivec2 bgSize{
            colW + 2 * kPerfStatsBgPadding.x,
            textH + 2 * kPerfStatsBgPadding.y
        };

        IRRender::fillRect(
            *canvas_,
            bgPos,
            bgSize,
            kPerfStatsBgColor,
            IRRender::kWidgetBackgroundDistance,
            bgScratch_
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERF_STATS_OVERLAY_H */
