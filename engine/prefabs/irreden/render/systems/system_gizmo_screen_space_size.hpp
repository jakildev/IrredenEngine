#ifndef SYSTEM_GIZMO_SCREEN_SPACE_SIZE_H
#define SYSTEM_GIZMO_SCREEN_SPACE_SIZE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/transform_modifier_fields.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace IRSystem {

// UPDATE-phase system that keeps editor gizmos at a constant pixel size
// regardless of camera zoom (T-164 Phase 2). Each tick:
//
//   scale = pixelSize_ / zoomSnapshot_
//   shape.params_ = handle.referenceParams_ * scale
//   if (!handle.isAnchor_) {
//       localTransform.translation_ = handle.referenceLocalPos_ * scale
//   }
//
// Writes are derived from the per-handle `referenceParams_` /
// `referenceLocalPos_` (set once by the Phase 1 builders), so successive
// frames don't compound — the same baseline maps to the same on-screen
// pixel size across the full zoom range.
//
// `isAnchor_` distinguishes single-entity gizmo markers (joint, IK) whose
// own `C_LocalTransform` IS the world-space anchor (the editor writes it
// post-construction) from child handles parented under a group root.
// For anchors we scale only `params_`; touching `translation_` would
// clobber the editor's placement.
//
// `pixelSize_` is a per-system knob the caller can tune. `1.0f` means
// the handle renders at its Phase 1 world-space size when zoom == 1.0;
// raising it makes every handle proportionally bigger in pixels.
//
// Must run BEFORE `PROPAGATE_TRANSFORM` in the UPDATE pipeline so the
// rescaled local translations propagate into `C_WorldTransform` the same
// frame; `SHAPES_TO_TRIXEL` reads `C_WorldTransform` in RENDER.
//
// Z-yaw safety: the rescaled `C_ShapeDescriptor::params_` is consumed
// by `system_shapes_to_trixel` AFTER the rotate snapshot, so the
// shapes shader sees the new scaled extent at the same cardinal yaw
// the rest of the frame uses — no special yaw handling required here.
//
// View clearance: a handle with `C_GizmoHandle::viewClearance_ > 0` is
// also displaced toward the camera, in world space, by at least
//
//   viewClearance_ + (the farthest extent, from the anchor, of any cleared
//                     handle that shares its anchor)
//
// along the direction the yawed iso projection is constant in, at the
// camera's continuous yaw. The handle keeps every pixel it covers with a
// clearance of 0 and sorts nearer than the whole sphere of that radius
// around its anchor.
//
// - The length is rounded up to a whole number of steps of the view-depth
//   diagonal. At a cardinal yaw that moves every world component by the same
//   whole number of units, so the handle keeps its alignment to the voxel
//   lattice. A thin handle draws only where its surface meets the lattice's
//   sample points; a fractional shift would redraw it differently.
// - The extent term is one length for the whole gizmo, so handles built with
//   one clearance move as a unit and keep their depth order among themselves.
// - The displacement is a `TRANSFORM_TRANSLATION` modifier, which
//   `PROPAGATE_TRANSFORM` adds after the parent chain, so the anchor's
//   rotation and scale do not turn or stretch it.
// - Camera pitch is outside the contract (GRID rendering is Z-yaw only).
//
// The yaw is read here, in UPDATE. A system that turns the camera later in
// the frame (a RENDER-phase rotate) is followed one frame late.
template <> struct System<GIZMO_SCREEN_SPACE_SIZE> {
    float pixelSize_ = 1.0f;
    float zoomSnapshot_ = 1.0f;
    float scaleSnapshot_ = 1.0f;

    void beginTick() {
        beginFrame(IRRender::getCameraZoom().x);
    }

    // Takes the zoom so a caller with no render manager can drive a frame.
    void beginFrame(float zoom) {
        // Camera zoom is set by IRRender; the editor's mouse-wheel handler
        // is the only mutator. Guard against pathological values so a
        // misconfigured creation never blows the gizmo extents up to
        // infinity (would trip the cull bounds and crash render).
        zoomSnapshot_ = IRMath::max(zoom, 1e-3f);
        scaleSnapshot_ = pixelSize_ / zoomSnapshot_;
        placeClearedHandles();
    }

    void tick(
        IRComponents::C_GizmoHandle &handle,
        IRComponents::C_ShapeDescriptor &shape,
        IRComponents::C_LocalTransform &localTransform
    ) {
        shape.params_ = handle.referenceParams_ * scaleSnapshot_;
        if (!handle.isAnchor_) {
            localTransform.translation_ = handle.referenceLocalPos_ * scaleSnapshot_;
        }
    }

    static SystemId create() {
        return registerSystem<
            GIZMO_SCREEN_SPACE_SIZE,
            IRComponents::C_GizmoHandle,
            IRComponents::C_ShapeDescriptor,
            IRComponents::C_LocalTransform>("GizmoScreenSpaceSize");
    }

  private:
    void placeClearedHandles() const {
        const auto nodes = IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<
                IRComponents::C_GizmoHandle,
                IRComponents::C_ShapeDescriptor,
                IRComponents::C_Modifiers>()
        );
        if (nodes.empty()) {
            return;
        }

        // One step of the view-depth diagonal: (1,1,1) in the view frame. The
        // cardinal form is exact, which the lattice alignment depends on.
        const float yaw = IRPrefab::Camera::getYaw();
        const auto [rasterYaw, residualYaw] = IRPrefab::Camera::computeYawSplit(yaw);
        const IRMath::vec3 depthStep =
            residualYaw == 0.0f ? IRMath::rotateCardinalZInv(
                                      IRMath::vec3(1.0f),
                                      IRMath::rasterYawCardinalIndex(rasterYaw)
                                  )
                                : IRMath::isoPixelToPos3DYawed(IRMath::vec2(0.0f), 3.0f, yaw);
        const float stepLength = IRMath::length(depthStep);
        const auto translationField = IRPrefab::TransformModifier::translationField();

        for (IREntity::ArchetypeNode *node : nodes) {
            // A child handle's offset from its anchor is scaled by the anchor's
            // world scale; its shape is not.
            float anchorScale = 1.0f;
            const IREntity::EntityId anchor = IREntity::getParentEntityFromArchetype(node->type_);
            if (anchor != IREntity::kNullEntity) {
                const auto anchorWorld =
                    IREntity::getComponentOptional<IRComponents::C_WorldTransform>(anchor);
                if (const auto *world = anchorWorld.value_or(nullptr)) {
                    const IRMath::vec3 scale = IRMath::abs(world->scale_);
                    anchorScale = IRMath::max(scale.x, IRMath::max(scale.y, scale.z));
                }
            }

            // Handles that share an anchor share a node.
            auto &handles = IREntity::getComponentData<IRComponents::C_GizmoHandle>(node);
            auto &shapes = IREntity::getComponentData<IRComponents::C_ShapeDescriptor>(node);
            auto &modifiers = IREntity::getComponentData<IRComponents::C_Modifiers>(node);
            float gizmoExtent = 0.0f;
            for (int i = 0; i < node->length_; ++i) {
                const IRComponents::C_GizmoHandle &handle = handles[i];
                if (handle.viewClearance_ <= 0.0f) {
                    continue;
                }
                const float reach =
                    handle.isAnchor_
                        ? 0.0f
                        : anchorScale * IRMath::length(handle.referenceLocalPos_) * scaleSnapshot_;
                gizmoExtent = IRMath::max(
                    gizmoExtent,
                    reach + IRMath::SDF::boundingRadius(
                                shapes[i].shapeType_,
                                handle.referenceParams_ * scaleSnapshot_
                            )
                );
            }
            for (int i = 0; i < node->length_; ++i) {
                const float clearance = handles[i].viewClearance_;
                const float steps =
                    clearance > 0.0f ? IRMath::ceil((clearance + gizmoExtent) / stepLength) : 0.0f;
                IRPrefab::Modifier::upsertBySourceInPlace(
                    modifiers[i],
                    translationField,
                    IRComponents::TransformKind::ADD,
                    depthStep * -steps,
                    node->entities_[i]
                );
            }
        }
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GIZMO_SCREEN_SPACE_SIZE_H */
