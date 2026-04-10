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
                auto &canvasTextures =
                    IREntity::getComponent<C_TriangleCanvasTextures>(guiCanvas);
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

                std::string buf(512, '\0');
                if (timing.enabled_) {
                    std::snprintf(
                        buf.data(), buf.size(),
                        "FPS: %.0f  %.1fms\nUPD: %.0f\nENT: %u\nDROP: %u\n"
                        "CLR:%.1f CMP:%.1f V1:%.1f V2:%.1f\n"
                        "SC:%.1f S0:%.1f S1:%.1f T2F:%.1f EC:%.1f\n"
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
                        timing.trixelToFbMs_,
                        timing.entityCanvasToFbMs_,
                        timing.visibleShapeCount_,
                        timing.shapeGroupsZ_
                    );
                } else {
                    std::snprintf(
                        buf.data(), buf.size(),
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
