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
#include <limits>
#include <string>
#include <vector>

namespace {

namespace Draw = IRPrefab::CanvasDraw;

using IRComponents::C_CanvasDrawImages;
using IRComponents::C_CanvasDrawQueue;
using IRComponents::C_SizeTriangles;
using IRMath::Color;
using IRMath::ivec2;
using IRMath::vec2;
using IRMath::vec4;

constexpr vec4 kNoTurn(1.0f, 0.0f, 0.0f, 1.0f);
constexpr float kTolerance = 1e-4f;

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
    EXPECT_EQ(command.unrotate_, kNoTurn);
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
            options,
            Draw::kCanvasTexelPixels,
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
    EXPECT_EQ(command({}).unrotate_, kNoTurn);
    EXPECT_EQ(command({.rotation_ = std::numeric_limits<float>::quiet_NaN()}).unrotate_, kNoTurn);
    EXPECT_EQ(command({.rotation_ = std::numeric_limits<float>::infinity()}).unrotate_, kNoTurn);
    EXPECT_NE(command({.rotation_ = 0.2f}).unrotate_, kNoTurn);
    // The clip is the whole box until a queue cuts it to its target.
    EXPECT_EQ(command({}).clipLo_, ivec2(0));
    EXPECT_EQ(command({}).clipHi_, ivec2(10));
}

// ---- Turned images ----------------------------------------------------------

// The command of a `size` box at `origin` turned `rotation` radians on a
// target of `texelPixels` texels.
IRRender::CanvasImageCommand
turnedCommand(ivec2 origin, ivec2 size, float rotation, vec2 texelPixels) {
    IRRender::CanvasImageCommand command;
    command.origin_ = origin;
    command.size_ = size;
    command.unrotate_ = Draw::imageUnrotation(rotation, texelPixels);
    return command;
}

vec2 unrotated(vec4 unrotate, vec2 offset) {
    return vec2(
        unrotate.x * offset.x + unrotate.y * offset.y,
        unrotate.z * offset.x + unrotate.w * offset.y
    );
}

TEST(CanvasImageRotation, NoTurnIsTheIdentityOnEitherTarget) {
    EXPECT_EQ(Draw::imageUnrotation(0.0f, Draw::kCanvasTexelPixels), kNoTurn);
    EXPECT_EQ(Draw::imageUnrotation(0.0f, Draw::kFramebufferTexelPixels), kNoTurn);
    EXPECT_EQ(Draw::imageUnrotation(-0.0f, Draw::kCanvasTexelPixels), kNoTurn);
}

TEST(CanvasImageRotation, APositiveTurnIsClockwiseOnScreen) {
    // A quarter turn clockwise swings the box's right edge to the bottom of
    // the screen, so the pixel below the centre is the box's right.
    const vec4 quarter = Draw::imageUnrotation(IRMath::kHalfPi, Draw::kFramebufferTexelPixels);
    const vec2 below = unrotated(quarter, vec2(0.0f, 8.0f));
    EXPECT_NEAR(below.x, 8.0f, kTolerance);
    EXPECT_NEAR(below.y, 0.0f, kTolerance);
    const vec2 right = unrotated(quarter, vec2(8.0f, 0.0f));
    EXPECT_NEAR(right.x, 0.0f, kTolerance);
    EXPECT_NEAR(right.y, -8.0f, kTolerance);
}

TEST(CanvasImageRotation, ACanvasTurnIsRigidInFramebufferPixels) {
    // Eight pixels below the centre is eight canvas rows; a quarter turn maps
    // it to eight pixels along the box, which is four of its texels.
    const vec4 quarter = Draw::imageUnrotation(IRMath::kHalfPi, Draw::kCanvasTexelPixels);
    const vec2 below = unrotated(quarter, vec2(0.0f, 8.0f));
    EXPECT_NEAR(below.x, 4.0f, kTolerance);
    EXPECT_NEAR(below.y, 0.0f, kTolerance);

    // At any angle an offset keeps its length in pixels.
    const vec2 texelPixels = Draw::kCanvasTexelPixels;
    for (const float rotation : {0.1f, -0.35f, 0.7853982f, 2.4f, -3.0f, 40.0f}) {
        const vec4 unrotate = Draw::imageUnrotation(rotation, texelPixels);
        for (const vec2 offset : {vec2(5.0f, 0.0f), vec2(0.0f, 7.0f), vec2(-3.0f, 11.0f)}) {
            EXPECT_NEAR(
                IRMath::length(unrotated(unrotate, offset) * texelPixels),
                IRMath::length(offset * texelPixels),
                1e-3f
            ) << rotation;
        }
    }
}

