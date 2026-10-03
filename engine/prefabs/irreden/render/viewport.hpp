#ifndef IR_PREFAB_VIEWPORT_H
#define IR_PREFAB_VIEWPORT_H

// Secondary viewports: a GUI-pinned view of tagged entities through a camera
// of its own, independent of the world camera.
//
// A viewport is two entities. The camera entity (`C_ViewportCamera` +
// `C_ZoomLevel` + its yaw in `C_LocalTransform`) is the viewport id. The
// canvas entity is a private voxel-pool canvas carrying `C_CanvasCamera`, so
// the voxel, AO and lighting stages raster and shade it like any other canvas
// but through that camera. Every frame SYNC_VIEWPORT_SUBJECTS rasterizes the
// voxel sets of the entities tagged `C_ViewportSubject` into the canvas's pool
// and VIEWPORT_TO_FRAMEBUFFER composites the canvas into the GUI rectangle.
//
// Design and limits: docs/design/secondary-viewport.md.

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_camera.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_viewport_camera.hpp>
#include <irreden/render/components/component_viewport_subject.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IRPrefab::Viewport {

struct Desc {
    // GUI-canvas trixels; `rectOrigin_` is the rectangle's top-left corner.
    IRMath::ivec2 rectOrigin_{0};
    IRMath::ivec2 rectSize_{96};
    float zoom_ = 1.0f;
    float yawRadians_ = 0.0f;
    IRMath::vec3 focus_{0.0f};
};

// One tagged voxel set, as SYNC_VIEWPORT_SUBJECTS gathers it.
struct SubjectPart {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    const IRComponents::C_VoxelSetNew *set_ = nullptr;
    IRMath::vec3 translation_{0.0f};
};

