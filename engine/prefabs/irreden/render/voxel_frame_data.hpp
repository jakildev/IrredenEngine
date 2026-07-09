#ifndef IR_PREFAB_VOXEL_FRAME_DATA_H
#define IR_PREFAB_VOXEL_FRAME_DATA_H

// Per-canvas voxel frame-data builder, shared by the voxel raster
// (VOXEL_TO_TRIXEL_STAGE_1) and the screen-space lighting passes
// (COMPUTE_VOXEL_AO, LIGHTING_TO_TRIXEL).
//
// The shared `SingleVoxelFrameData` UBO (binding 7) carries the
// per-canvas state every voxel/lighting shader reads — the visible-face
// triplet, the detached-canvas flag, the cardinal/residual yaw split,
// face-deform matrices, canvas offsets, etc. STAGE_1 authors it per canvas
// during its raster; the lighting passes re-author the iterating canvas's
// frame data before their own dispatch (re-voxelize P4 / #1558) so a second
// lit canvas (a detached re-voxelize solid) reads ITS frame instead of the
// main canvas's stale state. Both call `buildVoxelFrameData` so there is one
// source of truth for that layout.

#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/voxel_dispatch_grid.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <array>

namespace IRSystem {

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

inline void buildVoxelFrameData(
    FrameDataVoxelToCanvas &frameData,
    const C_TriangleCanvasTextures &canvas,
    int liveVoxelCount,
    const C_CanvasLocalRotation &canvasRotation
) {
    // The single-canvas raster always uploads perAxisRoute_ == 0 (byte-
    // identical to master). The smooth-Z-yaw per-axis pass flips it to 1/2/3
    // locally per axis and restores it afterward (see dispatchPerAxisCanvases).
    frameData.perAxisRoute_ = 0;

    const auto renderMode = IRRender::getSubdivisionMode();
    const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
    // Clamp to 1: voxelDispatchGridForCount divides by the count (and asserts
    // count > 0 at entry), and the lighting passes author frame data
    // for canvases whose pool is EMPTY (authorIteratingCanvasVoxelFrame /
    // restoreMainCanvasVoxelFrame have no liveVoxelCount gate — observed as a
    // SIGFPE on a lit scene whose main canvas holds zero voxels, #1619 step-0
    // harness). This clamp is the one deliberate empty-pool exception to that
    // contract; voxelCount_ below still carries the honest 0, which gates all
    // shader-side work.
    const ivec2 dispatchGrid = voxelDispatchGridForCount(IRMath::max(liveVoxelCount, 1));

    frameData.cameraTrixelOffset_ = IRRender::getEffectiveCameraIso();
    frameData.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(canvas.size_);
    frameData.voxelRenderOptions_ = ivec2(static_cast<int>(renderMode), effectiveSubdivisions);
    frameData.voxelDispatchGrid_ = dispatchGrid;
    frameData.voxelCount_ = liveVoxelCount;
    frameData.canvasSizePixels_ = canvas.size_;
    // Per-voxel occlusion depth axis (#1462). World canvas + the smooth-Z-yaw
    // per-axis route keep the fixed (1,1,1) iso depth axis (byte-identical
    // x+y+z); the detached branch below overrides it with the entity-rotated
    // axis. frameData_ is a reused member, so this must be reset every frame so
    // a prior detached canvas's axis can't leak into a world frame.
    frameData.voxelDepthAxis_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    // World-receive opt-in (#1576 P4b-2). Default OFF (.w == 0) — only the
    // re-voxelize branch below sets it when the owner opts in. Reset every frame
    // (reused member) so a prior world-placed detached canvas can't leak its
    // offset into a world / non-opt-in frame and corrupt its lighting.
    frameData.detachedWorldReceive_ = vec4(0.0f, 0.0f, 0.0f, 0.0f);

    // A non-zero `canvasRotation` marks a detached entity canvas (the main
    // world canvas keeps the all-zero `C_CanvasLocalRotation::kSentinelNoRotation`
    // sentinel). A detached canvas rasterizes its voxels in the entity's own
    // model space — camera yaw zeroed — and `faceDeform_` carries the full SO(3)
    // per-face deformation for the entity's rotation (T-295).
    const bool detachedCanvas = canvasRotation.isDetached();
    frameData.isDetachedCanvas_ = detachedCanvas ? 1.0f : 0.0f;
    if (detachedCanvas) {
        // A detached canvas rasters its pool in MODEL space: the camera pan
        // must not shift the content inside the canvas (the composite places
        // the canvas at the entity's camera-relative screen position — a
        // camera term here would apply the pan twice AND walk the content off
        // the canvas edge under any integer camera offset). The world canvas
        // set above keeps the camera term. At camera (0,0) this zero is
        // byte-identical, which is why the pan desync survived every
        // camera-at-origin reference capture (#1555's cull face of the same
        // model-vs-camera-space confusion is fixed at the cull sites in
        // SYSTEM_VOXEL_TO_TRIXEL_STAGE_1).
        frameData.cameraTrixelOffset_ = vec2(0.0f);
    }
    if (detachedCanvas && canvasRotation.reVoxelize_) {
        // Re-voxelize detached canvas (#1553): the entity's full rotation is
        // baked into the private pool's CELL positions by
        // SYSTEM_REBUILD_DETACHED_VOXELS, so this canvas rasterizes its pool with
        // CARDINAL/static frame data — no camera yaw, no per-face SO(3) skew
        // (applying the rotation a second time as a deform would re-introduce the
        // 2D warp #1551 traced). Mirrors the main world canvas at yaw 0;
        // isDetachedCanvas_ stays 1.0 so the emit keeps the screen-locked
        // (no camera-pan-offset) path, and voxelDepthAxis_ keeps the (1,1,1)
        // default set above.
        frameData.visualYaw_ = 0.0f;
        frameData.rasterYaw_ = 0.0f;
        frameData.residualYaw_ = 0.0f;
        const auto cardinalIndex = IRMath::rasterYawCardinalIndex(0.0f);
        const auto visibleFaces = IRMath::visibleFaceTripletCardinal(cardinalIndex);
        // Re-voxelize canvases mark `.w = 1` (#1557 Option B / #1570). The marker
        // tells `c_voxel_to_trixel_stage_{1,2}` to dilate each emitted face ±1px
        // along its in-plane iso axes to close the round-to-cell sub-cell gaps.
        // It NO LONGER bypasses the exposed-mask gate: the GPU scatter
        // (c_revoxelize_detached MODE 1) now authors the ROTATED-frame
        // face-occlusion mask from dest-grid adjacency — the GPU twin of
        // REBUILD_GRID_VOXELS' #1720 CPU mask — so stage 1/2 gate re-voxelize on
        // `faceIsExposed` exactly like the GRID path. The old bypass (emit all
        // three cardinal faces, depth-resolve the front) existed only because that
        // mask used to be stale (P2 #1556 dropped P1's recompute without moving it
        // to the GPU); its slot-tie checkerboard winner drove AO hatching on flat
        // surfaces that GRID never had. Other canvases keep `.w = 0`
        // (no dilation, real exposed-mask gate, byte-identical to master).
        frameData.visibleFaceIds_ = ivec4(
            static_cast<int>(visibleFaces[0]),
            static_cast<int>(visibleFaces[1]),
            static_cast<int>(visibleFaces[2]),
            1
        );
        const mat2 fd0 = IRMath::faceDeformationMatrix(visibleFaces[0], 0.0f);
        const mat2 fd1 = IRMath::faceDeformationMatrix(visibleFaces[1], 0.0f);
        const mat2 fd2 = IRMath::faceDeformationMatrix(visibleFaces[2], 0.0f);
        frameData.faceDeform_[0] = vec4(fd0[0], fd0[1]);
        frameData.faceDeform_[1] = vec4(fd1[0], fd1[1]);
        frameData.faceDeform_[2] = vec4(fd2[0], fd2[1]);
        // World receive (#1576 P4b-2; the default since #1624 — the owner's
        // C_EntityCanvas::screenLocked_ opts out, propagated onto
        // canvasRotation). When world-placed, publish the world cell origin +
        // the enable flag so
        // COMPUTE_VOXEL_AO / LIGHTING_TO_TRIXEL recover each voxel's WORLD pos as
        // (model pos + .xyz) and sample the shared world sun-shadow map + light
        // volume there. Off → the default screen-locked overlay (.w == 0) stays
        // byte-identical. Only the re-voxelize path carries this — the
        // octahedral-snap / per-face-deform DETACHED branch below recovers pos
        // differently (residual face skew), so it is not world-receive-capable.
        frameData.detachedWorldReceive_ =
            vec4(canvasRotation.worldCellOffset_, canvasRotation.worldPlaced_ ? 1.0f : 0.0f);
        return;
    }
    if (detachedCanvas) {
        frameData.visualYaw_ = 0.0f;
        frameData.rasterYaw_ = 0.0f;
        frameData.residualYaw_ = 0.0f;
        // Snap to the nearest of the 24 cube orientations; the residual is the
        // continuous leftover the per-face deform (single-canvas) and the
        // per-axis forward-scatter (off-snap) act on. A cube is invariant under
        // the snap, so this keeps the per-face skew small enough to stay clean
        // (T-295).
        const vec4 residual = IRMath::octahedralSnapResidual(canvasRotation.rotation_);
        // Face-selection + occlusion-depth FRAME for the single-canvas detached
        // emit: keep each voxel at its model iso position and only skew face
        // SHAPE by the residual (faceDeformationMatrixSO3 below), so the visible
        // set is the FULL orientation's front faces. (The retired per-axis
        // forward-scatter, #1560, instead repositioned every corner by the
        // residual alone and keyed on visibleTriplet(residual); detached SO(3)
        // now renders through the re-voxelize branch above, not this deform.)
        const vec4 selectionRotation = canvasRotation.rotation_;
        // Per-entity SO(3) visible triplet: the three faces the camera actually
        // sees, one per axis in X/Y/Z slot order. Previously hardcoded to
        // {X_NEG, Y_NEG, Z_NEG} regardless of rotation, so the deform below ran
        // on back-facing faces and entities glitched instead of rotating
        // (#1386). At identity the resolver returns the same legacy triplet, so
        // non-rotating entities stay byte-identical.
        const std::array<IRMath::FaceId, 3> visibleFaces =
            IRMath::visibleTriplet(selectionRotation);
        frameData.visibleFaceIds_ = ivec4(
            static_cast<int>(visibleFaces[0]),
            static_cast<int>(visibleFaces[1]),
            static_cast<int>(visibleFaces[2]),
            0
        );
        // Per-voxel occlusion depth projects onto the SAME frame's iso axis
        // `R⁻¹·(1,1,1)` (#1462), so face visibility and occlusion order stay on
        // one frame. Identity entity → (1,1,1) → byte-identical. Read only by
        // the single-canvas emit; the off-snap per-axis store keys depth on the
        // raw x+y+z origin-recovery metric, so this is inert there but kept on
        // the matching frame for clarity.
        frameData.voxelDepthAxis_ = vec4(IRMath::isoDepthAxisModel(selectionRotation), 0.0f);
        // Per-slot deform upload: slot 0 / 1 / 2 carries the X / Y / Z axis face
        // matrix. `visibleTriplet` returns faces in axis order, so each slot's
        // axis is fixed regardless of polarity — the deform is axis-only (X_NEG
        // and X_POS share the X matrix), so it is unchanged.
        const mat2 fdX = IRMath::faceDeformationMatrixSO3(IRMath::kXFace, residual);
        const mat2 fdY = IRMath::faceDeformationMatrixSO3(IRMath::kYFace, residual);
        const mat2 fdZ = IRMath::faceDeformationMatrixSO3(IRMath::kZFace, residual);
        frameData.faceDeform_[0] = vec4(fdX[0], fdX[1]);
        frameData.faceDeform_[1] = vec4(fdY[0], fdY[1]);
        frameData.faceDeform_[2] = vec4(fdZ[0], fdZ[1]);
        return;
    }

    // Main world canvas: rasterYaw picks the integer trixel basis permutation
    // (T-055); residualYaw is folded into faceDeform_[] which the trixel emit
    // shader applies to each sub-pixel offset in 2D iso space (T-293, replaces
    // the T-058 / T-322 screen-space bilinear residual composite). At every
    // non-zero cardinal the WORLD face whose iso footprint lands in each
    // diamond slot rotates with the camera — `visibleFaceIds_` carries the
    // current slot ↔ FaceId map (#1278).
    frameData.visualYaw_ = IRPrefab::Camera::getYaw();
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::computeYawSplit(frameData.visualYaw_);
    frameData.rasterYaw_ = rasterYaw;
    frameData.residualYaw_ = residualYaw;
    const auto cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);
    const auto visibleFaces = IRMath::visibleFaceTripletCardinal(cardinalIndex);
    frameData.visibleFaceIds_ = ivec4(
        static_cast<int>(visibleFaces[0]),
        static_cast<int>(visibleFaces[1]),
        static_cast<int>(visibleFaces[2]),
        0
    );
    // Per-slot deformation (axis-only; X_NEG and X_POS share the X-axis
    // matrix). At cardinal 0 the per-slot order {X_NEG, Y_NEG, Z_NEG}
    // collapses to the legacy axis order {kXFace, kYFace, kZFace}, so the
    // upload is bit-identical to pre-#1278 master.
    const mat2 fd0 = IRMath::faceDeformationMatrix(visibleFaces[0], residualYaw);
    const mat2 fd1 = IRMath::faceDeformationMatrix(visibleFaces[1], residualYaw);
    const mat2 fd2 = IRMath::faceDeformationMatrix(visibleFaces[2], residualYaw);
    frameData.faceDeform_[0] = vec4(fd0[0], fd0[1]);
    frameData.faceDeform_[1] = vec4(fd1[0], fd1[1]);
    frameData.faceDeform_[2] = vec4(fd2[0], fd2[1]);
}

