/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\shader.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <string>
#include <vector>

namespace IRRendering {

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

} // namespace IRRendering

#endif /* SHADER_H */
