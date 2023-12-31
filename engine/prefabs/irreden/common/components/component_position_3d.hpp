/*
 * Project: Irreden Engine
 * File: component_position_3d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_POSITION_3D_H
#define COMPONENT_POSITION_3D_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

namespace IRComponents {


struct C_Position3D {
    vec3 pos_;
    float tempPackBuffer_;

    C_Position3D(
        vec3 pos
    )
    :   pos_{pos}
    {

    }

    C_Position3D(
        float x,
        float y,
        float z
    )
    :   C_Position3D{vec3(x, y, z)}
    {

    }

    C_Position3D()
    :   C_Position3D{vec3(0, 0, 0)}
    {

    }

    void moveDown(float amount) {
        pos_.z += amount;
    }

    void moveUp(float amount) {
        pos_.z -= amount;
    }

    void moveBackDistance(float distance) {
        pos_.x += distance;
        pos_.y += distance;
        pos_.z += distance * 2.0f;

    }

    // event functions (not really a thing yet)
    void updateChild(C_Position3D& child) {
        child.pos_ = pos_;
    }

};

} // namespace IRComponents

#endif /* COMPONENT_POSITION_3D_H */