TEST(CanvasImageRotation, AnUnturnedBoxIsItsOwnBoundsAndMapsTexelsExactly) {
    const IRRender::CanvasImageCommand command =
        turnedCommand(ivec2(-7, 13), ivec2(31, 18), 0.0f, Draw::kCanvasTexelPixels);
    ivec2 lo;
    ivec2 hi;
    Draw::imageBounds(command, lo, hi);
    EXPECT_EQ(lo, ivec2(-7, 13));
    EXPECT_EQ(hi, ivec2(24, 31));
    // Bit-exact, so a draw with no rotation samples what it always sampled.
    for (int y = 13; y < 31; ++y) {
        for (int x = -7; x < 24; ++x) {
            const vec2 point = Draw::imageBoxPoint(command, ivec2(x, y));
            ASSERT_EQ(
                point,
                vec2(static_cast<float>(x + 7) + 0.5f, static_cast<float>(y - 13) + 0.5f)
            );
            ASSERT_TRUE(Draw::imageCovers(command, ivec2(x, y)));
        }
    }
    EXPECT_FALSE(Draw::imageCovers(command, ivec2(-8, 13)));
    EXPECT_FALSE(Draw::imageCovers(command, ivec2(24, 13)));
    EXPECT_FALSE(Draw::imageCovers(command, ivec2(-7, 12)));
    EXPECT_FALSE(Draw::imageCovers(command, ivec2(-7, 31)));
}

TEST(CanvasImageRotation, AQuarterTurnSwapsTheBoxSidesInPixels) {
    // 100 x 40 pixels about (60, 40) becomes 40 x 100 about the same centre.
    ivec2 lo;
    ivec2 hi;
    Draw::imageBounds(
        turnedCommand(
            ivec2(10, 20),
            ivec2(100, 40),
            IRMath::kHalfPi,
            Draw::kFramebufferTexelPixels
        ),
        lo,
        hi
    );
    EXPECT_NEAR(lo.x, 40, 1);
    EXPECT_NEAR(lo.y, -10, 1);
    EXPECT_NEAR(hi.x, 80, 1);
    EXPECT_NEAR(hi.y, 90, 1);

    // 60 x 40 canvas texels are 120 x 40 pixels; turned, 40 x 120 pixels, which
    // is 20 x 120 texels about the same centre (40, 40).
    Draw::imageBounds(
        turnedCommand(ivec2(10, 20), ivec2(60, 40), IRMath::kHalfPi, Draw::kCanvasTexelPixels),
        lo,
        hi
    );
    EXPECT_NEAR(lo.x, 30, 1);
    EXPECT_NEAR(lo.y, -20, 1);
    EXPECT_NEAR(hi.x, 50, 1);
    EXPECT_NEAR(hi.y, 100, 1);
}

TEST(CanvasImageRotation, BoundsHoldEveryCoveredTexelAndTheTurnKeepsTheArea) {
    const ivec2 origin(-12, 9);
    const ivec2 size(64, 36);
    constexpr int kMargin = 80;
    for (const vec2 texelPixels : {Draw::kCanvasTexelPixels, Draw::kFramebufferTexelPixels}) {
        for (const float rotation : {0.05f, 0.3f, -0.6f, 0.7853982f, 1.5707964f, 2.2f, -3.1f}) {
            const IRRender::CanvasImageCommand command =
                turnedCommand(origin, size, rotation, texelPixels);
            ivec2 lo;
            ivec2 hi;
            Draw::imageBounds(command, lo, hi);
            int covered = 0;
            ivec2 coveredLo(1 << 20);
            ivec2 coveredHi(-(1 << 20));
            for (int y = origin.y - kMargin; y < origin.y + size.y + kMargin; ++y) {
                for (int x = origin.x - kMargin; x < origin.x + size.x + kMargin; ++x) {
                    if (!Draw::imageCovers(command, ivec2(x, y))) {
                        continue;
                    }
                    ++covered;
                    coveredLo = IRMath::min(coveredLo, ivec2(x, y));
                    coveredHi = IRMath::max(coveredHi, ivec2(x + 1, y + 1));
                    ASSERT_TRUE(x >= lo.x && y >= lo.y && x < hi.x && y < hi.y)
                        << "texel " << x << "," << y << " at " << rotation;
                }
            }
            // The matrix has determinant 1 in texels too, so the turned box
            // covers as many texels as the box holds, give or take its edge.
            EXPECT_NEAR(covered, size.x * size.y, size.x + size.y) << rotation;
            // The bounds are not loose: a corner is a point, so the outermost
            // covered texel sits within a few texels of them.
            EXPECT_LE(coveredLo.x - lo.x, 3) << rotation;
            EXPECT_LE(coveredLo.y - lo.y, 3) << rotation;
            EXPECT_LE(hi.x - coveredHi.x, 3) << rotation;
            EXPECT_LE(hi.y - coveredHi.y, 3) << rotation;
        }
    }
}

