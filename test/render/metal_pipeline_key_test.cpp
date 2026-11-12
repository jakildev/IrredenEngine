// The Metal render-pipeline cache is keyed by value: attachment formats, the
// blend setting, and the contents of the vertex layout. Layouts with equal
// contents share one pipeline state, and a layout is never identified by the
// address of its MTL::VertexDescriptor, which a later layout can reuse.
//
// The key cases are pure value tests and need no device. The cache cases draw
// through the real ShaderProgram / VertexLayout / Framebuffer path on a
// headless Metal device and read the program's cache size.

#include <gtest/gtest.h>

#if defined(IR_GRAPHICS_METAL)

#include <irreden/render/buffer.hpp>
#include <irreden/render/framebuffer.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shapes_2d.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/vao.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace {

using IRRender::MetalRenderPipelineKey;
using IRRender::MetalRenderPipelineKeyHash;
using IRRender::MetalVertexAttributeSignature;
using IRRender::MetalVertexLayoutSignature;

static_assert(std::is_trivially_copyable_v<MetalRenderPipelineKey>);
static_assert(std::is_trivially_copyable_v<MetalVertexLayoutSignature>);

// A float2 position followed by a float4 colour, packed in buffer 0.
MetalRenderPipelineKey baseKey() {
    MetalRenderPipelineKey key{};
    key.colorPixelFormat_ = MTL::PixelFormatRGBA8Unorm;
    key.depthPixelFormat_ = MTL::PixelFormatDepth32Float;
    key.entityIdPixelFormat_ = MTL::PixelFormatInvalid;
    key.blendingEnabled_ = false;
    key.vertexLayout_.attributeCount_ = 2;
    key.vertexLayout_.attributes_[0] = MetalVertexAttributeSignature{
        .format_ = MTL::VertexFormatFloat2,
        .offset_ = 0,
        .bufferIndex_ = 0,
    };
    key.vertexLayout_.attributes_[1] = MetalVertexAttributeSignature{
        .format_ = MTL::VertexFormatFloat4,
        .offset_ = 8,
        .bufferIndex_ = 0,
    };
    key.vertexLayout_.stride_ = 24;
    return key;
}

TEST(MetalPipelineKey, EqualContentsAreEqualKeysWithEqualHashes) {
    const MetalRenderPipelineKey first = baseKey();
    const MetalRenderPipelineKey second = baseKey();
    EXPECT_EQ(first, second);
    EXPECT_EQ(MetalRenderPipelineKeyHash{}(first), MetalRenderPipelineKeyHash{}(second));
}

TEST(MetalPipelineKey, EachFieldDistinguishesTheKey) {
    struct Variant {
        const char *name_;
        MetalRenderPipelineKey key_;
    };
    std::vector<Variant> variants;
    variants.push_back({"base", baseKey()});

    auto vary = [&](const char *name, auto &&mutate) {
        MetalRenderPipelineKey key = baseKey();
        mutate(key);
        variants.push_back({name, key});
    };
    vary("attribute format", [](MetalRenderPipelineKey &key) {
        key.vertexLayout_.attributes_[1].format_ = MTL::VertexFormatFloat3;
    });
    vary("attribute offset", [](MetalRenderPipelineKey &key) {
        key.vertexLayout_.attributes_[1].offset_ = 16;
    });
    vary("attribute buffer index", [](MetalRenderPipelineKey &key) {
        key.vertexLayout_.attributes_[1].bufferIndex_ = 1;
    });
    vary("stride", [](MetalRenderPipelineKey &key) { key.vertexLayout_.stride_ = 32; });
    vary("attribute count", [](MetalRenderPipelineKey &key) {
        key.vertexLayout_.attributeCount_ = 1;
        key.vertexLayout_.attributes_[1] = MetalVertexAttributeSignature{};
    });
    vary("colour format", [](MetalRenderPipelineKey &key) {
        key.colorPixelFormat_ = MTL::PixelFormatBGRA8Unorm;
    });
    vary("depth format", [](MetalRenderPipelineKey &key) {
        key.depthPixelFormat_ = MTL::PixelFormatInvalid;
    });
    vary("entity-id format", [](MetalRenderPipelineKey &key) {
        key.entityIdPixelFormat_ = MTL::PixelFormatRG32Uint;
    });
    vary("blending", [](MetalRenderPipelineKey &key) { key.blendingEnabled_ = true; });

    for (std::size_t i = 1; i < variants.size(); ++i) {
        EXPECT_NE(variants[i].key_, variants[0].key_) << variants[i].name_;
    }
    std::unordered_set<MetalRenderPipelineKey, MetalRenderPipelineKeyHash> distinct;
    for (const Variant &variant : variants) {
        distinct.insert(variant.key_);
    }
    EXPECT_EQ(distinct.size(), variants.size());
}

class MetalPipelineKeyCacheTest : public ::testing::Test {
  protected:
    static constexpr int kTargetDim = 4;