namespace detail {

// Framebuffer pixels one canvas texel covers. A trixel is two pixels wide and
// one tall at zoom 1; raster density divides the zoom back out.
inline IRMath::vec2 texelFramebufferSize(float zoom, int subdivisions) {
    const float scale = zoom / static_cast<float>(IRMath::max(subdivisions, 1));
    return IRMath::vec2(2.0f * scale, scale);
}

// The canvas size, in trixels, whose composite covers @p rectFramebufferSize
// without overflowing it. Both dimensions are even: the raster origin is the
// integer half-size, which is the canvas's true center only then.
inline IRMath::ivec2
canvasSizeForRect(IRMath::vec2 rectFramebufferSize, float zoom, int subdivisions) {
    const IRMath::vec2 texel = texelFramebufferSize(zoom, subdivisions);
    const IRMath::ivec2 whole = IRMath::ivec2(IRMath::floor(rectFramebufferSize / texel));
    return IRMath::max(whole - (whole & IRMath::ivec2(1)), IRMath::ivec2(2));
}

// The framebuffer rectangle (pixels, Y down from the top edge) a GUI
// rectangle covers. The GUI canvas spans the whole framebuffer.
struct FramebufferRect {
    IRMath::vec2 origin_{0.0f};
    IRMath::vec2 size_{0.0f};
};

inline FramebufferRect framebufferRect(
    IRMath::ivec2 rectOrigin,
    IRMath::ivec2 rectSize,
    IRMath::ivec2 guiCanvasSize,
    IRMath::ivec2 framebufferSize
) {
    const IRMath::vec2 scale =
        IRMath::vec2(framebufferSize) / IRMath::vec2(IRMath::max(guiCanvasSize, IRMath::ivec2(1)));
    return FramebufferRect{IRMath::vec2(rectOrigin) * scale, IRMath::vec2(rectSize) * scale};
}

// The GUI canvas and main framebuffer extents a composite maps a GUI
// rectangle through; read once per frame in a system's beginTick.
struct CompositeExtent {
    IRMath::ivec2 guiCanvasSize_{1};
    IRMath::ivec2 framebufferSize_{1};
};

inline CompositeExtent compositeExtent() {
    return CompositeExtent{
        IREntity::getComponent<IRComponents::C_SizeTriangles>(IRRender::getCanvas("gui")).size_,
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer")
            .getResolutionPlusBuffer()
    };
}

// Canvas-texel shift that puts the subject's @p focus at the canvas center;
// with no shift the subject's own center sits there. The sum of the result is
// even, the lattice-parity contract on C_CanvasCamera::panIso_.
inline IRMath::ivec2
panTexelsForFocus(IRMath::vec3 focus, IRMath::vec4 viewToWorld, int subdivisions) {
    const IRMath::vec3 viewFocus =
        IRMath::rotateVectorByQuat(focus, IRMath::quatInverse(viewToWorld));
    const IRMath::vec2 focusTexels =
        IRMath::pos3DtoPos2DIso(viewFocus) * static_cast<float>(IRMath::max(subdivisions, 1));
    IRMath::ivec2 pan = -IRMath::roundVec(focusTexels);
    if (((pan.x + pan.y) & 1) != 0) {
        pan.y += 1;
    }
    return pan;
}

// `panIso_` for a texel shift: the raster floors `panIso_ * subdivisions`, so
// the quarter-texel bias keeps the product from landing one texel short.
inline IRMath::vec2 panIsoForTexels(IRMath::ivec2 panTexels, int subdivisions) {
    return (IRMath::vec2(panTexels) + IRMath::vec2(0.25f)) /
           static_cast<float>(IRMath::max(subdivisions, 1));
}

// Where the parts land in one centered box of cells. A part's cells are its
// dense box, placed by its local origin plus its world translation and snapped
// to whole cells relative to the first part.
struct SubjectBox {
    IRMath::ivec3 size_{0};
    // Per part, the cell of the box its voxel (0,0,0) occupies.
    std::vector<IRMath::ivec3> partOrigins_;
};

inline IRMath::vec3 partWorldOrigin(const SubjectPart &part) {
    const IRComponents::C_VoxelSetNew &set = *part.set_;
    const IRMath::vec3 localOrigin =
        set.numVoxels_ > 0 ? set.positions_[0].pos_ : set.stagedOrigin();
    return part.translation_ + localOrigin;
}

// @p parts must already be in their drawing order; @p box keeps its capacity.
inline void layoutSubjectBox(std::span<const SubjectPart> parts, SubjectBox &box) {
    box.partOrigins_.clear();
    box.size_ = IRMath::ivec3(0);
    if (parts.empty()) {
        return;
    }
    const IRMath::vec3 reference = partWorldOrigin(parts[0]);
    IRMath::ivec3 boxMin(0);
    IRMath::ivec3 boxMax(parts[0].set_->size_);
    for (const SubjectPart &part : parts) {
        const IRMath::ivec3 origin = IRMath::roundVec3HalfUp(partWorldOrigin(part) - reference);
        box.partOrigins_.push_back(origin);
        boxMin = IRMath::min(boxMin, origin);
        boxMax = IRMath::max(boxMax, origin + part.set_->size_);
    }
    for (IRMath::ivec3 &origin : box.partOrigins_) {
        origin -= boxMin;
    }
    box.size_ = boxMax - boxMin;
}

// Cells the re-voxelize resample walks for a box of @p size: the cube
// enclosing the box under any rotation.
inline int rotatedCellCount(IRMath::ivec3 size) {
    const float radius = IRMath::length(IRMath::vec3(size - IRMath::ivec3(1)) * 0.5f);
    const int side = 2 * static_cast<int>(IRMath::ceil(radius)) + 1;
    return side * side * side;
}

// True when a box of @p size fits the shared voxel buffers, at rest and while
// its canvas rotates.
inline bool boxFitsVoxelBuffers(IRMath::ivec3 size, int capacity) {
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
        return false;
    }
    const std::int64_t cells = static_cast<std::int64_t>(size.x) * size.y * size.z;
    return cells <= capacity && rotatedCellCount(size) <= capacity;
}

// Replace the pool with one centered box of @p size cells, all inactive.
// Span indices are pool-relative, so a resized box takes a fresh pool rather
// than a second span beside the freed one: the resample treats the pool's
// whole allocated prefix as the solid.
inline void rebuildPoolBox(IRComponents::C_VoxelPool &pool, IRMath::ivec3 size) {
    pool = IRComponents::C_VoxelPool{size};
    const int count = size.x * size.y * size.z;
    if (count <= 0) {
        return;
    }
    IRRender::VoxelPoolAllocation span = pool.allocateVoxels(static_cast<unsigned int>(count));
    const IRMath::vec3 origin =
        IRComponents::anchorOffset(IRComponents::EntityAnchor::CENTER, size);
    IRMath::iterateAABB(IRMath::ivec3(0), size - IRMath::ivec3(1), [&](int x, int y, int z) {
        const int index = IRMath::index3DtoIndex1D(IRMath::ivec3(x, y, z), size);
        const IRRender::VoxelGpuPosition position{IRMath::vec3(x, y, z) + origin, 0.0f};
        span.positions_[index] = position;
        span.positionGlobals_[index] = position;
        span.voxels_[index].color_ = IRMath::Color{0, 0, 0, 0};
    });
    pool.queuePositionRange(0, static_cast<std::size_t>(count));
}

