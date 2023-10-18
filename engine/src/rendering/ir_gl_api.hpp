/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\ir_gl_api.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IRREDEN_GLAPI_H
#define IRREDEN_GLAPI_H

#include "../gl_wrap/GL.h"

class IrredenGLAPI {
public:
    static IrredenGLAPI* instance();
    inline GL4API* getAPI() { return &m_api; }
private:
    IrredenGLAPI();
    GL4API m_api;
};

#define ENG_API IrredenGLAPI::instance()->getAPI()


#endif