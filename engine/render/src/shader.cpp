#include <irreden/render/shader.hpp>

#include <utility>

namespace IRRender {

std::unique_ptr<ShaderPipelineImpl> createShaderPipelineImpl(const std::vector<ShaderStage> &stages);

ShaderStage::ShaderStage(const char *filepath, ShaderType type)
    : m_filepath(filepath)
    , m_type(type) {}

const std::string &ShaderStage::getFilepath() const {
    return m_filepath;
}

ShaderType ShaderStage::getType() const {
    return m_type;
}

ShaderPipeline::ShaderPipeline(const std::vector<ShaderStage> &stages)
    : m_impl(createShaderPipelineImpl(stages)) {}

ShaderPipeline::~ShaderPipeline() = default;
ShaderPipeline::ShaderPipeline(ShaderPipeline &&other) noexcept = default;
ShaderPipeline &ShaderPipeline::operator=(ShaderPipeline &&other) noexcept = default;

void ShaderPipeline::use() {
    m_impl->use();
}

std::uint32_t ShaderPipeline::getHandle() const {
    return m_impl->getHandle();
}

} // namespace IRRender