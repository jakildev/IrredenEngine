/*
 * Project: Irreden Engine
 * File: shader.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */


#include <irreden/ir_profile.hpp>

#include <irreden/render/shader.hpp>
#include <irreden/render/ir_gl_api.hpp>

#include <fstream>
#include <iostream>
#include <sstream>

namespace IRRender {

    ShaderStage::ShaderStage(const char* filepath, GLenum type)
    :   m_handle(ENG_API->glCreateShader(type))
    ,   m_type(type)
    {
        compileShader(
            readFileAsString(filepath)
        );
        checkSuccess();
    }

    ShaderStage::~ShaderStage() {
        IRProfile::engLogInfo("Deleting shader stage: {}", m_handle);
        ENG_API->glDeleteShader(m_handle);
    }


    GLuint ShaderStage::getHandle() const {
        return m_handle;
    }

    // TODO: Move to a file utility class
    std::string ShaderStage::readFileAsString(const char* filepath) {
        std::string shaderCode;
        std::ifstream shaderFile;
        shaderFile.exceptions(
            std::ifstream::failbit |
            std::ifstream::badbit
        );
        try {
            shaderFile.open(filepath);
            std::stringstream shaderStream;
            shaderStream << shaderFile.rdbuf();
            shaderFile.close();
            shaderCode = shaderStream.str();
        }
        catch(std::ifstream::failure e) {
            IRProfile::engLogFatal(
                "std::ifstream::failure exception reading file: {}, {}",
                filepath,
                e.what()
            );
            IR_ASSERT(false, "Error while reading shader file.");
        }
        return shaderCode;
    }

    void ShaderStage::compileShader(std::string shaderCode) {
        const char* shaderCodeCStr = shaderCode.c_str();
        ENG_API->glShaderSource(m_handle, 1, &shaderCodeCStr, NULL);
        ENG_API->glCompileShader(m_handle);
    }

    void ShaderStage::checkSuccess() {
        int success;
        ENG_API->glGetShaderiv(m_handle, GL_COMPILE_STATUS, &success);
        if(!success)
        {
            GLint infoLogLength = 0;
            ENG_API->glGetShaderiv(
                m_handle,
                GL_INFO_LOG_LENGTH,
                &infoLogLength
            );
            std::vector <GLchar> infoLog(infoLogLength);
            ENG_API->glGetShaderInfoLog(
                m_handle,
                infoLogLength,
                NULL,
                infoLog.data()
            );
            IRProfile::engLogFatal(infoLog.data());
            IR_ASSERT(false, "Shader stage compilation failed.");
        }
    }


    ShaderProgram::ShaderProgram(
        const std::vector<GLuint>& stages
    )
    :   m_handle(ENG_API->glCreateProgram())
    {
        for(auto stage : stages) {
            ENG_API->glAttachShader(m_handle, stage);
        }
        ENG_API->glLinkProgram(m_handle);
        checkSuccess();
        IRProfile::engLogInfo("Created shader program: {}", m_handle);
    }

    ShaderProgram::~ShaderProgram() {
        IRProfile::engLogInfo("Deleting shader program: {}", m_handle);
        ENG_API->glDeleteProgram(m_handle);
    }

    void ShaderProgram::use() {
        ENG_API->glUseProgram(m_handle);
    }

    GLuint ShaderProgram::getHandle() const {
        return m_handle;
    }

    void ShaderProgram::checkSuccess() {
        int success;
        ENG_API->glGetProgramiv(m_handle, GL_LINK_STATUS, &success);
        if(!success)
        {
            GLint infoLogLength = 0;
            ENG_API->glGetProgramiv(
                m_handle,
                GL_INFO_LOG_LENGTH,
                &infoLogLength
            );
            std::vector <GLchar> infoLog(infoLogLength);
            ENG_API->glGetProgramInfoLog(
                m_handle,
                512,
                NULL,
                infoLog.data()
            );
            IRProfile::engLogFatal(infoLog.data());
            IR_ASSERT(false, "Shader program linking failed.");
        }
    }

} // namespace IRRender