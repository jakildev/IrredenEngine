#ifndef COMPONENT_GIZMO_HANDLE_H
#define COMPONENT_GIZMO_HANDLE_H

#include <cstdint>

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/math/color.hpp>

namespace IRComponents {

/// Per-handle metadata for editor gizmo entities. The visible geometry is
/// supplied by a sibling `C_ShapeDescriptor` on the same entity; this
/// component is a marker that tags which gizmo + which axis + the runtime
/// hover / drag state mutated by the input-pipeline gizmo systems.
///
/// Phase 2 (T-164) sizing fields:
///
/// - `referenceParams_` and `referenceLocalPos_` capture the
///   construction-time values written by the Phase 1 builders. The
///   `GIZMO_SCREEN_SPACE_SIZE` UPDATE system reads these as the
///   unscaled baseline and writes scaled copies back to
///   `C_ShapeDescriptor::params_` (and the sibling `C_Position3D::pos_`
///   when `isAnchor_` is false) each tick — so multiple frames of
///   scaling don't compound.
/// - `isAnchor_` distinguishes single-entity gizmo markers (joint, IK)
///   — whose own `C_Position3D` IS the world-space anchor the caller
///   wrote — from child handles inside a group (translate arrows,
///   rotate rings, scale sticks/center) — whose `C_Position3D` is a
///   local offset relative to a group-root parent. Phase 2 scales
///   `params_` for both forms, but only scales `pos_` for child
///   handles, so the anchor's editor-placed world position isn't
///   clobbered.
///
/// Phase 3 (T-165) interaction fields:
///
/// - `hover_` is set each frame by `GIZMO_HOVER` from the GPU entity-id
///   readback (`IRRender::getEntityIdAtMouseTrixel`). At most one
///   handle per frame is flagged.
/// - `baseColor_` is the unmodulated color of the handle (captured by
///   the builder when the entity is spawned). `GIZMO_HOVER` writes
///   this back onto `C_ShapeDescriptor::color_` when the handle is not
///   hovered, and writes a brightened version when it is. Storing the
///   base on the handle keeps the round-trip lossless across frames.
/// - `anchorEntity_` is the entity whose `C_Position3D` `GIZMO_DRAG`
///   mutates while this handle is being dragged. By convention it
///   points at the gizmo *group* entity (the parent that owns the
///   handle bundle), so all axes of a given gizmo move the same
///   anchor together. `kNullEntity` disables drag for the handle
///   (still hoverable).

enum class GizmoKind : std::uint8_t {
    TRANSLATE_ARROW,   ///< Cylinder shaft + cone head along an axis.
    ROTATE_RING,       ///< Torus around an axis.
    SCALE_STICK,       ///< Cylinder stick + box cap along an axis.
    SCALE_CENTER,      ///< Center box for uniform scale.
    JOINT_MARKER,      ///< Sphere at a skeletal joint.
    BIND_POINT_MARKER, ///< Small cross at a bind-point.
    IK_MARKER,         ///< Cone (Phase 1) / tetrahedron (Phase 2) at an IK target.
};

enum class GizmoAxis : std::uint8_t {
    NONE,
    X,
    Y,
    Z,
};

struct C_GizmoHandle {
    GizmoKind kind_ = GizmoKind::JOINT_MARKER;
    GizmoAxis axis_ = GizmoAxis::NONE;
    bool hover_ = false;
    bool isAnchor_ = true;
    IRMath::vec4 referenceParams_ = IRMath::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    IRMath::vec3 referenceLocalPos_ = IRMath::vec3(0.0f);
    IRMath::Color baseColor_ = IRMath::Color{220, 220, 220, 255};
    IREntity::EntityId anchorEntity_ = IREntity::kNullEntity;

    C_GizmoHandle() = default;

    C_GizmoHandle(GizmoKind kind, GizmoAxis axis)
        : kind_{kind}
        , axis_{axis} {}

    C_GizmoHandle(
        GizmoKind kind,
        GizmoAxis axis,
        IRMath::vec4 referenceParams,
        IRMath::vec3 referenceLocalPos,
        bool isAnchor,
        IRMath::Color baseColor,
        IREntity::EntityId anchorEntity
    )
        : kind_{kind}
        , axis_{axis}
        , isAnchor_{isAnchor}
        , referenceParams_{referenceParams}
        , referenceLocalPos_{referenceLocalPos}
        , baseColor_{baseColor}
        , anchorEntity_{anchorEntity} {}
};

} // namespace IRComponents

#endif /* COMPONENT_GIZMO_HANDLE_H */