// Author the iterating canvas's voxel frame data into the shared UBO before a
// screen-space lighting dispatch (COMPUTE_VOXEL_AO / LIGHTING_TO_TRIXEL), so a
// second lit canvas (a detached re-voxelize solid) is shaded with ITS frame
// (visible triplet, detached flag, yaw split) instead of whatever canvas
// VOXEL_TO_TRIXEL_STAGE_1 left resident (re-voxelize P4 / #1558). No-op for a
// canvas with no voxel pool: the UBO keeps its prior state, so a pure-SDF lit
// canvas is unchanged. `getComponentOptional` on the iterating canvas is the
// canvas-iteration pattern (few canvases; cf.
// system_trixel_to_framebuffer.hpp:63), not the per-voxel ECS footgun.
inline void authorIteratingCanvasVoxelFrame(
    FrameDataVoxelToCanvas &scratch,
    Buffer *voxelFrameDataBuf,
    IREntity::EntityId entity,
    const C_TriangleCanvasTextures &canvasTextures
) {
    auto pool = IREntity::getComponentOptional<C_VoxelPool>(entity);
    auto rotation = IREntity::getComponentOptional<C_CanvasLocalRotation>(entity);
    if (!pool.has_value() || !rotation.has_value()) {
        return;
    }
    buildVoxelFrameData(
        scratch,
        canvasTextures,
        (*pool.value()).getLiveVoxelCount(),
        *rotation.value()
    );
    voxelFrameDataBuf->subData(0, sizeof(FrameDataVoxelToCanvas), &scratch);
}

