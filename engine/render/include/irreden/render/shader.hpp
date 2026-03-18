#ifndef SHADER_H
#define SHADER_H

#include <irreden/render/ir_render_enums.hpp>

#include <memory>
#include <string>
#include <vector>

namespace IRRender {

class ShaderStage {
  public:
    explicit ShaderStage(const char *filepath, ShaderType type);

    const std::string &getFilepath() const;
    ShaderType getType() const;

  private:
    std::string m_filepath;
    ShaderType m_type;
};

class ShaderPipelineImpl {
  public:
    virtual ~ShaderPipelineImpl() = default;
    virtual void use() = 0;
    virtual std::uint32_t getHandle() const = 0;
};

class ShaderPipeline {
  public:
    explicit ShaderPipeline(const std::vector<ShaderStage> &stages);
    ~ShaderPipeline();
    ShaderPipeline(ShaderPipeline &&other) noexcept;
    ShaderPipeline &operator=(ShaderPipeline &&other) noexcept;
    ShaderPipeline(const ShaderPipeline &) = delete;
    ShaderPipeline &operator=(const ShaderPipeline &) = delete;

    void use();
    std::uint32_t getHandle() const;

  private:
    std::unique_ptr<ShaderPipelineImpl> m_impl;
};

using ShaderProgram = ShaderPipeline;

} // namespace IRRender

#endif /* SHADER_H */
