#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>

#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_canvas_draw_images.hpp>
#include <irreden/render/components/component_canvas_draw_queue.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace Draw = IRPrefab::CanvasDraw;

using IRComponents::C_CanvasDrawImages;
using IRComponents::C_CanvasDrawQueue;
using IRComponents::C_SizeTriangles;
using IRMath::Color;
using IRMath::ivec2;
using IRMath::vec4;

std::vector<std::uint8_t> solidImage(ivec2 size, Color color) {
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < size.x * size.y; ++i) {
        rgba.insert(rgba.end(), {color.red_, color.green_, color.blue_, color.alpha_});
    }
    return rgba;
}

std::array<std::uint8_t, 4> atlasTexel(const IRRender::MipAtlas &atlas, ivec2 at) {
    const std::size_t o = (static_cast<std::size_t>(at.y) * atlas.atlasSize_.x + at.x) * 4;
    return {atlas.pixels_[o], atlas.pixels_[o + 1], atlas.pixels_[o + 2], atlas.pixels_[o + 3]};
}

// ---- IRRender::buildMipAtlas ------------------------------------------------

static_assert(IRRender::mipLevelCount(ivec2(8, 4)) == 4);
static_assert(IRRender::mipLevelCount(ivec2(1, 1)) == 1);
static_assert(IRRender::mipLevelCount(ivec2(5, 3)) == 3);
static_assert(IRRender::mipLevelSize(ivec2(8, 4), 3) == ivec2(1, 1));
static_assert(IRRender::mipLevelSize(ivec2(5, 3), 1) == ivec2(2, 1));
static_assert(IRRender::mipAtlasLevelOrigin(ivec2(8, 4), 0) == ivec2(0, 0));
static_assert(IRRender::mipAtlasLevelOrigin(ivec2(8, 4), 1) == ivec2(8, 0));
static_assert(IRRender::mipAtlasLevelOrigin(ivec2(8, 4), 2) == ivec2(8, 2));
static_assert(IRRender::mipAtlasLevelOrigin(ivec2(8, 4), 3) == ivec2(8, 3));

TEST(MipAtlas, PacksEveryLevelBesideTheBase) {
    const ivec2 size(8, 4);
    const Color orange{230, 140, 40, 255};
    const IRRender::MipAtlas atlas =
        IRRender::buildMipAtlas(solidImage(size, orange).data(), size, 4096);
    EXPECT_EQ(atlas.baseSize_, size);
    EXPECT_EQ(atlas.levelCount_, 4);
    EXPECT_EQ(atlas.atlasSize_, ivec2(12, 4));
    ASSERT_EQ(atlas.pixels_.size(), 12u * 4u * 4u);
    // A flat image is the same colour at every level.
    for (int level = 0; level < atlas.levelCount_; ++level) {
        const ivec2 origin = IRRender::mipAtlasLevelOrigin(size, level);
        const ivec2 levelSize = IRRender::mipLevelSize(size, level);
        for (int y = 0; y < levelSize.y; ++y) {
            for (int x = 0; x < levelSize.x; ++x) {
                const std::array<std::uint8_t, 4> texel = atlasTexel(atlas, origin + ivec2(x, y));
                EXPECT_EQ(texel, (std::array<std::uint8_t, 4>{230, 140, 40, 255}))
                    << "level " << level << " at " << x << "," << y;
            }
        }
    }
}

TEST(MipAtlas, EachLevelAveragesTheOneAbove) {
    // Columns alternate black and white, so every level below the base is
    // mid grey.
    const ivec2 size(4, 2);
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < size.x * size.y; ++i) {
        const std::uint8_t value = (i % size.x) % 2 == 0 ? 0 : 255;
        rgba.insert(rgba.end(), {value, value, value, 255});
    }
    const IRRender::MipAtlas atlas = IRRender::buildMipAtlas(rgba.data(), size, 4096);
    ASSERT_EQ(atlas.levelCount_, 3);
    EXPECT_EQ(atlasTexel(atlas, ivec2(1, 0)), (std::array<std::uint8_t, 4>{255, 255, 255, 255}));
    const ivec2 levelOne = IRRender::mipAtlasLevelOrigin(size, 1);
    EXPECT_EQ(atlasTexel(atlas, levelOne), (std::array<std::uint8_t, 4>{128, 128, 128, 255}));
    EXPECT_EQ(
        atlasTexel(atlas, levelOne + ivec2(1, 0)),
        (std::array<std::uint8_t, 4>{128, 128, 128, 255})
    );
    EXPECT_EQ(
        atlasTexel(atlas, IRRender::mipAtlasLevelOrigin(size, 2)),
        (std::array<std::uint8_t, 4>{128, 128, 128, 255})
    );
}

