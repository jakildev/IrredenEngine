#ifndef VAO_H
#define VAO_H

#include <irreden/render/vertex_attributes.hpp>

#include <cstdint>
#include <memory>

namespace IRRender {

const unsigned int kMaxVertexAttributes = 16;

class VertexLayoutImpl {
  public:
    virtual ~VertexLayoutImpl() = default;
    virtual void bind() const = 0;
};

class VertexLayout {
  public:
    VertexLayout(
        std::uint32_t vertexBufferHandle,
        std::uint32_t indexBufferHandle,
        unsigned int numAttributes,
        const VertexArrayAttribute *attributes
    );
    ~VertexLayout();
    VertexLayout(VertexLayout &&other) noexcept;
    VertexLayout &operator=(VertexLayout &&other) noexcept;
    VertexLayout(const VertexLayout &) = delete;
    VertexLayout &operator=(const VertexLayout &) = delete;

    void bind() const;

  private:
    std::unique_ptr<VertexLayoutImpl> m_impl;
};

using VAO = VertexLayout;

} // namespace IRRender

#endif /* VAO_H */
