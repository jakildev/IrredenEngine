// Lighting demo — occluded window-boundary seed (#2330).
//
// Positive-fire fixture for the occlusion-aware boundary seed. Scene =
// the standard `IRLightingEmissive` setup (default voxel-pool / SDF row
// + floor + the emissive light at `kEmissiveLightPos`) with one extra: a
// voxel-pool wall slab positioned so that the `--light-boundary-sweep`
// d070 shot's per-axis-clamped seed cell lands *inside* it.
//
// Without the relocation, the gather still emits that seed and
// `c_propagate_light_volume`'s neighbour occlusion gate traps it — the
// light pops off instead of fading (`BOUNDARY_DISCOUNTED:0.786`, no
// in-window glow). With it, the seed moves to the nearest unoccluded
// cell on the clamped window face and the glow enters the window around
// the wall's -Y end (`BOUNDARY_RELOCATED:0.714`).
//
// Geometry contract (checked at runtime by scripts/light-verify.py's
// per-target expected-state table, not by a comment): the sweep pans the
// camera anchor to `kEmissiveLightPos.x + 70` on the d070 shot, which
// puts the clamp cell at world `(30, 6, -2)`. The slab below spans
// x [29,31], y [5,9], z [-6,4], so it contains that cell; the nearest
// free cell on the clamped (x = -halfExtent) window face is `(30, 4, -2)`
// at distance 2, giving the 0.714 residual. If `kEmissiveLightPos` or the
// sweep distances move, this slab must move with them.

#include "common/lighting_demo_main.hpp"

namespace {

void buildOccludedBoundaryGeometry() {
    using IRMath::Color;
    using IRMath::ivec3;
    using IRMath::vec3;
    using IRMath::vec4;
    namespace LD = IRLightingDemo::detail;

    LD::createGeometry();

    // Voxel-pool (not SDF) so the slab lands in the occlusion grid's voxel
    // bitfield without needing a `C_LightBlocker` opt-in. Sits in the gap
    // between the voxel row (y ~ 0) and the SDF row (y = 12), so it
    // interpenetrates neither.
    LD::createVoxelPoolShape(
        vec3(30.0f, 7.0f, LD::floorTopZ() - 5.0f),
        IRRender::ShapeType::BOX,
        vec4(3.0f, 5.0f, 11.0f, 0.0f),
        Color{170, 60, 60, 255},
        ivec3(1, 2, 5)
    );
}

} // namespace

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_occluded_boundary",
        .addEmissive_ = true,
        .geometryFn_ = buildOccludedBoundaryGeometry,
    }
)