// Write @p parts' authored records into the pool's box. A later part overdraws
// an earlier one where their cells overlap. Bone, priority and rotated-emit
// state describe the source span, not this one, and the face bits are
// re-derived by the raster.
inline void fillPoolBox(
    IRComponents::C_VoxelPool &pool, std::span<const SubjectPart> parts, const SubjectBox &box
) {
    std::vector<IRComponents::C_Voxel> &voxels = pool.getColors();
    const std::size_t count = static_cast<std::size_t>(pool.getLiveVoxelCount());
    for (std::size_t i = 0; i < count; ++i) {
        voxels[i].color_.alpha_ = 0;
    }
    for (std::size_t partIndex = 0; partIndex < parts.size(); ++partIndex) {
        const IRMath::ivec3 partSize = parts[partIndex].set_->size_;
        const std::span<const IRComponents::C_Voxel> records =
            parts[partIndex].set_->authoredRecords();
        if (records.size() != static_cast<std::size_t>(partSize.x * partSize.y * partSize.z)) {
            continue;
        }
        const IRMath::ivec3 origin = box.partOrigins_[partIndex];
        IRMath::iterateAABB(
            IRMath::ivec3(0),
            partSize - IRMath::ivec3(1),
            [&](int x, int y, int z) {
                const IRMath::ivec3 cell(x, y, z);
                const IRComponents::C_Voxel &record =
                    records[IRMath::index3DtoIndex1D(cell, partSize)];
                if (record.color_.alpha_ == 0) {
                    return;
                }
                IRComponents::C_Voxel &voxel =
                    voxels[IRMath::index3DtoIndex1D(origin + cell, box.size_)];
                voxel = record;
                voxel.flags_ &=
                    static_cast<std::uint8_t>(~IRComponents::VoxelFlags::kFaceOccludedMask);
                voxel.bone_id_ = 0;
                voxel.reserved_ = 0;
            }
        );
    }
    pool.resyncActiveMaskFromColors(0, count);
    pool.markRecordsChanged();
}

inline IRComponents::C_ViewportCamera *cameraOrNull(IREntity::EntityId viewport) {
    if (viewport == IREntity::kNullEntity || !IREntity::entityExists(viewport)) {
        return nullptr;
    }
    auto camera = IREntity::getComponentOptional<IRComponents::C_ViewportCamera>(viewport);
    return camera.has_value() ? camera.value() : nullptr;
}

template <typename Component> Component *componentOrNull(IREntity::EntityId entity) {
    auto component = IREntity::getComponentOptional<Component>(entity);
    return component.has_value() ? component.value() : nullptr;
}

// Reallocate the canvas's textures when the rectangle, zoom or raster density
// moved its size. A frame-boundary operation: no stage holds the textures
// between the UPDATE drain and VOXEL_TO_TRIXEL_STAGE_1.
inline void resizeCanvas(IREntity::EntityId canvas, IRMath::ivec2 size) {
    auto *sizeTriangles = componentOrNull<IRComponents::C_SizeTriangles>(canvas);
    if (sizeTriangles == nullptr || sizeTriangles->size_ == size) {
        return;
    }
    sizeTriangles->size_ = size;
    if (auto *textures = componentOrNull<IRComponents::C_TriangleCanvasTextures>(canvas)) {
        textures->onDestroy();
        *textures = IRComponents::C_TriangleCanvasTextures{size};
    }
    if (auto *ao = componentOrNull<IRComponents::C_CanvasAOTexture>(canvas)) {
        ao->onDestroy();
        *ao = IRComponents::C_CanvasAOTexture{size};
    }
}

inline float snapZoom(float zoom) {
    return IRMath::snapToPowerOfTwo(
        IRMath::clamp(
            zoom,
            IRConstants::kTrixelCanvasZoomMin.x,
            IRConstants::kTrixelCanvasZoomMax.x
        )
    );
}

