#ifndef SYSTEM_SPRITES_TO_SCREEN_H
#define SYSTEM_SPRITES_TO_SCREEN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_sprite.hpp>
#include <irreden/render/texture.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

using namespace IRMath;

namespace IRSystem {

/// Screen-composite sprite pass. Iterates entities with C_Sprite +
/// C_PositionGlobal3D, computes an iso-projected screen position and
/// depth for each (APPLY_POSITION_OFFSET has already folded any
/// per-frame offset into C_PositionGlobal3D), sorts back-to-front
/// grouped by atlas texture, and issues one drawArraysInstanced call
/// per atlas. Sprites bypass the trixel pipeline entirely — alpha-
/// blended quads land directly on the default framebuffer at the
/// FRAMEBUFFER_TO_SCREEN pipeline stage, after the trixel composite
/// finishes.
///
/// CPU-side ordering: entries sort first by texture handle (sprites
/// sharing one atlas pack into a single drawArraysInstanced call) then
/// by iso depth descending (back-to-front so nearer sprites land on top
/// within an atlas). Strict global back-to-front across atlases is
/// deferred to a future multi-atlas / bindless pass; v1 documents that
/// authors who need pixel-perfect z-ordering across sheets should keep
/// co-depth sprites on the same atlas.
template <> struct System<SPRITE_TO_SCREEN> {
    /// SSBO capacity in sprite instances. Set high enough that v1 demos
    /// never trip the cap; sprites past the cap are silently dropped with
    /// a one-shot warning. Bumping this is a no-op for runtimes with
    /// fewer sprites — the SSBO is sized once at system create.
    static constexpr std::size_t kMaxSprites = 4096;

    struct Group {
        IRRender::ResourceId textureHandle_;
        std::size_t firstEntry_;
        std::size_t count_;
    };

    IRRender::Buffer *frameDataBuf_ = nullptr;
    IRRender::Buffer *instancesBuf_ = nullptr;
    IRRender::ShaderProgram *program_ = nullptr;
    IRRender::VAO *quadVao_ = nullptr;
    bool overflowWarned_ = false;
    IRRender::FrameDataSpritesToScreen frameData_{};
    std::vector<IRRender::SpriteRenderEntry> entries_;
    std::vector<IRRender::GpuSpriteInstance> gpuScratch_;
    std::vector<Group> groups_;

    void
    tick(const IRComponents::C_Sprite &sprite, const IRComponents::C_PositionGlobal3D &global) {
        if (sprite.textureHandle_ == 0) {
            return;
        }
        if (entries_.size() >= kMaxSprites) {
            if (!overflowWarned_) {
                IR_LOG_WARN(
                    "SpritesToScreen: sprite count exceeded kMaxSprites={} — extras "
                    "dropped this frame",
                    kMaxSprites
                );
                overflowWarned_ = true;
            }
            return;
        }
        const vec3 worldPos = global.pos_;
        IRRender::SpriteRenderEntry entry{};
        entry.textureHandle_ = sprite.textureHandle_;
        entry.isoDepth_ = static_cast<int>(pos3DtoDistance(worldPos));
        entry.size_ = sprite.size_;
        entry.uvRect_ = sprite.uvRect_;
        entry.tintRgba_ = vec4(
            static_cast<float>(sprite.tint_.red_) / 255.0f,
            static_cast<float>(sprite.tint_.green_) / 255.0f,
            static_cast<float>(sprite.tint_.blue_) / 255.0f,
            static_cast<float>(sprite.tint_.alpha_) / 255.0f
        );
        entry.screenPos_ = computeScreenAnchor(worldPos) - sprite.anchor_ * sprite.size_;
        entries_.push_back(entry);
    }

    void beginTick() {
        entries_.clear();
        groups_.clear();
    }

    void endTick() {
        if (entries_.empty()) {
            return;
        }
        std::sort(entries_.begin(), entries_.end(), entryLess);
        groups_ = buildGroups(entries_);
        uploadFrameData();
        bindPipeline();
        for (const Group &g : groups_) {
            auto *atlas = IRRender::getResource<IRRender::Texture2D>(g.textureHandle_);
            if (atlas == nullptr) {
                continue;
            }
            packGroupToScratch(g);
            instancesBuf_->subData(
                0,
                gpuScratch_.size() * sizeof(IRRender::GpuSpriteInstance),
                gpuScratch_.data()
            );
            atlas->bind(0);
            IRRender::device()->drawArraysInstanced(
                IRRender::DrawMode::TRIANGLES,
                0,
                6,
                static_cast<int>(g.count_)
            );
        }
        unbindPipeline();
    }

    static SystemId create() {
        IRRender::createNamedResource<IRRender::ShaderProgram>(
            "SpritesToScreenProgram",
            std::vector{
                IRRender::ShaderStage{
                    IRRender::kFileVertSpritesToScreen,
                    IRRender::ShaderType::VERTEX
                },
                IRRender::ShaderStage{
                    IRRender::kFileFragSpritesToScreen,
                    IRRender::ShaderType::FRAGMENT
                }
            }
        );
        IRRender::createNamedResource<IRRender::Buffer>(
            "SpritesToScreenFrameData",
            nullptr,
            sizeof(IRRender::FrameDataSpritesToScreen),
            IRRender::BUFFER_STORAGE_DYNAMIC,
            IRRender::BufferTarget::UNIFORM,
            IRRender::kBufferIndex_SpritesFrameData
        );
        IRRender::createNamedResource<IRRender::Buffer>(
            "SpritesToScreenInstances",
            nullptr,
            kMaxSprites * sizeof(IRRender::GpuSpriteInstance),
            IRRender::BUFFER_STORAGE_DYNAMIC,
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_SpritesInstances
        );

        SystemId id = registerSystem<
            SPRITE_TO_SCREEN,
            IRComponents::C_Sprite,
            IRComponents::C_PositionGlobal3D>("SpritesToScreen");
        auto *sys = getSystemParams<System<SPRITE_TO_SCREEN>>(id);
        sys->frameDataBuf_ =
            IRRender::getNamedResource<IRRender::Buffer>("SpritesToScreenFrameData");
        sys->instancesBuf_ =
            IRRender::getNamedResource<IRRender::Buffer>("SpritesToScreenInstances");
        sys->program_ =
            IRRender::getNamedResource<IRRender::ShaderProgram>("SpritesToScreenProgram");
        sys->quadVao_ = IRRender::getNamedResource<IRRender::VAO>("QuadVAOArrays");
        sys->entries_.reserve(256);
        sys->gpuScratch_.reserve(256);
        return id;
    }

    /// Stable build-order rule used by the sort: equal-handle sprites
    /// stack from far to near; cross-handle ordering is by handle so the
    /// resulting per-handle runs are contiguous in the final entry list.
    static bool
    entryLess(const IRRender::SpriteRenderEntry &a, const IRRender::SpriteRenderEntry &b) {
        if (a.textureHandle_ != b.textureHandle_) {
            return a.textureHandle_ < b.textureHandle_;
        }
        return a.isoDepth_ > b.isoDepth_;
    }

    /// Walk a sorted entry vector and produce one Group per contiguous
    /// run of equal texture handles. Exposed for unit testing — the
    /// production path lives inside `endTick`.
    static std::vector<Group>
    buildGroups(const std::vector<IRRender::SpriteRenderEntry> &sortedEntries) {
        std::vector<Group> out;
        std::size_t runStart = 0;
        while (runStart < sortedEntries.size()) {
            std::size_t runEnd = runStart + 1;
            const IRRender::ResourceId tex = sortedEntries[runStart].textureHandle_;
            while (runEnd < sortedEntries.size() && sortedEntries[runEnd].textureHandle_ == tex) {
                ++runEnd;
            }
            out.push_back(Group{tex, runStart, runEnd - runStart});
            runStart = runEnd;
        }
        return out;
    }

  private:
    /// Anchor-point screen position before subtracting `anchor_ * size_`.
    /// Sprites share the same iso → screen transform as the trixel
    /// composite: iso delta from the camera, scaled by the per-trixel
    /// step size, with the same X-flip that `pos3DtoPos2DScreen` encodes
    /// for world-space points.
    static vec2 computeScreenAnchor(vec3 worldPos) {
        const vec2 viewport = vec2(IRRender::getViewport());
        const vec2 cameraIso = IRRender::getCameraPosition2DIso();
        const vec2 stepSize = IRRender::getTriangleStepSizeScreen();
        const vec2 isoDelta = pos3DtoPos2DIso(worldPos) - cameraIso;
        const vec2 screenSign = vec2(-1.0f, IRPlatform::kGfx.screenYDirection_);
        return viewport * 0.5f + isoDelta * stepSize * screenSign;
    }

    void packGroupToScratch(const Group &g) {
        gpuScratch_.resize(g.count_);
        for (std::size_t i = 0; i < g.count_; ++i) {
            const IRRender::SpriteRenderEntry &e = entries_[g.firstEntry_ + i];
            gpuScratch_[i].screenPosSize_ =
                vec4(e.screenPos_.x, e.screenPos_.y, e.size_.x, e.size_.y);
            gpuScratch_[i].uvRect_ = e.uvRect_;
            gpuScratch_[i].tintRgba_ = e.tintRgba_;
        }
    }

    void uploadFrameData() {
        const ivec2 viewport = IRRender::getViewport();
        frameData_.projection_ = IRMath::ortho(
            0.0f,
            static_cast<float>(viewport.x),
            0.0f,
            static_cast<float>(viewport.y),
            -1.0f,
            100.0f
        );
        frameDataBuf_->subData(0, sizeof(IRRender::FrameDataSpritesToScreen), &frameData_);
    }

    void bindPipeline() {
        // Defensive rebind: other compute passes (e.g. RENDER_STATELESS_PARTICLES_TO_TRIXEL)
        // rebind slot 0 and slot 25 to their own resources earlier in the frame. OpenGL has
        // a single global binding state, so we must re-assert our own before drawing.
        frameDataBuf_->bindBase(
            IRRender::BufferTarget::UNIFORM,
            IRRender::kBufferIndex_SpritesFrameData
        );
        instancesBuf_->bindBase(
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_SpritesInstances
        );
        program_->use();
        quadVao_->bind();
        IRRender::device()->setDepthTest(false);
        IRRender::device()->setDepthWrite(false);
        IRRender::device()->enableBlending();
    }

    static void unbindPipeline() {
        IRRender::device()->disableBlending();
        IRRender::device()->setDepthTest(true);
        IRRender::device()->setDepthWrite(true);
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SPRITES_TO_SCREEN_H */