TEST(MipAtlas, OddSizesWeightEveryTexelOnce) {
    // 3 texels into 1: each contributes a third.
    const ivec2 size(3, 1);
    std::vector<std::uint8_t> rgba{0, 0, 0, 255, 90, 90, 90, 255, 180, 180, 180, 255};
    const IRRender::MipAtlas atlas = IRRender::buildMipAtlas(rgba.data(), size, 4096);
    ASSERT_EQ(atlas.levelCount_, 2);
    EXPECT_EQ(atlas.atlasSize_, ivec2(4, 1));
    EXPECT_EQ(atlasTexel(atlas, ivec2(3, 0)), (std::array<std::uint8_t, 4>{90, 90, 90, 255}));
}

TEST(MipAtlas, PremultipliesByAlpha) {
    const ivec2 size(2, 1);
    std::vector<std::uint8_t> rgba{200, 100, 50, 255, 200, 100, 50, 0};
    const IRRender::MipAtlas atlas = IRRender::buildMipAtlas(rgba.data(), size, 4096);
    EXPECT_EQ(atlasTexel(atlas, ivec2(0, 0)), (std::array<std::uint8_t, 4>{200, 100, 50, 255}));
    EXPECT_EQ(atlasTexel(atlas, ivec2(1, 0)), (std::array<std::uint8_t, 4>{0, 0, 0, 0}));
    // The clear texel adds no colour to the average, only halves its alpha.
    EXPECT_EQ(atlasTexel(atlas, ivec2(2, 0)), (std::array<std::uint8_t, 4>{100, 50, 25, 128}));
}

TEST(MipAtlas, FitsAnOversizedImageToTheLimitKeepingItsAspect) {
    const ivec2 size(40, 20);
    const Color teal{20, 160, 150, 255};
    const IRRender::MipAtlas atlas =
        IRRender::buildMipAtlas(solidImage(size, teal).data(), size, 8);
    EXPECT_EQ(atlas.baseSize_, ivec2(8, 4));
    EXPECT_EQ(atlas.levelCount_, 4);
    EXPECT_EQ(atlasTexel(atlas, ivec2(7, 3)), (std::array<std::uint8_t, 4>{20, 160, 150, 255}));
}

TEST(MipAtlas, NoImageIsEmpty) {
    const IRRender::MipAtlas atlas = IRRender::buildMipAtlas(nullptr, ivec2(0, 0), 4096);
    EXPECT_TRUE(atlas.pixels_.empty());
    EXPECT_EQ(atlas.levelCount_, 0);
}

// ---- IRPrefab::CanvasDraw images --------------------------------------------

// Stands in for the decode and upload: a 300 x 200 source held at 150 x 100,
// no texture. A path containing "missing" fails.
bool probeLoader(const std::string &path, Draw::ImageSource &source, std::string &error) {
    if (path.find("missing") != std::string::npos) {
        error = "failed to load image '" + path + "'";
        return false;
    }
    source.sourceSize_ = ivec2(300, 200);
    source.baseSize_ = ivec2(150, 100);
    source.levelCount_ = IRRender::mipLevelCount(source.baseSize_);
    return true;
}