// Resolve the main world canvas's voxel-frame inputs (textures + pool +
// rotation) once per frame in a lighting pass's beginTick, for the endTick
// restore below (#1558). beginTick lookups are once-per-frame, not the
// per-entity footgun. Writes nullptr for any absent component (or all three
// when `mainEntity` is null), which restoreMainCanvasVoxelFrame treats as a
// no-op. Shared by COMPUTE_VOXEL_AO + LIGHTING_TO_TRIXEL so the resolution
// side stays symmetric with the restore side.
inline void resolveMainCanvasVoxelFrameInputs(
    IREntity::EntityId mainEntity,
    const C_TriangleCanvasTextures **outTextures,
    const C_VoxelPool **outPool,
    const C_CanvasLocalRotation **outRotation
) {
    *outTextures = nullptr;
    *outPool = nullptr;
    *outRotation = nullptr;
    if (mainEntity == IREntity::kNullEntity) {
        return;
    }
    auto tex = IREntity::getComponentOptional<C_TriangleCanvasTextures>(mainEntity);
    auto pool = IREntity::getComponentOptional<C_VoxelPool>(mainEntity);
    auto rotation = IREntity::getComponentOptional<C_CanvasLocalRotation>(mainEntity);
    if (tex.has_value())
        *outTextures = tex.value();
    if (pool.has_value())
        *outPool = pool.value();
    if (rotation.has_value())
        *outRotation = rotation.value();
}

// Re-author the MAIN world canvas's voxel frame data into the shared UBO at a
// lighting pass's endTick, so downstream stages (BAKE / COMPUTE_SUN_SHADOW /
// FOG / TRIXEL_TO_FRAMEBUFFER) keep reading the world frame after a detached
// canvas temporarily authored its own (#1558). The main canvas's inputs are
// resolved once per frame in the caller's beginTick (never held across frames);
// pass null pointers to no-op (e.g. a scene with no voxel main canvas).
inline void restoreMainCanvasVoxelFrame(
    FrameDataVoxelToCanvas &scratch,
    Buffer *voxelFrameDataBuf,
    const C_TriangleCanvasTextures *mainTextures,
    const C_VoxelPool *mainPool,
    const C_CanvasLocalRotation *mainRotation
) {
    if (mainTextures == nullptr || mainPool == nullptr || mainRotation == nullptr) {
        return;
    }
    buildVoxelFrameData(scratch, *mainTextures, mainPool->getLiveVoxelCount(), *mainRotation);
    voxelFrameDataBuf->subData(0, sizeof(FrameDataVoxelToCanvas), &scratch);
}

} // namespace IRSystem

#endif /* IR_PREFAB_VOXEL_FRAME_DATA_H */
