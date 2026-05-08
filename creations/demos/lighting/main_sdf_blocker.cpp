// Lighting demo — SDF blocker casts a point-light shadow.
//
// Acceptance scene for T-117 / #364: a `C_ShapeDescriptor` (BOX) tagged
// with `C_LightBlocker(blocksLOS_=true)` placed between a POINT light
// and the canvas floor produces a visible shadow on the surface.
// Without the fix, the point-light wavefront passes straight through
// the SDF and the floor lights up symmetrically around the blocker.
//
// Scene = the standard `IRLightingPoint` setup (default voxel-pool /
// SDF row + floor + point light at the canonical position) with two
// extras:
//   • A tall SDF wall slab placed directly between the point light and
//     the visible shape row, opted into LOS blocking via
//     `C_LightBlocker(blocksLOS_=true)`. The new GPU LOS path occludes
//     the point light's volume contribution, producing a visible
//     warm/cold transition on the floor + shapes behind the wall.
//   • Sun ambient is kept on so the rest of the scene reads, but
//     `castsShadow_=false` on the blocker keeps the sun-shadow bake
//     out of the picture so the visible occlusion is unambiguously the
//     SDF-LOS contribution.

#include "common/lighting_demo_main.hpp"

namespace {

void buildSdfBlockerGeometry() {
    using IRComponents::C_LightBlocker;
    using IRMath::Color;
    using IRMath::vec3;
    using IRMath::vec4;
    namespace LD = IRLightingDemo::detail;

    LD::createGeometry();

    // The default floor sits at center-Z=5 with thickness 2 and the
    // standard scene's point light is added at (34, -7, -1) with
    // intensity 2.2. A tall wall placed at x≈22 in the +Y row blocks
    // the warm point light from illuminating the -X half of the row.
    constexpr float kFloorTopZ = 5.0f - 1.0f;
    const vec4 wallParams = vec4(2.0f, 14.0f, 18.0f, 0.0f);
    const float wallHalfZ = LD::sdfBottomZOffset(IRRender::ShapeType::BOX, wallParams);
    IREntity::EntityId wall = LD::createSdfShape(
        vec3(22.0f, -2.0f, kFloorTopZ - wallHalfZ),
        IRRender::ShapeType::BOX,
        wallParams,
        Color{210, 100, 110, 255}
    );
    IREntity::setComponent(wall, C_LightBlocker{true, false, 1.0f});
}

} // namespace

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_sdf_blocker",
        .addPoint_ = true,
        .geometryFn_ = buildSdfBlockerGeometry,
    }
)
