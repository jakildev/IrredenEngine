// Lighting demo — many SDF-only shapes cast shadows on each other.
//
// Validates SDF-to-SDF analytic shadow casting (see
// `c_compute_sun_shadow.glsl::analyticShapeShadowHit`). No voxel-pool
// entities — pure shape rendering — so every shadow visible here is
// produced by the analytic SDF march, not the occupancy grid.
//
// Layout: a 5×5 grid of mixed primitives (boxes, spheres, cylinders,
// cones, torus) sitting on an SDF floor. A few "tall" pillars at the
// near edge cast long shadows across the rest of the grid so the
// sun-shadow march has obvious caster→receiver pairs at every zoom.

#include "common/lighting_demo_main.hpp"

namespace {

void buildSdfCascadeGeometry() {
    using IRComponents::C_LightBlocker;
    using IRMath::Color;
    using IRMath::vec3;
    using IRMath::vec4;
    namespace LD = IRLightingDemo::detail;

    constexpr float kCellSpacing = 14.0f;
    constexpr int kGridSize = 5;
    constexpr float kFloorCenterZ = 5.0f;

    // Floor — large enough to receive shadows from every shape in the
    // grid plus the tall pillars at the near edge.
    const vec4 floorParams =
        vec4(kGridSize * kCellSpacing + 24.0f, kGridSize * kCellSpacing + 24.0f, 2.0f, 0.0f);
    const float floorTopZ =
        kFloorCenterZ - LD::sdfBottomZOffset(IRRender::ShapeType::BOX, floorParams);
    const vec3 gridCenter =
        vec3((kGridSize - 1) * 0.5f * kCellSpacing, (kGridSize - 1) * 0.5f * kCellSpacing, 0.0f);
    IREntity::EntityId floor = LD::createSdfShape(
        vec3(gridCenter.x, gridCenter.y, kFloorCenterZ),
        IRRender::ShapeType::BOX,
        floorParams,
        Color{160, 160, 170, 255}
    );
    IREntity::setComponent(floor, C_LightBlocker{false, false, 0.0f});

    struct Cell {
        IRRender::ShapeType type_;
        vec4 params_;
        Color color_;
    };
    const Cell cells[] = {
        {IRRender::ShapeType::BOX, vec4(7, 7, 7, 0), Color{100, 200, 220, 255}},
        {IRRender::ShapeType::SPHERE, vec4(4, 4, 4, 0), Color{220, 180, 100, 255}},
        {IRRender::ShapeType::CYLINDER, vec4(3, 3, 7, 0), Color{180, 220, 120, 255}},
        {IRRender::ShapeType::CONE, vec4(4, 4, 8, 0), Color{220, 140, 100, 255}},
        {IRRender::ShapeType::TORUS, vec4(4, 2, 0, 0), Color{180, 130, 220, 255}},
    };

    for (int y = 0; y < kGridSize; ++y) {
        for (int x = 0; x < kGridSize; ++x) {
            const Cell &cell = cells[(x + y) % 5];
            const float zOffset = LD::sdfBottomZOffset(cell.type_, cell.params_);
            LD::createSdfShape(
                vec3(
                    static_cast<float>(x) * kCellSpacing,
                    static_cast<float>(y) * kCellSpacing,
                    floorTopZ - zOffset
                ),
                cell.type_,
                cell.params_,
                cell.color_
            );
        }
    }

    // Tall pillars on the -X edge (sun-side under the default direction)
    // cast long shadows across the grid; this is what makes the demo
    // useful for visualising SDF-to-SDF shadow propagation.
    const vec4 pillarParams = vec4(4, 4, 22, 0);
    const float pillarZOffset = LD::sdfBottomZOffset(IRRender::ShapeType::BOX, pillarParams);
    for (int y = 0; y < kGridSize; ++y) {
        if (y % 2 != 0)
            continue;
        LD::createSdfShape(
            vec3(-kCellSpacing, static_cast<float>(y) * kCellSpacing, floorTopZ - pillarZOffset),
            IRRender::ShapeType::BOX,
            pillarParams,
            Color{200, 90, 110, 255}
        );
    }
}

} // namespace

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_sdf_cascade",
        .geometryFn_ = buildSdfCascadeGeometry,
    }
)
