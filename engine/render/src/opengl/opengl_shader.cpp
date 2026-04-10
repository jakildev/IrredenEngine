#include <irreden/ir_profile.hpp>
#include <irreden/ir_utility.hpp>

#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/opengl/opengl_types.hpp>
#include <irreden/render/shader.hpp>

#include <filesystem>
#include <sstream>

namespace IRRender {

namespace detail {

std::string resolveShaderIncludes(
    const std::string &source,
    const std::filesystem::path &baseDir
) {
    std::istringstream stream(source);
    std::ostringstream result;
    std::string line;
    while (std::getline(stream, line)) {
        std::string trimmed = line;
        auto start = trimmed.find_first_not_of(" \t");
        if (start != std::string::npos) {
            trimmed = trimmed.substr(start);
        }
        if (trimmed.rfind("#include \"", 0) == 0) {
            auto closeQuote = trimmed.find('"', 10);
            if (closeQuote != std::string::npos) {
                std::string filename = trimmed.substr(10, closeQuote - 10);
                std::filesystem::path includePath = baseDir / filename;
                result << IRUtility::readFileAsString(includePath.string()) << "\n";
                continue;
            }
        }
        result << line << "\n";
    }
    return result.str();
}

} // namespace detail

class OpenGLShaderPipelineImpl final : public ShaderPipelineImpl {
  public:
    explicit OpenGLShaderPipelineImpl(const std::vector<ShaderStage> &stages)
        : m_handle(ENG_API->glCreateProgram()) {
        std::vector<GLuint> compiledStages;
        compiledStages.reserve(stages.size());

        for (const ShaderStage &stage : stages) {
            const GLuint shader = ENG_API->glCreateShader(toGLShaderType(stage.getType()));
            std::string rawSource = IRUtility::readFileAsString(stage.getFilepath());
            std::filesystem::path shaderDir =
                std::filesystem::path(stage.getFilepath()).parent_path();
            std::string source = detail::resolveShaderIncludes(rawSource, shaderDir);
            const char *sourcePtr = source.c_str();
            ENG_API->glShaderSource(shader, 1, &sourcePtr, nullptr);
            ENG_API->glCompileShader(shader);

            int success = 0;
            ENG_API->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLint infoLogLength = 0;
                ENG_API->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
                std::vector<GLchar> infoLog(infoLogLength);
                ENG_API->glGetShaderInfoLog(shader, infoLogLength, nullptr, infoLog.data());
                IRE_LOG_FATAL("{}", infoLog.data());
                IR_ASSERT(false, "Shader stage compilation failed.");
            }

            compiledStages.push_back(shader);
            ENG_API->glAttachShader(m_handle, shader);
        }

        ENG_API->glLinkProgram(m_handle);

        int success = 0;
        ENG_API->glGetProgramiv(m_handle, GL_LINK_STATUS, &success);
        if (!success) {
            GLint infoLogLength = 0;
            ENG_API->glGetProgramiv(m_handle, GL_INFO_LOG_LENGTH, &infoLogLength);
            std::vector<GLchar> infoLog(infoLogLength);
            ENG_API->glGetProgramInfoLog(m_handle, infoLogLength, nullptr, infoLog.data());
            IRE_LOG_FATAL("{}", infoLog.data());
            IR_ASSERT(false, "Shader program linking failed.");
        }

        for (GLuint shader : compiledStages) {
            ENG_API->glDetachShader(m_handle, shader);
            ENG_API->glDeleteShader(shader);
        }
    }

    ~OpenGLShaderPipelineImpl() override {
        ENG_API->glDeleteProgram(m_handle);
    }

    void use() override {
        ENG_API->glUseProgram(m_handle);
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

  private:
    GLuint m_handle = 0;
};

std::unique_ptr<ShaderPipelineImpl> createShaderPipelineImpl(const std::vector<ShaderStage> &stages) {
    return std::make_unique<OpenGLShaderPipelineImpl>(stages);
}

} // namespace IRRender
