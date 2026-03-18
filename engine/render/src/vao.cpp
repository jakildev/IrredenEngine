#include <irreden/render/vao.hpp>

namespace IRRender {

std::unique_ptr<VertexLayoutImpl> createVertexLayoutImpl(
    std::uint32_t vertexBufferHandle,
    std::uint32_t indexBufferHandle,
    unsigned int numAttributes,
    const VertexArrayAttribute *attributes
);

VertexLayout::VertexLayout(
    std::uint32_t vertexBufferHandle,
    std::uint32_t indexBufferHandle,
    unsigned int numAttributes,
    const VertexArrayAttribute *attributes
)
    : m_impl(createVertexLayoutImpl(
          vertexBufferHandle, indexBufferHandle, numAttributes, attributes
      )) {}

VertexLayout::~VertexLayout() = default;
VertexLayout::VertexLayout(VertexLayout &&other) noexcept = default;
VertexLayout &VertexLayout::operator=(VertexLayout &&other) noexcept = default;

void VertexLayout::bind() const {
    m_impl->bind();
}

} // namespace IRRender
