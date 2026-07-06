#ifndef SYSTEM_TRIXEL_TO_FRAMEBUFFER_H
#define SYSTEM_TRIXEL_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

// ADD ABLILITY TO TEXTURE OVER FACES!

namespace IRSystem {

template <> struct System<TRIXEL_TO_FRAMEBUFFER> {
    Buffer *frameDataBuf_ = nullptr;
    Buffer *hoveredIdBuf_ = nullptr;
    ShaderProgram *program_ = nullptr;
    // Smooth camera Z-yaw forward-scatter composite (T3 / #1310). Replaces the
    // single-canvas gather draw on the main canvas while rotating; see
    // drawPerAxisScatter.
    ShaderProgram *scatterProgram_ = nullptr;
    VAO *quadVao_ = nullptr;

    // Smooth camera Z-yaw (T3 / #1310). Re-resolved every frame in beginTick,
    // never held across frames (.claude/rules/cpp-ecs.md). Non-null only on the
    // main world canvas AND only while the per-axis trixel canvases are
    // allocated (camera at a non-cardinal residual yaw). When set, the main
    // canvas's single trixel→framebuffer draw is replaced by a three-pass depth
    // composite of the X/Y/Z per-axis canvases (see drawPerAxisScatter). At a
    // cardinal these are released, this is null, and the byte-identical
    // single-canvas fast path runs.
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    const C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    // Per-axis empty-cell compaction (#1961 / #2256) is run in
    // VOXEL_TO_TRIXEL_STAGE_1 (right after the per-axis stores) into the
    // component-owned cell buffers, so both the per-axis compute stages and this
    // system's scatter draw over only occupied cells. This system just consumes
    // those buffers off C_PerAxisTrixelCanvases in drawPerAxisScatter.
    //
    // Voxel-compaction buffers (VOXEL_TO_TRIXEL_STAGE_1's named resources) that
    // own slots 25/26. STAGE_1's single-canvas compact relies on those slots
    // staying bound to these across frames; since the scatter transiently rebinds
    // 25/26 (the cell list), it must restore them afterward — the same discipline
    // STAGE_1's own per-axis path follows. Looked up lazily; null only if no voxel
    // system is registered, in which case no per-axis path runs either, so the
    // restore never fires.
    Buffer *voxelCompactedBuf_ = nullptr;
    Buffer *voxelIndirectBuf_ = nullptr;

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &triangleCanvasTextures,
        const C_Name &
    ) {
        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        auto &frameData = IREntity::getComponent<C_FrameDataTrixelToFramebuffer>("mainFramebuffer");
        vec2 framebufferResolution = vec2(framebuffer.getResolutionPlusBuffer());
        const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
        const IRRender::SubdivisionMode renderMode = IRRender::getSubdivisionMode();
        auto renderBehavior = IREntity::getComponentOptional<C_TrixelCanvasRenderBehavior>(entity);
        const C_TrixelCanvasRenderBehavior behavior =
            renderBehavior.has_value() ? (*renderBehavior.value()) : C_TrixelCanvasRenderBehavior{};
        auto zoomLevel = IREntity::getComponentOptional<C_ZoomLevel>(entity);
        const vec2 baseCanvasZoom =
            behavior.useCameraZoom_
                ? IRRender::getCameraZoom()
                : (zoomLevel.has_value() ? (*zoomLevel.value()).zoom_ : vec2(1.0f));

        frameData.frameData_.canvasZoomLevel_ = baseCanvasZoom;
        if (behavior.applyRenderSubdivisions_ && renderMode != IRRender::SubdivisionMode::NONE) {
            frameData.frameData_.canvasZoomLevel_ /= vec2(effectiveSubdivisions);
        }

        frameData.frameData_.cameraTrixelOffset_ =
            behavior.useCameraPositionIso_ ? IRRender::getEffectiveCameraIso() : vec2(0.0f);
        frameData.frameData_.cameraTrixelOffset_ +=
            vec2(behavior.parityOffsetIsoX_, behavior.parityOffsetIsoY_);
        if (behavior.applyRenderSubdivisions_ && renderMode != IRRender::SubdivisionMode::NONE) {
            frameData.frameData_.cameraTrixelOffset_ *= vec2(effectiveSubdivisions);
        }
        frameData.frameData_.textureOffset_ = vec2(0);
        frameData.frameData_.distanceOffset_ = 0;
        // Main world gather is always WORLD content (#1958): the gather clamps it
        // out of the reserved foreground near band (a no-op for in-budget content).
        // Explicit so the persistent mainFramebuffer frame-data (shared with the
        // per-axis scatter path) never carries a stale foreground flag.
        frameData.frameData_.depthPriorityMode_ = 0;
        // No-priority perf fast-path (#2155): forward this canvas's stamp so the
        // finalization shader skips the per-fragment entity-id decode read when no
        // voxel in the canvas carries a per-trixel priority (still read for hovered
        // fragments; byte-identical output either way).
        frameData.frameData_.anyPerTrixelPriority_ = triangleCanvasTextures.anyPerTrixelPriority_;
        frameData.frameData_.mpMatrix_ = calcProjectionMatrix(framebufferResolution) *
                                         calcModelMatrix(
                                             framebufferResolution,
                                             frameData.frameData_.cameraTrixelOffset_,
                                             frameData.frameData_.canvasZoomLevel_
                                         );

        if (!behavior.mouseHoverEnabled_) {
            frameData.frameData_.mouseHoveredTriangleIndex_ = vec2(-1000000.0f);
            frameData.frameData_.effectiveSubdivisionsForHover_ = vec2(1.0f);
            frameData.frameData_.showHoverHighlight_ = 0.0f;
        } else {
            const ivec2 hoverSubdiv = IRRender::mouseTrixelPositionWorld();
            const float subdiv = static_cast<float>(effectiveSubdivisions);
            frameData.frameData_.mouseHoveredTriangleIndex_ = vec2(hoverSubdiv) / vec2(subdiv);
            frameData.frameData_.effectiveSubdivisionsForHover_ = vec2(subdiv, 0.0f);
            frameData.frameData_.showHoverHighlight_ =
                (IRRender::isHoveredTrixelVisible() ? 1.0f : 0.0f);
        }

        // Smooth camera Z-yaw composite (T3 / #1310). On the main world canvas
        // while rotating, replace the single cardinal-snapped gather draw with
        // the forward-scatter composite of the per-axis (X/Y/Z) canvases T2
        // (#1309) populated. Each non-empty canvas cell is scattered as its true
        // deformed face quad into the shared framebuffer depth buffer; the
        // GL_LESS depth test (enabled on framebuffer bind) resolves the nearest
        // face per pixel — that is the composite. The scatter is REPLACE-not-add:
        // the main canvas's cardinal-snapped single-canvas voxels sit at the same
        // world depth as the smooth copies, so drawing both would let depth ties
        // ghost the snapped layer through. Lighting / AO on the resolved
        // composite is T4 (#1311); during rotation the composite shows raw voxel
        // color.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            drawPerAxisScatter(
                frameData,
                *perAxisCanvases_,
                triangleCanvasTextures.size_,
                framebufferResolution
            );
            // Fall through (no early return) to the single-canvas gather below:
            // during rotation the main canvas holds only SDF / text / overlay
            // content (its voxel pass is skipped in VOXEL_TO_TRIXEL_STAGE_1), so
            // the gather composites that content by depth alongside the smooth
            // voxels with no double-draw. SDF stays cardinal-snapped during
            // rotation — splitting SDF into the per-axis canvases is the
            // documented follow-up (design doc §Blast radius). Restore the
            // main-canvas model-projection the scatter overwrote with its
            // per-axis zoomEff matrix.
            frameData.frameData_.mpMatrix_ = calcProjectionMatrix(framebufferResolution) *
                                             calcModelMatrix(
                                                 framebufferResolution,
                                                 frameData.frameData_.cameraTrixelOffset_,
                                                 frameData.frameData_.canvasZoomLevel_
                                             );
        }

        frameData.updateFrameData(frameDataBuf_);

        triangleCanvasTextures.bind(0, 1, 2);
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        IRRender::device()->drawElements(
            DrawMode::TRIANGLES,
            IRShapes2D::kQuadIndicesLength,
            IndexType::UNSIGNED_SHORT
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    // Smooth camera Z-yaw forward-scatter composite (Option 4, T3 / #1310;
    // docs/design/per-axis-trixel-canvas-rotation.md §"Mechanism chosen").
    //
    // T2 (#1309) routes each visible voxel face into its axis canvas and
    // stores ONE cell per face center (atomicMin on the shared world-space
    // `pos3DtoDistance`, so each non-empty cell is the occlusion winner on its
    // view ray). This pass forward-scatters each non-empty cell as its true
    // deformed face quad: instanced over the canvas grid (one instance per
    // cell), the vertex shader recovers the face's world origin from the
    // un-yawed (cardinal) iso pixel `isoPixelToPos3D(cell - perAxisBase,
    // storedDepth>>10)` — an exact integer inverse, non-singular at every yaw
    // (the index is un-yawed) — then projects the four cube-face
    // corners with pos3DtoPos2DIsoYawed (which IS P(θ)·corner, the deform
    // implicit). The framebuffer GL_LESS depth test (enabled on bind) composites
    // the three canvases per pixel. No gather / parity inverse ⇒ the #1256
    // stripe class cannot occur. Cardinal residualYaw==0 keeps the byte-
    // identical single-canvas gather (this path is taken only while rotating).
    //
    // The per-axis textures are sized to the bounded worst case
    // (IRMath::perAxisTrixelCanvasWorstCaseSize → ~(2W, W+H)), larger than the
    // main canvas, so the model-matrix zoom is scaled component-wise by
    // perAxisSize / mainSize — one per-axis texel maps to the same screen
    // footprint (and the canvas-center iso origin to screen center) as a
    // main-canvas texel. The vertex shader uses the same canvas→clip mapping
    // the gather did, so the scatter lands at the same scale/position.
    //
    // v1 cost: instances over all size.x·size.y cells, degenerating empties in
    // the vertex shader. The compaction follow-up (compute pre-pass appending
    // non-empty cell indices + indirect draw args) is scoped in the design doc
    // if the perf gate flags the empty-cell sweep. Picking during rotation
    // (winning entity-id from the composite) is also a follow-up — the gather
    // fast path still resolves it at every cardinal.
    void drawPerAxisScatter(
        C_FrameDataTrixelToFramebuffer &frameData,
        const C_PerAxisTrixelCanvases &axes,
        const ivec2 mainCanvasSize,
        const vec2 framebufferResolution
    ) {
        // #1458 stores the per-axis face-local lattice at BASE (world-unit)
        // resolution — camera zoom is no longer baked into the lattice, so the
        // scatter's screen scale is the FULL camera zoom. canvasZoomLevel_ has
        // effSub divided out (for the subdivided cardinal canvas the gather
        // reads); multiply it back, then rescale component-wise for the larger
        // per-axis texture extent. The pre-#1458 effSub/cappedDensity rescale
        // assumed a density-scaled lattice; against the base-resolution store
        // it cancelled zoom entirely — zooming while rotated shrank the cull
        // viewport (crop) without magnifying content.
        const int effSub = IRMath::max(IRRender::getVoxelRenderEffectiveSubdivisions(), 1);
        const vec2 zoomEff = frameData.frameData_.canvasZoomLevel_ * static_cast<float>(effSub) *
                             vec2(axes.size_) / vec2(mainCanvasSize);

        // Per-axis scatter inputs. `perAxisBase` MUST match
        // VOXEL_TO_TRIXEL_STAGE_1's per-axis store anchor (perAxisFrameOffset:
        // trixelOriginOffsetZ1(axisSize) + floor(cameraIso)) so the vertex
        // shader's world-origin recovery is bit-consistent with where the cells
        // were written. visualYaw + visibleFaceIds mirror buildVoxelFrameData.
        const float visualYaw = IRPrefab::Camera::getYaw();
        const auto cardinalIndex =
            IRMath::rasterYawCardinalIndex(IRPrefab::Camera::computeYawSplit(visualYaw).first);
        const auto visibleFaces = IRMath::visibleFaceTripletCardinal(cardinalIndex);
        // Whole-iso base anchor (#1944). The per-axis canvases are BASE-resolution
        // (#1458), so the camera-pan anchor is the WHOLE-iso camera offset
        // `floor(cameraIso)` — exactly the cardinal path's anchor — NOT scaled by
        // the subdivision density (the density-scaled anchor was vestigial since
        // #1458 made the content base-resolution).
        const vec2 cameraIso = IRRender::getEffectiveCameraIso();
        const vec2 anchorFloor = IRMath::floor(cameraIso);
        frameData.frameData_.perAxisBase_ =
            IRMath::trixelOriginOffsetZ1(axes.size_) + ivec2(anchorFloor);

        // Sub-cell camera pan (#1944 — the jitter fix). The anchor above places
        // content in WHOLE canvas cells; the remaining fraction must move the
        // scatter CONTINUOUSLY at the SAME screen scale as one anchor cell, or the
        // two disagree at every cell boundary and the scene snaps back ~1 cell per
        // crossing — the per-frame pan/rotation jitter (along the world axes; worse
        // at higher zoom). One canvas cell maps to `screenPxPerCell`
        // (fbRes·zoomEff/axisSize) framebuffer px through the model scale below, so
        // the smooth term is screenPxPerCell·fract(cameraIso) — it exactly
        // complements screenPxPerCell·floor(cameraIso) (the anchor), so their sum
        // is screenPxPerCell·cameraIso, continuous. The y term carries the iso→
        // screen sign flip the scatter applies to cornerIso
        // (quadPos.y = 0.5 - cornerIso.y/size.y). This REPLACES calcModelMatrix's
        // whole-game-px offset, which snapped to the canvas's game-px grid
        // (zoomEff·2) — a different scale than one anchor cell — leaving the
        // uncompensated snap. (The cardinal single-canvas path keeps
        // calcModelMatrix: there the canvas IS the native-res blit grid, so its
        // game-px snap + the framebuffer→screen residual are the anti-vibration.)
        const vec2 fractIso = cameraIso - anchorFloor;
        const vec2 screenPxPerCell = framebufferResolution * zoomEff / vec2(axes.size_);
        const vec2 smoothPx = vec2(fractIso.x * screenPxPerCell.x, -fractIso.y * screenPxPerCell.y);
        mat4 perAxisModel = translate(
            mat4(1.0f),
            vec3(
                framebufferResolution.x * 0.5f + smoothPx.x,
                framebufferResolution.y * 0.5f + smoothPx.y,
                0.0f
            )
        );
        perAxisModel = scale(
            perAxisModel,
            vec3(framebufferResolution.x * zoomEff.x, framebufferResolution.y * zoomEff.y, 1.0f)
        );
        frameData.frameData_.mpMatrix_ = calcProjectionMatrix(framebufferResolution) * perAxisModel;
        frameData.frameData_.visualYaw_ = visualYaw;
        frameData.frameData_.visibleFaceIds_ = ivec4(
            static_cast<int>(visibleFaces[0]),
            static_cast<int>(visibleFaces[1]),
            static_cast<int>(visibleFaces[2]),
            0
        );
        frameData.frameData_.distanceOffset_ = 0;
        // Subdivided composite-depth scale (#1884 high-zoom fix). The per-axis
        // store is BASE-resolution (#1458: rawDist>>10 = world units), but the SDF
        // floor + cardinal voxel gather encode depth SUBDIVIDED (worldDepth×effSub).
        // At high zoom the floor's depth out-scaled the base scatter ~effSub× and
        // clipped the voxels into the floor. Carry effSub in effectiveSubdivisions-
        // ForHover_.x so the scatter (v_peraxis_scatter) lifts its iso-depth to the
        // same subdivided magnitude. The store + the #1458 frac bits keep the
        // recovered worldCorner sub-cell-exact, so the scale-up preserves precision.
        // The .y stays 0 → the fall-through gather clamps its depthScale to 1
        // (unchanged main-canvas path); .x only feeds hover, which is gated off here.
        frameData.frameData_.effectiveSubdivisionsForHover_ =
            vec2(static_cast<float>(effSub), 0.0f);
        // Conservative-coverage dilation needs the framebuffer extent the ortho
        // mpMatrix maps into, to convert a pixel margin to NDC (#1494).
        frameData.frameData_.scatterFbResolution_ = vec4(framebufferResolution, 0.0f, 0.0f);
        // Per-pixel depth-color debug (#1697): evaluate hue from interpolated
        // face-corner world depth in the fragment shader instead of pre-baked
        // per-voxel vColor, eliminating the 4/3-band moiré at non-cardinal yaw.
        frameData.frameData_.depthColorMode_ = IRRender::getDepthColorDebugMode() ? 1 : 0;
        frameData.frameData_.depthColorExtent_ = IRRender::getDepthColorDebugExtent();
        // Composite instrumentation (#1457): the scatter shaders false-color by
        // winning axis canvas / recovered origin when the matching overlay mode
        // is active. Depth is untouched, so the visualized winner per pixel is
        // exactly the real composite's winner.
        frameData.frameData_.scatterDebugMode_ = static_cast<int>(IRRender::getDebugOverlay());
        frameData.updateFrameData(frameDataBuf_);

        scatterProgram_->use();
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        // #1961: instance over only the compacted occupied cells (filled by the
        // beginTick compaction pre-pass) via an indirect draw whose instance
        // count is the GPU-written occupied-cell count — instead of the full
        // worst-case grid (axes.size_.x * axes.size_.y, mostly empty). Each axis
        // binds its own compacted-list region + indirect-args struct.
        // #1961 / #2256: the per-axis compaction (now run in VOXEL_TO_TRIXEL_STAGE_1
        // right after the per-axis stores) filled the component-owned cell buffers
        // this frame. Instance over only the occupied cells via the per-axis
        // indirect draw; recompute the region stride from the axis size the same
        // way the compaction sized the buffer.
        Buffer *cellCompacted = axes.cellCompacted_.second;
        Buffer *cellIndirect = axes.cellIndirect_.second;
        const int regionStride = axes.cellRegionStride_;
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
            const C_PerAxisTrixelCanvases::AxisTextures &tex = axes.axes_[axis];
            tex.colors_.second->bind(0);
            tex.distances_.second->bind(1);
            cellCompacted->bindRange(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_PerAxisCellCompacted,
                static_cast<std::ptrdiff_t>(axis) * regionStride *
                    static_cast<int>(sizeof(std::uint32_t)),
                static_cast<size_t>(regionStride) * sizeof(std::uint32_t)
            );
            IRRender::device()->drawElementsInstancedIndirect(
                DrawMode::TRIANGLES,
                IndexType::UNSIGNED_SHORT,
                cellIndirect,
                static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes
            );
        }
        // Restore slots 25/26 to the voxel-compaction buffers (#1961). The cell
        // compaction + the per-axis bindRange above leave 25/26 pointing at the
        // cell buffers; the next frame's VOXEL_TO_TRIXEL_STAGE_1 single-canvas
        // compact relies on those slots still holding its own buffers (it binds
        // them once at create() + sticky thereafter), so a leak here re-reads the
        // cell list as the voxel list and corrupts the world voxels the following
        // frame (the #1961 center-cube regression).
        restoreVoxelCompactionSlots();
        // Restore the gather program for any subsequent canvas's single-canvas
        // tick (background / gui / overlays draw after the main canvas).
        program_->use();
    }

    // Rebind slots 25/26 to VOXEL_TO_TRIXEL_STAGE_1's compaction buffers after the
    // cell compaction borrowed them (#1961). Looked up lazily by name (see the
    // voxel*Buf_ field comment); a no-op if the voxel system isn't registered.
    void restoreVoxelCompactionSlots() {
        IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots(voxelCompactedBuf_, voxelIndirectBuf_);
    }

    void beginTick() {
        HoveredEntityIdLayout resetData;
        hoveredIdBuf_->subData(0, sizeof(resetData), &resetData);

        // Resolve the main canvas's per-axis trixel canvases once per frame for
        // the per-entity tick to consume without a getComponent on its own
        // iterating canvas (#1310). Re-resolved every frame; never held across
        // frames. Null unless the main canvas has the component AND it is
        // currently allocated (camera rotating) — VOXEL_TO_TRIXEL_STAGE_1's
        // beginTick already ran the per-frame allocate/release sync, so the
        // allocation state is current. At a cardinal these are released → the
        // tick takes the byte-identical single-canvas path.
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis =
                IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
            if (perAxis.has_value() && (*perAxis.value()).isAllocated()) {
                perAxisCanvases_ = perAxis.value();
            }
        }

        program_->use();
        quadVao_->bind();
        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        framebuffer.bindFramebuffer();
        framebuffer.clear();
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "CanvasToFramebufferProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertTrixelToFramebuffer, ShaderType::VERTEX},
                ShaderStage{IRRender::kFileFragTrixelToFramebuffer, ShaderType::FRAGMENT}
            }
        );
        // Smooth camera Z-yaw forward-scatter composite (T3 / #1310) — see
        // drawPerAxisScatter. Instanced over the per-axis canvas grid.
        IRRender::createNamedResource<ShaderProgram>(
            "PerAxisScatterProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertPerAxisScatter, ShaderType::VERTEX},
                ShaderStage{IRRender::kFileFragPerAxisScatter, ShaderType::FRAGMENT}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "TrixelToFramebufferFrameData",
            nullptr,
            sizeof(FrameDataTrixelToFramebuffer),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataUniformIsoTriangles
        );
        HoveredEntityIdLayout initData;
        IRRender::createNamedResource<Buffer>(
            "HoveredEntityIdBuffer",
            &initData,
            sizeof(initData),
            BUFFER_STORAGE_DYNAMIC | BUFFER_STORAGE_MAP_READ | BUFFER_STORAGE_MAP_PERSISTENT |
                BUFFER_STORAGE_MAP_COHERENT,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_HoveredEntityId
        );

        // Exclude detached per-entity canvases: those are composited at the
        // owning entity's iso position by ENTITY_CANVAS_TO_FRAMEBUFFER. Without
        // the exclude this full-screen pass would also blit each detached
        // canvas across the whole framebuffer (a second camera-scaled copy).
        SystemId id = registerSystem<
            TRIXEL_TO_FRAMEBUFFER,
            C_TriangleCanvasTextures,
            C_Name,
            Exclude<C_DetachedCanvas>>("CanvasToFramebuffer");
        auto *sys = getSystemParams<System<TRIXEL_TO_FRAMEBUFFER>>(id);
        sys->frameDataBuf_ = IRRender::getNamedResource<Buffer>("TrixelToFramebufferFrameData");
        sys->hoveredIdBuf_ = IRRender::getNamedResource<Buffer>("HoveredEntityIdBuffer");
        sys->program_ = IRRender::getNamedResource<ShaderProgram>("CanvasToFramebufferProgram");
        sys->scatterProgram_ = IRRender::getNamedResource<ShaderProgram>("PerAxisScatterProgram");
        sys->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAO");
        IRRender::tagGpuStage(id, "trixelToFb");
        return id;
    }

    static mat4 calcProjectionMatrix(const vec2 &resolution) {
        mat4 projection = ortho(0.0f, resolution.x, 0.0f, resolution.y, -1.0f, 100.0f);
        return projection;
    }

    static mat4
    calcModelMatrix(const vec2 &resolution, const vec2 &cameraPositionIso, const vec2 &zoomLevel) {
        // Game-pixel half of the anti-vibration decomposition (see
        // `IRMath::cameraSubPixelOffsets`). `FRAMEBUFFER_TO_SCREEN` consumes
        // the matching `screenPxResidual_` from the same helper to keep the
        // two stages byte-for-byte consistent at game-pixel boundaries.
        const IRMath::CameraSubPixelOffsets sub =
            IRMath::cameraSubPixelOffsets(cameraPositionIso, zoomLevel, ivec2(1));
        const vec2 isoPixelOffset = vec2(sub.framebufferGamePxOffset_);
        mat4 model = mat4(1.0f);
        model = translate(
            model,
            vec3(resolution.x / 2 + isoPixelOffset.x, resolution.y / 2 + isoPixelOffset.y, 0.0f)
        );
        model = scale(model, vec3(resolution.x * zoomLevel.x, resolution.y * zoomLevel.y, 1.0f));
        return model;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TRIXEL_TO_FRAMEBUFFER_H */
