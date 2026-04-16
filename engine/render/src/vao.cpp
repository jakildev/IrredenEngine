#include <irreden/render/buffer.hpp>
#include <irreden/render/vao.hpp>

namespace IRRender {

std::unique_ptr<VertexLayoutImpl> createVertexLayoutImpl(
    const Buffer *vertexBuffer,
    const Buffer *indexBuffer,
    unsigned int numAttributes,
    const VertexArrayAttribute *attributes
);

VertexLayout::VertexLayout(
    const Buffer *vertexBuffer,
    const Buffer *indexBuffer,
    unsigned int numAttributes,
    const VertexArrayAttribute *attributes
)
    : m_impl(createVertexLayoutImpl(
          vertexBuffer, indexBuffer, numAttributes, attributes
      )) {}

VertexLayout::~VertexLayout() = default;
VertexLayout::VertexLayout(VertexLayout &&other) noexcept = default;
VertexLayout &VertexLayout::operator=(VertexLayout &&other) noexcept = default;

void VertexLayout::bind() const {
    m_impl->bind();
}

} // namespace IRRender