// The camera half of the per-frame sync: publish the viewport camera onto its
// canvas and size the canvas to the rectangle.
// @p subdivisions is the raster density the canvas runs at for @p zoom.
inline void syncCanvasCamera(
    const IRComponents::C_ViewportCamera &camera,
    IRMath::vec4 viewToWorld,
    IRMath::vec2 zoom,
    int subdivisions,
    IRMath::ivec2 guiCanvasSize,
    IRMath::ivec2 framebufferSize
) {
    const IREntity::EntityId canvas = camera.canvasEntity_;
    auto *canvasCamera = componentOrNull<IRComponents::C_CanvasCamera>(canvas);
    auto *canvasRotation = componentOrNull<IRComponents::C_CanvasLocalRotation>(canvas);
    if (canvasCamera == nullptr || canvasRotation == nullptr) {
        return;
    }
    canvasCamera->rotation_ = viewToWorld;
    canvasCamera->zoom_ = zoom;
    canvasCamera->panIso_ =
        panIsoForTexels(panTexelsForFocus(camera.focus_, viewToWorld, subdivisions), subdivisions);
    // The pool's cells are resampled into the camera's frame, so the canvas
    // rasters with cardinal frame data and never joins the world's shadows.
    canvasRotation->rotation_ = IRMath::quatInverse(viewToWorld);
    canvasRotation->reVoxelize_ = true;
    canvasRotation->worldPlaced_ = false;
    canvasRotation->castsWorldShadow_ = false;

    const FramebufferRect rect =
        framebufferRect(camera.rectOrigin_, camera.rectSize_, guiCanvasSize, framebufferSize);
    resizeCanvas(canvas, canvasSizeForRect(rect.size_, zoom.x, subdivisions));
}

// The subject half: make the canvas's pool hold exactly @p parts' voxels.
// Runs after syncCanvasCamera, which decides whether the canvas is rotated.
inline void syncSubjectPool(
    IRComponents::C_ViewportCamera &camera, std::span<const SubjectPart> parts, SubjectBox &box
) {
    const IREntity::EntityId canvas = camera.canvasEntity_;
    IRComponents::C_VoxelPool *pool = IRPrefab::VoxelPool::detail::poolForCanvas(canvas);
    if (pool == nullptr) {
        return;
    }
    layoutSubjectBox(parts, box);
    const bool drawable =
        boxFitsVoxelBuffers(box.size_, IRRender::VoxelPoolConfig::getMaxAllocationSizeTotal());
    const IRMath::ivec3 size = drawable ? box.size_ : IRMath::ivec3(0);
    const IREntity::EntityId subject = drawable ? parts[0].entity_ : IREntity::kNullEntity;
    auto *resample = componentOrNull<IRComponents::C_DetachedRevoxelizeBuffer>(canvas);

    const bool oversize = !drawable && !parts.empty();
    if (oversize && !camera.subjectOversize_) {
        IRE_LOG_WARN(
            "Viewport canvas {}: subject box {}x{}x{} exceeds the shared voxel buffers; not drawn",
            canvas,
            box.size_.x,
            box.size_.y,
            box.size_.z
        );
    }
    camera.subjectOversize_ = oversize;

    const bool rebuilt = pool->getVoxelPoolSize3D() != size;
    if (rebuilt) {
        rebuildPoolBox(*pool, size);
        // Sized to the old pool; the raster stands it back up at the new one.
        if (resample != nullptr) {
            resample->onDestroy();
        }
    }
    if (drawable) {
        fillPoolBox(*pool, parts, box);
        if (rebuilt || camera.drawnSubject_ != subject) {
            pool->setEntityIdForRange(
                0,
                static_cast<std::size_t>(pool->getLiveVoxelCount()),
                subject
            );
        }
    }
    camera.drawnSubject_ = subject;
}

// Stand up the camera entity for an existing voxel-pool canvas. The canvas
// carries `C_VoxelPool`, `C_CanvasLocalRotation` and `C_CanvasCamera`; its
// texture components are optional, so a headless canvas is a valid target.
inline IREntity::EntityId createCamera(IREntity::EntityId canvas, const Desc &desc) {
    IRComponents::C_LocalTransform transform{};
    transform.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), desc.yawRadians_);
    IRComponents::C_ViewportCamera camera{
        canvas,
        desc.rectOrigin_,
        IRMath::max(desc.rectSize_, IRMath::ivec2(1))
    };
    camera.focus_ = desc.focus_;
    return IREntity::createEntity(
        transform,
        camera,
        IRComponents::C_ZoomLevel{snapZoom(desc.zoom_)}
    );
}

} // namespace detail