    void SetUp() override {
        m_device = IRRender::bootstrapHeadlessRenderDevice();
        if (m_device == nullptr) {
            GTEST_SKIP() << "No Metal device available — headless host without a GPU.";
        }
        const std::string vertPath =
            std::string(IR_TEST_RENDER_SHADER_DIR) + "/v_framebuffer_to_screen.glsl";
        const std::string fragPath =
            std::string(IR_TEST_RENDER_SHADER_DIR) + "/f_framebuffer_to_screen.glsl";
        m_program = std::make_unique<IRRender::ShaderProgram>(std::vector{
            IRRender::ShaderStage{vertPath.c_str(), IRRender::ShaderType::VERTEX},
            IRRender::ShaderStage{fragPath.c_str(), IRRender::ShaderType::FRAGMENT}
        });
        m_target = std::make_unique<IRRender::Framebuffer>(
            IRMath::ivec2(kTargetDim),
            IRMath::ivec2(0),
            IRRender::TextureFormat::RGBA8,
            IRRender::TextureFormat::DEPTH24_STENCIL8
        );
        m_source = std::make_unique<IRRender::Texture2D>(
            IRRender::TextureKind::TEXTURE_2D,
            kTargetDim,
            kTargetDim,
            IRRender::TextureFormat::RGBA8
        );
        m_quadVertices = std::make_unique<IRRender::Buffer>(
            IRShapes2D::k2DQuadTextured,
            sizeof(IRShapes2D::k2DQuadTextured),
            0
        );
        m_target->bind();
        m_program->use();
        m_source->bind(0);
        m_device->setDepthTest(false);
        m_device->setDepthWrite(false);
    }

    void TearDown() override {
        if (m_device == nullptr) {
            return;
        }
        m_device->setDepthTest(true);
        m_device->setDepthWrite(true);
        IRRender::setActiveMetalPipeline(nullptr);
        IRRender::bindMetalDefaultRenderTarget();
        m_quadVertices.reset();
        m_source.reset();
        m_target.reset();
        m_program.reset();
        MTL::Device *rawDevice = IRRender::metalDevice();
        IRRender::shutdownMetalRuntime();
        if (rawDevice != nullptr) {
            rawDevice->release();
        }
        m_device = nullptr;
    }

    void drawWith(const IRRender::VertexLayout &layout) {
        layout.bind();
        m_device->drawArrays(IRRender::DrawMode::TRIANGLES, 0, 6);
        m_device->finish();
    }

    std::size_t cachedStateCount() const {
        return IRRender::activeMetalPipeline()->renderPipelineStateCount();
    }

    IRRender::RenderDevice *m_device = nullptr;
    std::unique_ptr<IRRender::ShaderProgram> m_program;
    std::unique_ptr<IRRender::Framebuffer> m_target;
    std::unique_ptr<IRRender::Texture2D> m_source;
    std::unique_ptr<IRRender::Buffer> m_quadVertices;
};

TEST_F(MetalPipelineKeyCacheTest, LayoutsWithEqualContentsShareOneState) {
    using namespace IRRender;
    const VertexLayout first{m_quadVertices.get(), nullptr, 2, kAttrList2Float2};
    const VertexLayout second{m_quadVertices.get(), nullptr, 2, kAttrList2Float2};

    drawWith(first);
    drawWith(second);

    EXPECT_EQ(cachedStateCount(), 1u);
}

// Move-assigning a fresh layout builds the replacement before the old one is
// released, so each round's descriptor lives at a new address: the shape a
// growing vertex buffer takes when its owner replaces its layout in place.
TEST_F(MetalPipelineKeyCacheTest, ReplacingALayoutWithEqualContentsBuildsNoNewState) {
    using namespace IRRender;
    VertexLayout layout{m_quadVertices.get(), nullptr, 2, kAttrList2Float2};
    drawWith(layout);
    for (int round = 0; round < 10; ++round) {
        layout = VertexLayout{m_quadVertices.get(), nullptr, 2, kAttrList2Float2};
        drawWith(layout);
    }

    EXPECT_EQ(cachedStateCount(), 1u);
}

TEST_F(MetalPipelineKeyCacheTest, DestroyingAndRecreatingAnEqualLayoutBuildsNoNewState) {
    using namespace IRRender;
    for (int round = 0; round < 10; ++round) {
        const VertexLayout layout{m_quadVertices.get(), nullptr, 2, kAttrList2Float2};
        drawWith(layout);
    }

    EXPECT_EQ(cachedStateCount(), 1u);
}

TEST_F(MetalPipelineKeyCacheTest, EachBlendSettingHasItsOwnState) {
    using namespace IRRender;
    const VertexLayout quad{m_quadVertices.get(), nullptr, 2, kAttrList2Float2};

    drawWith(quad);
    m_device->enableBlending();
    drawWith(quad);
    m_device->disableBlending();
    drawWith(quad);

    EXPECT_EQ(cachedStateCount(), 2u);
}

#ifndef IR_RELEASE
TEST_F(MetalPipelineKeyCacheTest, RefusesALayoutTheSignatureCannotHold) {
    using namespace IRRender;
    constexpr unsigned int kTooMany = MetalVertexLayoutSignature::kMaxAttributes + 1;
    std::vector<VertexArrayAttribute> attributes(kTooMany, kAttrFloat2);
    EXPECT_THROW(
        (VertexLayout{m_quadVertices.get(), nullptr, kTooMany, attributes.data()}),
        std::runtime_error
    );
}
#endif

} // namespace

#endif // IR_GRAPHICS_METAL
