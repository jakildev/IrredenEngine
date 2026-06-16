#ifndef SYSTEM_TEXT_TO_TRIXEL_H
#define SYSTEM_TEXT_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_command.hpp>

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/gui_text_batch.hpp>
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

// Glyph layout + the batched compute-dispatch helpers now live in
// irreden/render/gui_text_batch.hpp (IRPrefab::GuiText) so the widget render
// systems share the same path. This system owns the GPU resources
// (TextToTrixelProgram / FontDataBuffer / GlyphDrawCommandBuffer) and renders
// free-text entities + the GUI command-list overlay; dispatchGuiText reuses
// those resources for widget text, which is why TEXT_TO_TRIXEL must run first.
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
        IRPrefab::GuiText::expandTextToCommands(
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
            auto fontData = IRPrefab::GuiText::buildFontBuffer();
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
            IRPrefab::GuiText::expandTextToCommands(
                drawCommands_,
                commandList_,
                IRPrefab::GuiText::kGuiOverlayPadding,
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
        if (count > IRPrefab::GuiText::kMaxGlyphCommands) {
            count = IRPrefab::GuiText::kMaxGlyphCommands;
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
            IRPrefab::GuiText::kFontBufferSize * sizeof(uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_FontData
        );
        IRRender::createNamedResource<Buffer>(
            "GlyphDrawCommandBuffer",
            nullptr,
            IRPrefab::GuiText::kMaxGlyphCommands * sizeof(GlyphDrawCommand),
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
