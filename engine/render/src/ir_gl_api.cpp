/*
 * Project: Irreden Engine
 * File: ir_gl_api.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/render/ir_gl_api.hpp>
#include <GLFW/glfw3.h>
#include <memory>

IrredenGLAPI::IrredenGLAPI() {
    GetAPI4(&m_api, [](const char* func) -> void* { return (void *)glfwGetProcAddress(func); });
	InjectAPITracer4(&m_api);
}

// singleton
IrredenGLAPI* IrredenGLAPI::instance() {
    /* cant use make_unique here because of private constructor */
    static std::unique_ptr<IrredenGLAPI> instance = std::unique_ptr<IrredenGLAPI>(new IrredenGLAPI{});
    return instance.get();
}