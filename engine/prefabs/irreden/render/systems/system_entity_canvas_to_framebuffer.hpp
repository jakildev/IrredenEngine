#ifndef SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H
#define SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>

#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/common/components/component_world_transform.hpp>

#include <vector>

// Future work: for shape-only entities, the compute-to-canvas step could
// be eliminated entirely by evaluating SDFs directly in the fragment shader.
// The analytical iso-projected surface math from c_shapes_to_trixel.glsl is
// reusable for this purpose. See the Renderer Performance Overhaul plan,
// Phase 3.

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

namespace IRSystem {

constexpr int kMaxEntityCanvasInstances = 512;

template <> struct System<ENTITY_CANVAS_TO_FRAMEBUFFER> {
    struct CanvasInstance {
        FrameDataTrixelToFramebuffer frameData_;
        const C_TriangleCanvasTextures *textures_;
    };

    static std::vector<CanvasInstance> &getInstances() {
        static std::vector<CanvasInstance> instances;
        return instances;
    }

    // Detached SO(3) smooth-rotation forward-scatter instances (P3b / #1475).
    // One per visible detached entity whose canvas has per-axis trixel canvases
    // allocated (rotating off an octahedral snap). Drawn after the gather pass
    // in endTick, scattering the three per-axis canvases straight to the
    // framebuffer (the per-DETACHED-entity analog of the camera-yaw scatter in
    // system_trixel_to_framebuffer::drawPerAxisScatter), depth-composited
    // against the gather content by the framebuffer's GL_LESS test.
    struct ScatterInstance {
        FrameDataTrixelToFramebuffer frameData_;
        const C_PerAxisTrixelCanvases *axes_;
    };

    static std::vector<ScatterInstance> &getScatterInstances() {
        static std::vector<ScatterInstance> instances;
        return instances;
    }

    static SystemId create() {
        // Per-DETACHED-entity SO(3) forward-scatter program (P3b / #1475).
        // Reuses f_peraxis_scatter (the camera-yaw fragment is identical — write
        // color + depth); only the vertex projection differs (residual quat +
        // entity-TRS placement). The shared resources it draws with (QuadVAO,
        // TrixelToFramebufferFrameData, GlobalConstants) are created by
        // TRIXEL_TO_FRAMEBUFFER, which registers before this system.
        IRRender::createNamedResource<ShaderProgram>(
            "PerAxisScatterDetachedProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertPerAxisScatterDetached, ShaderType::VERTEX},
                ShaderStage{IRRender::kFileFragPerAxisScatter, ShaderType::FRAGMENT}
            }
        );

        SystemId s = createSystem<C_EntityCanvas, C_WorldTransform>(
            "EntityCanvasToFramebuffer",
            [](IREntity::EntityId entityId,
               const C_EntityCanvas &entityCanvas,
               const C_WorldTransform &worldTransform) {
                if (!entityCanvas.visible_ || entityCanvas.canvasEntity_ == IREntity::kNullEntity ||
                    static_cast<int>(getInstances().size()) >= kMaxEntityCanvasInstances) {
                    return;
                }

                auto texOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(
                    entityCanvas.canvasEntity_
                );
                if (!texOpt.has_value())
                    return;
                auto *canvasTextures = texOpt.value();

                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                vec2 fbRes = vec2(framebuffer.getResolutionPlusBuffer());
                vec2 mainCanvasSize = IRRender::getMainCanvasSizeTrixels();
                vec2 cameraIso = IRRender::getCameraPosition2DIso();
                vec2 cameraZoom = IRRender::getCameraZoom();

                // The detached canvas texture is rasterized camera-yaw-zeroed in
                // the entity's own model space (buildVoxelFrameData's detached
                // branch), so its de-tile gather phase is keyed to the entity's
                // FIXED world iso position — constant under camera yaw, reused
                // below as `-entityIso` for the gather's `cameraTrixelOffset_`
                // parity (unchanged, so no new #1256-class stripe risk). The
                // screen PLACEMENT, by contrast, must orbit with the rotating
                // world: project the world position under the camera's continuous
                // Z-yaw (#1500), exactly as the world / SDF content does
                // (system_shapes_to_trixel via pos3DtoPos2DIsoYawed). The two
                // coincide at yaw == 0, so cardinal frames stay byte-identical.
                const float visualYaw = IRPrefab::Camera::getYaw();
                vec2 entityIso = pos3DtoPos2DIso(worldTransform.translation_);
                vec2 entityIsoPlacement =
                    pos3DtoPos2DIsoYawed(worldTransform.translation_, visualYaw);

                ivec2 mainCanvasSizeI = ivec2(mainCanvasSize);
                vec2 canvasOriginZ1 = vec2(trixelOriginOffsetZ1(mainCanvasSizeI));
                vec2 entityOnMainCanvas =
                    canvasOriginZ1 + IRMath::floor(cameraIso) + entityIsoPlacement;
                vec2 normalizedPos = entityOnMainCanvas / mainCanvasSize;

                // Game-pixel half of the anti-vibration decomposition —
                // see `IRMath::cameraSubPixelOffsets`. Matches the
                // `TRIXEL_TO_FRAMEBUFFER` call site so detached canvases
                // composite onto the same sub-pixel-snapped grid as the
                // main canvas.
                const IRMath::CameraSubPixelOffsets subPixelOffsets =
                    IRMath::cameraSubPixelOffsets(cameraIso, cameraZoom, ivec2(1));
                vec2 isoPixelOffset = vec2(subPixelOffsets.framebufferGamePxOffset_);

                vec2 entityAPos = vec2(normalizedPos.x - 0.5f, 0.5f - normalizedPos.y);
                vec2 entityFbCenter = vec2(
                    fbRes.x * 0.5f + isoPixelOffset.x + entityAPos.x * fbRes.x * cameraZoom.x,
                    fbRes.y * 0.5f + isoPixelOffset.y + entityAPos.y * fbRes.y * cameraZoom.y
                );

                vec2 entityScale = vec2(entityCanvas.canvasSize_) / mainCanvasSize;
                // Placement only: the composite places each detached canvas
                // texture at the entity's iso position, axis-aligned. A
                // DETACHED entity's full SO(3) rotation is baked into the
                // canvas texture itself by the voxel emit (T-295, via
                // PROPAGATE_CANVAS_ROTATION → C_CanvasLocalRotation →
                // VOXEL_TO_TRIXEL_STAGE_1), so the composite TRS no longer
                // applies any rotation.
                mat4 model = translate(mat4(1.0f), vec3(entityFbCenter, 0.0f));
                model = scale(
                    model,
                    vec3(
                        fbRes.x * cameraZoom.x * entityScale.x,
                        fbRes.y * cameraZoom.y * entityScale.y,
                        1.0f
                    )
                );

                FrameDataTrixelToFramebuffer fd{};
                fd.mpMatrix_ = calcProjectionMatrix(fbRes) * model;
                fd.canvasZoomLevel_ = cameraZoom;
                fd.cameraTrixelOffset_ = -entityIso;
                fd.textureOffset_ = vec2(0.0f);
                fd.distanceOffset_ = 0;
                fd.mouseHoveredTriangleIndex_ = vec2(-1000000.0f);
                fd.effectiveSubdivisionsForHover_ = vec2(1.0f);
                fd.showHoverHighlight_ = 0.0f;

                CanvasInstance inst{};
                inst.frameData_ = fd;
                inst.textures_ = canvasTextures;
                getInstances().push_back(inst);

                // Detached SO(3) smooth rotation (P3b / #1475). If this canvas
                // has per-axis trixel canvases ALLOCATED, the entity is rotating
                // off an octahedral snap: VOXEL_TO_TRIXEL_STAGE_1 skipped its
                // single-canvas voxel emit and routed the visible model faces
                // into the three per-axis canvases instead. Forward-scatter those
                // straight to the framebuffer (bypassing the single-parity de-tile
                // gather, which cannot resolve the smooth mixed-parity centers —
                // the #1256 stripe class). The gather instance pushed above still
                // composites any SDF / text the single canvas holds; the scatter
                // adds the smooth voxels by depth. At a snap nothing is allocated
                // → no scatter, the single-canvas blit renders byte-identically.
                // The cheap !isAllocated early-out keeps static entities off the
                // C_CanvasLocalRotation lookup.
                auto perAxisOpt = IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(
                    entityCanvas.canvasEntity_
                );
                if (!perAxisOpt.has_value() || !(*perAxisOpt.value()).isAllocated() ||
                    static_cast<int>(getScatterInstances().size()) >= kMaxEntityCanvasInstances) {
                    return;
                }
                auto rotationOpt = IREntity::getComponentOptional<C_CanvasLocalRotation>(
                    entityCanvas.canvasEntity_
                );
                if (!rotationOpt.has_value() || !(*rotationOpt.value()).isDetached()) {
                    return;
                }
                const C_PerAxisTrixelCanvases &axes = *perAxisOpt.value();
                const vec4 fullRotation = (*rotationOpt.value()).rotation_;
                const vec4 residual = IRMath::octahedralSnapResidual(fullRotation);

                // perAxisBase mirrors P3a's store trixelFrameOffset for this
                // entity's axis canvases — the identical formula
                // system_trixel_to_framebuffer's world scatter uses — so
                // faceOriginFromInPlane recovers the exact stored model origin
                // (the camera-iso term cancels in the recovery).
                const int subdivisionScale =
                    IRRender::getSubdivisionMode() != IRRender::SubdivisionMode::NONE
                        ? IRPrefab::PerAxisCanvas::subdivisionDensity()
                        : 1;
                const vec2 storeCameraIso = IRRender::getEffectiveCameraIso();
                const ivec2 perAxisBase =
                    trixelOriginOffsetZ1(axes.size_) +
                    ivec2(IRMath::floor(storeCameraIso * static_cast<float>(subdivisionScale)));

                // Placement: map one capped-lattice iso unit (the recovered
                // origin's units) straight to framebuffer px around the entity's
                // screen center. One WORLD iso unit = fbRes*cameraZoom /
                // mainCanvasSize fb px (the detached single-canvas trixel scale,
                // matched so there is no size jump across the snap deadband);
                // lattice = world * subdivisionScale, so divide. iso-y is
                // screen-down and framebuffer y is up → negate the y scale. The
                // entity's iso position + sub-pixel snap are already folded into
                // entityFbCenter, so the scatter reuses the gather's placement.
                const vec2 pixelsPerLattice =
                    fbRes * cameraZoom / mainCanvasSize / static_cast<float>(subdivisionScale);
                mat4 scatterModel = translate(mat4(1.0f), vec3(entityFbCenter, 0.0f));
                scatterModel =
                    scale(scatterModel, vec3(pixelsPerLattice.x, -pixelsPerLattice.y, 1.0f));

                FrameDataTrixelToFramebuffer sfd{};
                sfd.mpMatrix_ = calcProjectionMatrix(fbRes) * scatterModel;
                sfd.distanceOffset_ = 0;
                sfd.perAxisBase_ = perAxisBase;
                // Per-slot model FaceId triplet — MUST be the identical array
                // P3a's store wrote (buildVoxelFrameData: visibleTriplet on the
                // FULL rotation, not the residual). The store encodes the
                // workgroup SLOT into rawDist & 3, so the scatter recovers each
                // face via visibleFaceIds[slot]. Left unset it defaulted to
                // {0,0,0,0} — every slot resolved to X_NEG / axis 0, so the
                // Y- and Z-axis canvases recovered with the wrong in-plane axis
                // (faceOriginFromInPlane) and spanned the wrong face plane
                // (faceSpanCorner), flinging those faces off the cube silhouette
                // instead of meeting at shared edges (#1525). Camera-path twin:
                // drawPerAxisScatter in system_trixel_to_framebuffer.hpp.
                const std::array<IRMath::FaceId, 3> visibleFaces =
                    IRMath::visibleTriplet(fullRotation);
                sfd.visibleFaceIds_ = ivec4(
                    static_cast<int>(visibleFaces[0]),
                    static_cast<int>(visibleFaces[1]),
                    static_cast<int>(visibleFaces[2]),
                    0
                );
                sfd.detachedResidual_ = residual;
                sfd.detachedDepthAxis_ = vec4(IRMath::isoDepthAxisModel(residual), 0.0f);
                // Conservative-coverage dilation needs the framebuffer extent the
                // ortho mpMatrix maps into, to convert a pixel margin to NDC (#1494).
                sfd.scatterFbResolution_ = vec4(fbRes, 0.0f, 0.0f);

                ScatterInstance sinst{};
                sinst.frameData_ = sfd;
                sinst.axes_ = &axes;
                getScatterInstances().push_back(sinst);
            },
            []() {
                getInstances().clear();
                getScatterInstances().clear();
            },
            []() {
                auto &allInstances = getInstances();
                auto &scatterInstances = getScatterInstances();
                if (allInstances.empty() && scatterInstances.empty())
                    return;

                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                framebuffer.bindFramebuffer();

                auto *frameDataBuffer =
                    IRRender::getNamedResource<Buffer>("TrixelToFramebufferFrameData");
                IRRender::getNamedResource<VAO>("QuadVAO")->bind();
                IRRender::device()->setPolygonMode(PolygonMode::FILL);

                // Gather pass: blit each detached canvas (octahedral-snap voxels +
                // any SDF / text) via the single-parity de-tile gather.
                if (!allInstances.empty()) {
                    IRRender::getNamedResource<ShaderProgram>("CanvasToFramebufferProgram")->use();
                    for (auto &inst : allInstances) {
                        frameDataBuffer
                            ->subData(0, sizeof(FrameDataTrixelToFramebuffer), &inst.frameData_);
                        inst.textures_->bind(0, 1, 2);
                        IRRender::device()->drawElements(
                            DrawMode::TRIANGLES,
                            IRShapes2D::kQuadIndicesLength,
                            IndexType::UNSIGNED_SHORT
                        );
                    }
                }

                // Scatter pass (P3b / #1475): forward-scatter each rotating
                // detached entity's three per-axis canvases straight to the
                // framebuffer. Instanced over the canvas grid (one instance per
                // cell), three axes per entity; the framebuffer GL_LESS depth test
                // composites them against the gather content above and against
                // each other. Mirrors system_trixel_to_framebuffer's world
                // drawPerAxisScatter, retargeted at the entity's own canvases.
                //
                // Backend parity (P4 / #1465): the Metal mirror
                // (metal/peraxis_scatter_detached.metal — vertex; fragment reuses
                // f_peraxis_scatter) is a faithful port of v_peraxis_scatter_detached.glsl
                // and was hardware-verified rendering correct smooth SO(3) detached
                // solids on macOS/Metal (IRCanvasStress, off-octahedral-snap poses).
                // The shared FrameDataTrixelToFramebuffer std140/MSL layout is pinned
                // by the static_asserts in ir_render_types.hpp.
                //
                // UNLIT by design (today): the scatter binds only colors_/distances_
                // per axis — NOT the per-axis ao_/sunShadow_ textures — so detached
                // entities composite raw voxel color while rotating. Receiving world
                // AO / sun-shadow on the resolved composite (the trixel-level per-axis
                // design in docs/design/per-axis-trixel-canvas-rotation.md §"Lighting /
                // AO placement") is tracked by #1375 (receive) / #1376 (cast); it is
                // NOT wired here yet. A missing highlight on a spinning detached cube
                // is that pending work, not a regression.
                if (!scatterInstances.empty()) {
                    IRRender::getNamedResource<ShaderProgram>("PerAxisScatterDetachedProgram")
                        ->use();
                    for (auto &inst : scatterInstances) {
                        frameDataBuffer
                            ->subData(0, sizeof(FrameDataTrixelToFramebuffer), &inst.frameData_);
                        const int instanceCount = inst.axes_->size_.x * inst.axes_->size_.y;
                        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
                            const C_PerAxisTrixelCanvases::AxisTextures &tex =
                                inst.axes_->axes_[axis];
                            tex.colors_.second->bind(0);
                            tex.distances_.second->bind(1);
                            IRRender::device()->drawElementsInstanced(
                                DrawMode::TRIANGLES,
                                IRShapes2D::kQuadIndicesLength,
                                IndexType::UNSIGNED_SHORT,
                                instanceCount
                            );
                        }
                    }
                }

                IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            }
        );
        IRRender::tagGpuStage(s, "entityCanvasToFb");
        return s;
    }

    static mat4 calcProjectionMatrix(const vec2 &resolution) {
        return ortho(0.0f, resolution.x, 0.0f, resolution.y, -1.0f, 100.0f);
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H */
