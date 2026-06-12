#ifndef ENTITY_VOXEL_POOL_CANVAS_H
#define ENTITY_VOXEL_POOL_CANVAS_H

#include <irreden/ir_entity.hpp>

#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_size_triangles.hpp>

using namespace IRComponents;

namespace IREntity {

template <> struct Prefab<PrefabTypes::kVoxelPoolCanvas> {
    static EntityId create(
        std::string canvasName,
        ivec3 voxelPoolSize,
        ivec2 triangleCanvasSize,
        EntityId framebuffer = kNullEntity
    ) {
        EntityId canvas = createEntity(
            C_VoxelPool{voxelPoolSize},
            C_SizeTriangles{triangleCanvasSize},
            C_TriangleCanvasTextures{triangleCanvasSize},
            C_CanvasLocalRotation{},
            // Per-axis trixel canvases for smooth camera Z-yaw (#1308). Inert
            // until allocated lazily on the main world canvas by
            // syncAllocationToCameraYaw() while the camera sits at a non-cardinal
            // residual yaw. Default-constructed = (0,0) size + null handles, so a
            // static / cardinal canvas pays only the component slot, no GPU
            // memory. (Detached SO(3) uses the re-voxelize buffer below, not this.)
            C_PerAxisTrixelCanvases{},
            // Resident GPU locals buffer for the detached re-voxelize scatter
            // (#1556). Inert until allocated lazily by
            // syncResidentBuffers() for a DETACHED_REVOXELIZE canvas; a static /
            // non-re-voxelize canvas pays only the component slot, no GPU memory.
            C_DetachedRevoxelizeBuffer{},
            C_Name{canvasName}
        );
        if (framebuffer == kNullEntity) {
            setParent(canvas, getEntity("mainFramebuffer"));
        } else {
            setParent(canvas, framebuffer);
        }
        IRE_LOG_INFO(
            "Created voxel pool canvas {} with framebuffer parent {}, size {},{}",
            canvas,
            framebuffer,
            triangleCanvasSize.x,
            triangleCanvasSize.y
        );
        return canvas;
    }
};
} // namespace IREntity

#endif /* ENTITY_VOXEL_POOL_CANVAS_H */