TEST(CanvasImageRotation, AnEmptyBoxHasNoBounds) {
    ivec2 lo;
    ivec2 hi;
    Draw::imageBounds(
        turnedCommand(ivec2(4, 5), ivec2(0, 30), 0.4f, Draw::kFramebufferTexelPixels),
        lo,
        hi
    );
    EXPECT_EQ(lo, ivec2(4, 5));
    EXPECT_EQ(hi, ivec2(4, 35));
}

TEST_F(CanvasDrawImageTest, ATurnedImageClipsItsBoundsToTheCanvas) {
    const Draw::ImageHandle handle = load("a.png");
    // 60 x 40 texels about (40, 40): a quarter turn makes it 20 x 120 texels,
    // which the 200 x 100 canvas cuts above and below.
    Draw::queueImage(
        m_canvas,
        handle,
        ivec2(10, 20),
        ivec2(60, 40),
        {.rotation_ = IRMath::kHalfPi},
        Draw::kDepthFront
    );
    const C_CanvasDrawQueue::Batch &batch = Draw::queue().batches_[0];
    ASSERT_EQ(batch.images_.size(), 1u);
    const IRRender::CanvasImageCommand &command = batch.images_[0].command_;
    EXPECT_EQ(command.origin_, ivec2(10, 20));
    EXPECT_EQ(command.size_, ivec2(60, 40));
    EXPECT_EQ(command.unrotate_, Draw::imageUnrotation(IRMath::kHalfPi, Draw::kCanvasTexelPixels));
    EXPECT_NEAR(command.clipLo_.x, 30, 1);
    EXPECT_EQ(command.clipLo_.y, 0);
    EXPECT_NEAR(command.clipHi_.x, 50, 1);
    EXPECT_EQ(command.clipHi_.y, 100);
    // The primitive reserves the same texels in the painter order.
    EXPECT_EQ(batch.primitives_[0].lo_, command.clipLo_);
    EXPECT_EQ(batch.primitives_[0].hi_, command.clipHi_);
}

TEST_F(CanvasDrawImageTest, ATurnCanBringAnOffCanvasBoxOnAndTakeAnOnCanvasBoxOff) {
    const Draw::ImageHandle handle = load("a.png");
    // 10 x 60 texels left of the canvas; turned a quarter it is 30 x 20 texels
    // about (-7, 70) and reaches texel column 0.
    const ivec2 pos(-12, 40);
    const ivec2 size(10, 60);
    Draw::queueImage(m_canvas, handle, pos, size, {}, Draw::kDepthFront);
    EXPECT_TRUE(Draw::queue().batches_[0].images_.empty());
    Draw::queueImage(
        m_canvas,
        handle,
        pos,
        size,
        {.rotation_ = IRMath::kHalfPi},
        Draw::kDepthFront
    );
    ASSERT_EQ(Draw::queue().batches_[0].images_.size(), 1u);
    const IRRender::CanvasImageCommand &command = Draw::queue().batches_[0].images_[0].command_;
    EXPECT_EQ(command.clipLo_.x, 0);
    EXPECT_NEAR(command.clipHi_.x, 8, 1);

    // 60 x 4 texels whose left end is on the canvas; turned a quarter it is
    // 2 x 120 texels about column 228.
    Draw::queueImage(
        m_canvas,
        handle,
        ivec2(198, 40),
        ivec2(60, 4),
        {.rotation_ = IRMath::kHalfPi},
        Draw::kDepthFront
    );
    EXPECT_EQ(Draw::queue().batches_[0].images_.size(), 1u);
}

TEST_F(CanvasDrawImageTest, ATurnedImageMeetsADrawItsUnturnedBoxWouldMiss) {
    const Draw::ImageHandle handle = load("a.png");
    // Two 60 x 10 texel strips side by side, 4 texels apart.
    const ivec2 size(60, 10);
    Draw::queueImage(m_canvas, handle, ivec2(20, 40), size, {}, Draw::kDepthFront);
    Draw::queueImage(m_canvas, handle, ivec2(84, 40), size, {}, Draw::kDepthFront);
    const C_CanvasDrawQueue::Batch &batch = Draw::queue().batches_[0];
    EXPECT_EQ(Draw::overlapGroupEnd(batch.primitives_, 0), 2u);

    // The same pair 20 rows down, the second turned a quarter about
    // (114, 65): 5 texels wide and as tall as the canvas, it crosses the rows
    // of the strip above it, which its unturned box clears.
    Draw::queueImage(m_canvas, handle, ivec2(20, 60), size, {}, Draw::kDepthFront);
    Draw::queueImage(
        m_canvas,
        handle,
        ivec2(84, 60),
        size,
        {.rotation_ = IRMath::kHalfPi},
        Draw::kDepthFront
    );
    EXPECT_EQ(Draw::overlapGroupEnd(batch.primitives_, 0), 3u);
}

} // namespace
