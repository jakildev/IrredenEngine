#ifndef SYSTEM_PERF_STATS_OVERLAY_H
#define SYSTEM_PERF_STATS_OVERLAY_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/profile/scope_timer.hpp>

#include <cstdio>
#include <string>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

constexpr ivec2 kPerfStatsPadding = ivec2(24, 24);

template <> struct System<PERF_STATS_OVERLAY> {
    static SystemId create() {
        return createSystem<C_Name>(
            "PerfStatsOverlay",
            [](const C_Name &) {},
            []() {
                static EntityId textEntity = 0;

                EntityId guiCanvas = IRRender::getCanvas("gui");
                auto &canvasTextures = IREntity::getComponent<C_TriangleCanvasTextures>(guiCanvas);
                int canvasWidth = canvasTextures.size_.x;

                if (textEntity == 0) {
                    textEntity = IREntity::createEntity(
                        C_TextSegment{""},
                        C_GuiPosition{kPerfStatsPadding},
                        C_GuiElement{},
                        C_TextStyle{
                            IRColors::kWhite,
                            0,
                            TextAlignH::RIGHT,
                            TextAlignV::TOP,
                            canvasWidth - 2 * kPerfStatsPadding.x,
                            0,
                            1
                        }
                    );
                }

                auto &style = IREntity::getComponent<C_TextStyle>(textEntity);
                style.boxWidth_ = canvasWidth - 2 * kPerfStatsPadding.x;

                auto &timing = IRRender::gpuStageTiming();
                const auto &cpuHist = IRProfile::cpuFrameHistogram();

                // Top-line CPU buckets from World::input/update/render —
                // only rendered when the CPU histogram is enabled. Falls back
                // to 0.0 if the name has never been recorded.
                const double cpuInputMs = cpuHist.lastFrameMs("input");
                const double cpuUpdateMs = cpuHist.lastFrameMs("update");
                const double cpuRenderMs = cpuHist.lastFrameMs("render");

                std::string buf(1024, '\0');
                if (timing.enabled_ && cpuHist.enabled_) {
                    std::snprintf(
                        buf.data(),
                        buf.size(),
                        "FPS: %.0f  %.1fms\nUPD: %.0f\nENT: %u\nDROP: %u\n"
                        "CPU I:%.2f U:%.2f R:%.2f\n"
                        "GPU CLR:%.1f CMP:%.1f V1:%.1f V2:%.1f\n"
                        "CPU CMP:%.2f V1:%.2f V2:%.2f\n"
                        "GPU SC:%.1f S0:%.1f S1:%.1f OCC:%.1f AO:%.1f\n"
                        "CPU S1:%.2f OCC:%.2f AO:%.2f\n"
                        "GPU BSM:%.1f SH:%.1f LV:%.1f LIT:%.1f FOG:%.1f\n"
                        "CPU LV:%.2f LIT:%.2f\n"
                        "GPU T2T:%.1f T2F:%.1f EC:%.1f SSR:%.1f FB:%.1f\n"
                        "VIS:%u Z:%u",
                        IRTime::renderFps(),
                        IRTime::renderFrameTimeMs(),
                        IRTime::updateFps(),
                        static_cast<unsigned int>(IREntity::getLiveEntityCount()),
                        IRTime::droppedFrames(),
                        cpuInputMs,
                        cpuUpdateMs,
                        cpuRenderMs,
                        timing.canvasClearMs_,
                        timing.voxelCompactMs_,
                        timing.voxelStage1Ms_,
                        timing.voxelStage2Ms_,
                        cpuHist.lastFrameMs("voxelCompact"),
                        cpuHist.lastFrameMs("voxelStage1"),
                        cpuHist.lastFrameMs("voxelStage2"),
                        timing.shapeCompactMs_,
                        timing.shapePass0Ms_,
                        timing.shapePass1Ms_,
                        timing.buildLightOcclusionGridMs_,
                        timing.computeVoxelAoMs_,
                        cpuHist.lastFrameMs("shapePass1"),
                        cpuHist.lastFrameMs("buildLightOcclusionGrid"),
                        cpuHist.lastFrameMs("computeVoxelAO"),
                        timing.bakeSunShadowMapMs_,
                        timing.computeSunShadowMs_,
                        timing.computeLightVolumeMs_,
                        timing.lightingToTrixelMs_,
                        timing.fogToTrixelMs_,
                        cpuHist.lastFrameMs("computeLightVolume"),
                        cpuHist.lastFrameMs("lightingToTrixel"),
                        timing.trixelToTrixelMs_,
                        timing.trixelToFbMs_,
                        timing.entityCanvasToFbMs_,
                        timing.screenSpaceResidualRotateMs_,
                        timing.fbToScreenMs_,
                        timing.visibleShapeCount_,
                        timing.shapeGroupsZ_
                    );
                } else if (timing.enabled_) {
                    std::snprintf(
                        buf.data(),
                        buf.size(),
                        "FPS: %.0f  %.1fms\nUPD: %.0f\nENT: %u\nDROP: %u\n"
                        "CLR:%.1f CMP:%.1f V1:%.1f V2:%.1f\n"
                        "SC:%.1f S0:%.1f S1:%.1f OCC:%.1f AO:%.1f\n"
                        "BSM:%.1f SH:%.1f LV:%.1f LIT:%.1f FOG:%.1f\n"
                        "T2T:%.1f T2F:%.1f EC:%.1f SSR:%.1f FB:%.1f\n"
                        "VIS:%u Z:%u",
                        IRTime::renderFps(),
                        IRTime::renderFrameTimeMs(),
                        IRTime::updateFps(),
                        static_cast<unsigned int>(IREntity::getLiveEntityCount()),
                        IRTime::droppedFrames(),
                        timing.canvasClearMs_,
                        timing.voxelCompactMs_,
                        timing.voxelStage1Ms_,
                        timing.voxelStage2Ms_,
                        timing.shapeCompactMs_,
                        timing.shapePass0Ms_,
                        timing.shapePass1Ms_,
                        timing.buildLightOcclusionGridMs_,
                        timing.computeVoxelAoMs_,
                        timing.bakeSunShadowMapMs_,
                        timing.computeSunShadowMs_,
                        timing.computeLightVolumeMs_,
                        timing.lightingToTrixelMs_,
                        timing.fogToTrixelMs_,
                        timing.trixelToTrixelMs_,
                        timing.trixelToFbMs_,
                        timing.entityCanvasToFbMs_,
                        timing.screenSpaceResidualRotateMs_,
                        timing.fbToScreenMs_,
                        timing.visibleShapeCount_,
                        timing.shapeGroupsZ_
                    );
                } else if (cpuHist.enabled_) {
                    std::snprintf(
                        buf.data(),
                        buf.size(),
                        "FPS: %.0f  %.1fms\nUPD: %.0f\nENT: %u\nDROP: %u\n"
                        "CPU I:%.2f U:%.2f R:%.2f\n"
                        "CPU CMP:%.2f V1:%.2f V2:%.2f\n"
                        "CPU S1:%.2f OCC:%.2f AO:%.2f\n"
                        "CPU LV:%.2f LIT:%.2f",
                        IRTime::renderFps(),
                        IRTime::renderFrameTimeMs(),
                        IRTime::updateFps(),
                        static_cast<unsigned int>(IREntity::getLiveEntityCount()),
                        IRTime::droppedFrames(),
                        cpuInputMs,
                        cpuUpdateMs,
                        cpuRenderMs,
                        cpuHist.lastFrameMs("voxelCompact"),
                        cpuHist.lastFrameMs("voxelStage1"),
                        cpuHist.lastFrameMs("voxelStage2"),
                        cpuHist.lastFrameMs("shapePass1"),
                        cpuHist.lastFrameMs("buildLightOcclusionGrid"),
                        cpuHist.lastFrameMs("computeVoxelAO"),
                        cpuHist.lastFrameMs("computeLightVolume"),
                        cpuHist.lastFrameMs("lightingToTrixel")
                    );
                } else {
                    std::snprintf(
                        buf.data(),
                        buf.size(),
                        "FPS: %.0f  %.1fms\nUPD: %.0f\nENT: %u\nDROP: %u",
                        IRTime::renderFps(),
                        IRTime::renderFrameTimeMs(),
                        IRTime::updateFps(),
                        static_cast<unsigned int>(IREntity::getLiveEntityCount()),
                        IRTime::droppedFrames()
                    );
                }

                IREntity::getComponent<C_TextSegment>(textEntity).text_ = buf.c_str();
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERF_STATS_OVERLAY_H */
