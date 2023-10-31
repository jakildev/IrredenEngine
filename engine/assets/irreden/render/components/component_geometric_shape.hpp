/*
 * Project: Irreden Engine
 * File: component_geometric_shape.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_GEOMETRIC_SHAPE_H
#define COMPONENT_GEOMETRIC_SHAPE_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;
using namespace IRConstants;

// Unused at the moment

namespace IRComponents {

    template<Shape3D shape>
    struct C_GeometricShape;

    template<>
    struct C_GeometricShape<Shape3D::RECTANGULAR_PRISM> {
        uvec3 size_;

        C_GeometricShape(
            uvec3 size
        )
        :   size_(size)
        {

        }

        // Default
        C_GeometricShape()
        :   size_(uvec3{0, 0, 0})
        {

        }

    };

    template<>
    struct C_GeometricShape<Shape3D::SPHERE> {
        float radius_;

        C_GeometricShape(
            float radius
        )
        :   radius_(radius)
        {

        }

        // Default
        C_GeometricShape()
        :   radius_(0.0f)
        {

        }

    };

    // template<>


} // namespace IRComponents




#endif /* COMPONENT_GEOMETRIC_SHAPE_H */