class CanvasDrawImageTest : public testing::Test {
  protected:
    void SetUp() override {
        m_canvas = IREntity::createEntity(C_SizeTriangles{ivec2(200, 100)});
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    Draw::ImageHandle load(const std::string &path) {
        std::string error;
        return Draw::loadImage(path, error, probeLoader);
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
};

TEST_F(CanvasDrawImageTest, LoadingAPathAgainSharesItsHandle) {
    const Draw::ImageHandle first = load("a.png");
    const Draw::ImageHandle second = load("b.png");
    ASSERT_NE(first, Draw::kInvalidImage);
    EXPECT_NE(second, first);
    EXPECT_EQ(load("a.png"), first);
    EXPECT_EQ(Draw::images().images_.size(), 2u);
    EXPECT_EQ(Draw::imageSize(first), ivec2(300, 200));
    EXPECT_TRUE(Draw::isImage(first));
}

TEST_F(CanvasDrawImageTest, AFailedLoadRegistersNothing) {
    std::string error;
    EXPECT_EQ(Draw::loadImage("missing.png", error, probeLoader), Draw::kInvalidImage);
    EXPECT_NE(error.find("missing.png"), std::string::npos);
    EXPECT_TRUE(Draw::images().images_.empty());
    EXPECT_EQ(Draw::imageSize(Draw::kInvalidImage), ivec2(0));
}

TEST_F(CanvasDrawImageTest, ReleaseDropsOneReferenceAndRetiresTheHandleAtZero) {
    const Draw::ImageHandle handle = load("a.png");
    load("a.png");
    Draw::releaseImage(handle);
    EXPECT_TRUE(Draw::isImage(handle));
    Draw::releaseImage(handle);
    EXPECT_FALSE(Draw::isImage(handle));
    EXPECT_EQ(Draw::imageSize(handle), ivec2(0));
    Draw::releaseImage(handle);
    EXPECT_NE(load("a.png"), handle);
}

TEST_F(CanvasDrawImageTest, QueuedImageCarriesItsBoxAtlasAndDepth) {
    const Draw::ImageHandle handle = load("a.png");
    Draw::queueImage(m_canvas, handle, ivec2(-10, 20), ivec2(60, 40), {}, Draw::kDepthBehind);
    const C_CanvasDrawQueue &queue = Draw::queue();
    ASSERT_EQ(queue.batches_.size(), 1u);
    const C_CanvasDrawQueue::Batch &batch = queue.batches_[0];
    ASSERT_EQ(batch.images_.size(), 1u);
    ASSERT_EQ(batch.primitives_.size(), 1u);
    EXPECT_EQ(batch.images_[0].handle_, handle);
    EXPECT_EQ(batch.primitives_[0].image_, 0);
    EXPECT_EQ(batch.primitives_[0].lo_, ivec2(0, 20));
    EXPECT_EQ(batch.primitives_[0].hi_, ivec2(50, 60));
    const IRRender::CanvasImageCommand &command = batch.images_[0].command_;
    EXPECT_EQ(command.clipLo_, ivec2(0, 20));
    EXPECT_EQ(command.clipHi_, ivec2(50, 60));
    EXPECT_EQ(command.origin_, ivec2(-10, 20));
    EXPECT_EQ(command.size_, ivec2(60, 40));
    EXPECT_EQ(command.baseSize_, ivec2(150, 100));
    EXPECT_EQ(command.levelCount_, 8);
    EXPECT_EQ(command.distance_, Draw::kDepthBehind);
}

TEST_F(CanvasDrawImageTest, UnknownHandlesAndOffCanvasBoxesQueueNothing) {
    const Draw::ImageHandle handle = load("a.png");
    Draw::queueImage(m_canvas, handle + 1, ivec2(0), ivec2(10), {}, Draw::kDepthFront);
    Draw::queueImage(m_canvas, handle, ivec2(200, 0), ivec2(10), {}, Draw::kDepthFront);
    Draw::queueImage(m_canvas, handle, ivec2(0), ivec2(10, 0), {}, Draw::kDepthFront);
    Draw::releaseImage(handle);
    Draw::queueImage(m_canvas, handle, ivec2(0), ivec2(10), {}, Draw::kDepthFront);
    for (const C_CanvasDrawQueue::Batch &batch : Draw::queue().batches_) {
        EXPECT_TRUE(batch.images_.empty());
        EXPECT_TRUE(batch.primitives_.empty());
    }
}

TEST_F(CanvasDrawImageTest, CommandPullsOptionsIntoRange) {
    C_CanvasDrawImages::Image image;
    image.baseSize_ = ivec2(64, 32);
    image.levelCount_ = 7;
    const auto command = [&image](const Draw::ImageOptions &options) {
        return Draw::detail::imageCommand(
            image,
            ivec2(0),
            ivec2(10),
            ivec2(0),
            ivec2(10),
            options,
            Draw::kDepthFront
        );
    };
    EXPECT_EQ(
        command({.uvRect_ = vec4(-1.0f, 0.25f, 2.0f, 0.75f)}).uvRect_,
        vec4(0.0f, 0.25f, 1.0f, 0.75f)
    );
    // A mirrored crop is kept as given.
    EXPECT_EQ(
        command({.uvRect_ = vec4(1.0f, 0.0f, 0.0f, 1.0f)}).uvRect_,
        vec4(1.0f, 0.0f, 0.0f, 1.0f)
    );
    EXPECT_EQ(command({.cell_ = 0}).cell_, 1);
    EXPECT_EQ(command({.cell_ = -7}).cell_, 1);
    EXPECT_EQ(command({.cell_ = 12}).cell_, 12);
    EXPECT_EQ(command({.cell_ = 1 << 20}).cell_, Draw::kMaxImageCell);
    EXPECT_EQ(command({.levels_ = 0}).posterizeLevels_, 0);
    EXPECT_EQ(command({.levels_ = 1}).posterizeLevels_, 0);
    EXPECT_EQ(command({.levels_ = 2}).posterizeLevels_, 2);
    EXPECT_EQ(command({.levels_ = 500}).posterizeLevels_, Draw::kMaxImagePosterizeLevels);
    EXPECT_EQ(
        command({.tint_ = Color{1, 2, 3, 255}}).tintPacked_,
        Color(1, 2, 3, 255).toPackedRGBA()
    );
}

} // namespace
