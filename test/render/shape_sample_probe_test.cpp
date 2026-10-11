#include <gtest/gtest.h>
#include <irreden/render/shape_sample_probe.hpp>

namespace {
using namespace IRMath;
namespace ProbeDetail = IRPrefab::ShapeSampleProbe::detail;

const auto fixtureModel = [] {
    return IRSystem::System<IRSystem::TRIXEL_TO_FRAMEBUFFER>::calcModelMatrix(
        vec2(1284, 722),
        vec2(0),
        vec2(1),
        vec2(1)
    );
};

auto fixturePixel(ivec2 pixel) {
    return ProbeDetail::mapPixel(
        pixel,
        ivec2(2560, 1440),
        ivec2(2),
        ivec2(1284, 722),
        vec2(0),
        ivec2(642, 722),
        fixtureModel()
    );
}

TEST(ShapeSampleProbe, NativeGatherUsesNearestFramebufferCenter) {
    const auto tied = fixturePixel(ivec2(1253, 569));
    ASSERT_TRUE(tied.valid_);
    EXPECT_EQ(tied.framebufferPixel_, ivec2(628, 285));
    EXPECT_EQ(tied.preUpscalePosition_, vec2(628.75f, 285.75f));
    EXPECT_NEAR(tied.canvasOriginRaw_.x - 321, -6.75f, 0.0001f);
    EXPECT_NEAR(tied.canvasOriginRaw_.y - 361, -75.5f, 0.0001f);
    const auto unique = fixturePixel(ivec2(1250, 570));
    ASSERT_TRUE(unique.valid_);
    EXPECT_EQ(unique.framebufferPixel_, ivec2(627, 286));
    EXPECT_NEAR(unique.canvasOriginRaw_.x - 321, -7.25f, 0.0001f);
    EXPECT_NEAR(unique.canvasOriginRaw_.y - 361, -74.5f, 0.0001f);
    for (int y = 568; y <= 569; ++y)
        for (int x = 1252; x <= 1253; ++x)
            EXPECT_EQ(fixturePixel(ivec2(x, y)).canvasOriginRaw_, tied.canvasOriginRaw_);
}

TEST(ShapeSampleProbe, RejectsOutsideViewportFramebufferAndCanvas) {
    EXPECT_FALSE(fixturePixel(ivec2(-1, 100)).valid_);
    EXPECT_FALSE(fixturePixel(ivec2(2560, 100)).valid_);
    EXPECT_FALSE(fixturePixel(ivec2(100, 1440)).valid_);
    EXPECT_FALSE(
        ProbeDetail::mapPixel(
            ivec2(10),
            ivec2(2560, 1440),
            ivec2(0),
            ivec2(1284, 722),
            vec2(0),
            ivec2(642, 722),
            fixtureModel()
        )
            .valid_
    );
    EXPECT_FALSE(
        ProbeDetail::mapPixel(
            ivec2(10),
            ivec2(2560, 1440),
            ivec2(2),
            ivec2(1284, 722),
            vec2(4000),
            ivec2(642, 722),
            fixtureModel()
        )
            .valid_
    );
    auto tinyModel = fixtureModel();
    tinyModel[0].x = 1;
    EXPECT_FALSE(
        ProbeDetail::mapPixel(
            ivec2(10),
            ivec2(2560, 1440),
            ivec2(2),
            ivec2(1284, 722),
            vec2(0),
            ivec2(642, 722),
            tinyModel
        )
            .valid_
    );
}

TEST(ShapeSampleProbe, DecodesOwnerAndRejectsForeignPublication) {
    const auto owner = ProbeDetail::decodeOwner(((13u * 64u + 57u) * 3u + 2u) * 2u + 1u, 14);
    ASSERT_TRUE(owner.valid_);
    EXPECT_EQ(owner.tile_, 13u);
    EXPECT_EQ(owner.lane_, 57u);
    EXPECT_EQ(owner.face_, 2u);
    EXPECT_EQ(owner.half_, 1u);
    EXPECT_FALSE(ProbeDetail::decodeOwner(14u * 384u, 14).valid_);
    EXPECT_FALSE(ProbeDetail::decodeOwner(0xffffffffu, 20000000).valid_);
    IRRender::GPUShapeDescriptor shape{};
    shape.entityId = 123;
    EXPECT_TRUE(ProbeDetail::matchesPublication(shape, 123, 37 * 8 + 2, owner));
    EXPECT_FALSE(ProbeDetail::matchesPublication(shape, 124, 37 * 8 + 2, owner));
    EXPECT_FALSE(ProbeDetail::matchesPublication(shape, 123, 37 * 8 + 1, owner));
    EXPECT_FALSE(ProbeDetail::matchesPublication(shape, 123, 37 * 8 + 6, owner));
}

TEST(ShapeSampleProbe, PublicationExpiresOnDifferentTickAndEveryInvalidation) {
    IRComponents::CanvasShapeGeometry geometry;
    geometry.frameData_.shapeCount = 1;
    geometry.tileCount_ = 1;
    geometry.ownerSize_ = ivec2(2);
    EXPECT_FALSE(geometry.samplesValidForTick(0));
    geometry.publishSamples(true, 7);
    EXPECT_TRUE(geometry.samplesValidForTick(7));
    EXPECT_FALSE(geometry.samplesValidForTick(8));
    EXPECT_EQ(geometry.publicationTick(), 7u);
    geometry.invalidateSamples();
    EXPECT_FALSE(geometry.samplesValidForTick(7));
    EXPECT_EQ(geometry.publicationTick(), 0u);
    geometry.publishSamples(false, 8);
    EXPECT_FALSE(geometry.samplesValidForTick(8));
    EXPECT_EQ(geometry.publicationTick(), 0u);
    geometry.publishSamples(true, 9);
    geometry.reset();
    EXPECT_FALSE(geometry.samplesValidForTick(9));
    EXPECT_EQ(geometry.publicationTick(), 0u);
}
} // namespace
