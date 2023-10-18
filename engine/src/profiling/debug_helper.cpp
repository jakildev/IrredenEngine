/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\profiling\debug_helper.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <glad/glad.h>
#include <iostream>
#include "../profiling/debug_helper.hpp"
#include "../profiling/logger_spd.hpp"
#include "../rendering/ir_gl_api.hpp"

namespace IRDebugging {

    void printSystemInfo() {
        int intAttr;
        ENG_API->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &intAttr);
        ENG_LOG_INFO("Maximum nr of vertex attributes supported: {}", intAttr);
        ENG_API->glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &intAttr);
        ENG_LOG_INFO("Max 3d texture size: {}", intAttr);
        ENG_API->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &intAttr);
        ENG_LOG_INFO("Max uniform block size: {}", intAttr);

    }

} // namespace IRDebugging
