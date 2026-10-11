#include "ir_sdf_sun_ray.metal"

inline bool shapeUsesSunRay(ShapeDescriptor shape, int2 renderOptions,
                     int smoothYaw, int latticeShapes) {
    bool analytic = (renderOptions.x != 0 && renderOptions.y > 1) ||
                    (smoothYaw != 0 && latticeShapes == 0);
    bool supported = shape.shapeType == SHAPE_SPHERE ||
                     shape.shapeType == SHAPE_CYLINDER ||
                     shape.shapeType == SHAPE_CONE || shape.shapeType == SHAPE_TORUS;
    return analytic && supported && (shape.flags & 1u) == 0u;
}
