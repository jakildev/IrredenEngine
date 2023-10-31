/*
 * Project: Irreden Engine
 * File: shapes_2d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SHAPES_2D_H
#define SHAPES_2D_H

namespace IRShapes2D {

    const float k2DQuadTextured[] = {
        // xy        // uv
        -0.5f,  0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.0f, 0.0f,
         0.5f, -0.5f,  1.0f, 0.0f,

        -0.5f,  0.5f,  0.0f, 1.0f,
         0.5f, -0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  1.0f, 1.0f,
    };

    const float kQuadVertices[] {
        -0.5f, -0.5f,
         0.5f, -0.5f,
        -0.5f,  0.5f,
         0.5f,  0.5f
    };

    const unsigned short kQuadIndices[] {
        0, 1, 2,
        1, 2, 3
    };

    const int kQuadIndicesLength =
        sizeof(kQuadIndices) /
        sizeof(kQuadIndices[0])
    ;

} // namespace IRShapes2D

#endif /* SHAPES_2D_H */
