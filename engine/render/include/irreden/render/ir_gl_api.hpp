/*
 * Project: Irreden Engine
 * File: ir_gl_api.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_GL_API_H
#define IR_GL_API_H

#include <irreden/render/gl_wrap/GL.h>

class IrredenGLAPI {
public:
    static IrredenGLAPI* instance();
    inline GL4API* getAPI() { return &m_api; }
private:
    IrredenGLAPI();
    GL4API m_api;
};

#define ENG_API IrredenGLAPI::instance()->getAPI()


#endif /* IR_GL_API_H */