// Create a viewport and return its id. The canvas starts empty; tag subjects
// with `setSubject` or `C_ViewportSubject`. Main thread, outside archetype
// iteration.
inline IREntity::EntityId create(const Desc &desc, std::string canvasName = "viewport") {
    // The world-placed detached-canvas bundle (pool + lighting components);
    // the sync sizes the pool and textures from the subject and rectangle.
    const IREntity::EntityId canvas =
        IRPrefab::EntityCanvas::createWithVoxelPool(canvasName, IRMath::ivec2(1), IRMath::ivec3(0))
            .canvasEntity_;
    IREntity::setComponent(canvas, IRComponents::C_CanvasCamera{});
    return detail::createCamera(canvas, desc);
}

inline bool isViewport(IREntity::EntityId viewport) {
    return detail::cameraOrNull(viewport) != nullptr;
}

inline void setRect(IREntity::EntityId viewport, IRMath::ivec2 origin, IRMath::ivec2 size) {
    if (auto *camera = detail::cameraOrNull(viewport)) {
        camera->rectOrigin_ = origin;
        camera->rectSize_ = IRMath::max(size, IRMath::ivec2(1));
    }
}

// Zoom snaps to a power of two inside the trixel-canvas zoom range, like the
// world camera's.
inline void setCamera(IREntity::EntityId viewport, float zoom, float yawRadians) {
    if (!isViewport(viewport)) {
        return;
    }
    if (auto *zoomLevel = detail::componentOrNull<IRComponents::C_ZoomLevel>(viewport)) {
        zoomLevel->zoom_ = IRMath::vec2(detail::snapZoom(zoom));
    }
    if (auto *transform = detail::componentOrNull<IRComponents::C_LocalTransform>(viewport)) {
        transform->rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), yawRadians);
    }
}

inline void setFocus(IREntity::EntityId viewport, IRMath::vec3 focus) {
    if (auto *camera = detail::cameraOrNull(viewport)) {
        camera->focus_ = focus;
    }
}

inline void setVisible(IREntity::EntityId viewport, bool visible) {
    if (auto *camera = detail::cameraOrNull(viewport)) {
        camera->visible_ = visible;
    }
}

// Make @p subject the only entity the viewport draws; kNullEntity clears it.
// Both tag changes are deferred into one structural drain, so two entities
// never share the viewport for a frame and the call is legal mid-iteration.
inline void setSubject(IREntity::EntityId viewport, IREntity::EntityId subject) {
    bool alreadyTagged = false;
    IREntity::forEachComponent<IRComponents::C_ViewportSubject>(
        [&](IREntity::EntityId &entity, IRComponents::C_ViewportSubject &tag) {
            if (tag.viewport_ != viewport) {
                return;
            }
            if (entity == subject) {
                alreadyTagged = true;
                return;
            }
            IREntity::removeComponentDeferred<IRComponents::C_ViewportSubject>(entity);
        }
    );
    if (subject != IREntity::kNullEntity && !alreadyTagged) {
        IREntity::setComponentDeferred(subject, IRComponents::C_ViewportSubject{viewport});
    }
}

// The entity the viewport drew last frame: the tagged entity with the lowest
// id, or kNullEntity while nothing is drawn.
inline IREntity::EntityId drawnSubject(IREntity::EntityId viewport) {
    const auto *camera = detail::cameraOrNull(viewport);
    return camera != nullptr ? camera->drawnSubject_ : IREntity::kNullEntity;
}

inline IREntity::EntityId canvasOf(IREntity::EntityId viewport) {
    const auto *camera = detail::cameraOrNull(viewport);
    return camera != nullptr ? camera->canvasEntity_ : IREntity::kNullEntity;
}

// Destroy the viewport and its canvas and untag its subjects. Deferred, so a
// stage still holding the canvas this frame keeps a live entity.
inline void destroy(IREntity::EntityId viewport) {
    const auto *camera = detail::cameraOrNull(viewport);
    if (camera == nullptr) {
        return;
    }
    setSubject(viewport, IREntity::kNullEntity);
    if (camera->canvasEntity_ != IREntity::kNullEntity) {
        IREntity::destroyEntity(camera->canvasEntity_);
    }
    IREntity::destroyEntity(viewport);
}

} // namespace IRPrefab::Viewport

#endif /* IR_PREFAB_VIEWPORT_H */
