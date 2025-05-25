/*
 * Project: Irreden Engine
 * File: shader.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SHADER_H
#define SHADER_H

#include <irreden/render/opengl/opengl_types.hpp>

#include <string>
#include <vector>

namespace IRRender {

    class ShaderStage {
    public:
        explicit ShaderStage(const char* filepath, GLenum type);
        ~ShaderStage();

        GLuint getHandle() const;
    private:
        GLuint m_handle;
        GLuint m_type;

        std::string readFileAsString(const char* filepath);
        void compileShader(std::string source);
        void checkSuccess();
    };

    class ShaderProgram {
    public:

        ShaderProgram(const std::vector<GLuint>& stages);
        ~ShaderProgram();

        void use();
        GLuint getHandle() const;
    private:
        GLuint m_handle;

        void checkSuccess();
    };

} // namespace IRRender

#endif /* SHADER_H */
