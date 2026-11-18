// The device-free half of IRPrefab::DepthTestedOwner: which scatter owner a
// pixel still shows, given the owner attachment and the depth attachment
// right after the scatter and at frame end. The capture step itself needs a
// device; the voxel editor's face_pick session is its executor.

#include <gtest/gtest.h>

#include <irreden/render/depth_tested_owner.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using IRMath::ivec2;
using IRMath::uvec2;
using IRPrefab::DepthTestedOwner::Pixel;
using IRPrefab::DepthTestedOwner::resolve;

constexpr IREntity::EntityId kOwner = 0x0000000700000042ull;

uvec2 ownerWords(IREntity::EntityId id) {
    return uvec2(static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(id >> 32));
}

std::vector<Pixel> resolveAll(
    const std::vector<uvec2> &owners,
    const std::vector<float> &scatter,
    const std::vector<float> &final,
    ivec2 size
) {
    std::vector<Pixel> pixels;
    resolve(owners.data(), scatter.data(), final.data(), size, pixels);
    return pixels;
}

} // namespace

TEST(DepthTestedOwner, UnchangedDepthKeepsTheScatterOwner) {
    const std::vector<uvec2> owners{ownerWords(kOwner)};
    const std::vector<float> depth{0.25f};
    const std::vector<Pixel> pixels = resolveAll(owners, depth, depth, ivec2(1, 1));
    ASSERT_EQ(pixels.size(), 1u);
    EXPECT_EQ(pixels[0].owner_, kOwner);
    EXPECT_EQ(pixels[0].coveredOwner_, IREntity::kNullEntity);
    EXPECT_EQ(pixels[0].depth_, 0.25f);
}

TEST(DepthTestedOwner, AnyChangedDepthBitMarksThePixelCovered) {
    const float scatter = 0.25f;
    const std::vector<float> finals{
        std::nextafter(scatter, 0.0f),
        std::nextafter(scatter, 1.0f),
        0.125f,
    };
    for (const float final : finals) {
        const std::vector<Pixel> pixels =
            resolveAll({ownerWords(kOwner)}, {scatter}, {final}, ivec2(1, 1));
        EXPECT_EQ(pixels[0].owner_, IREntity::kNullEntity) << final;
        EXPECT_EQ(pixels[0].coveredOwner_, kOwner) << final;
        EXPECT_EQ(pixels[0].depth_, final);
    }
}

TEST(DepthTestedOwner, SignedZeroIsAChangedBit) {
    const std::vector<Pixel> pixels =
        resolveAll({ownerWords(kOwner)}, {0.0f}, {-0.0f}, ivec2(1, 1));
    EXPECT_EQ(pixels[0].owner_, IREntity::kNullEntity);
    EXPECT_EQ(pixels[0].coveredOwner_, kOwner);
}

TEST(DepthTestedOwner, UnownedPixelHasNoOwnerEitherWay) {
    const std::vector<Pixel> kept = resolveAll({uvec2(0u)}, {0.5f}, {0.5f}, ivec2(1, 1));
    const std::vector<Pixel> drawn = resolveAll({uvec2(0u)}, {0.5f}, {0.25f}, ivec2(1, 1));
    for (const Pixel &pixel : {kept[0], drawn[0]}) {
        EXPECT_EQ(pixel.owner_, IREntity::kNullEntity);
        EXPECT_EQ(pixel.coveredOwner_, IREntity::kNullEntity);
    }
    EXPECT_EQ(drawn[0].depth_, 0.25f);
}

TEST(DepthTestedOwner, CarrierBitsAreStrippedFromTheOwner) {
    uvec2 words = ownerWords(kOwner);
    words.y |= 1u << IRRender::kEntityIdPriorityShiftInHighWord;
    const std::vector<Pixel> pixels = resolveAll({words}, {0.5f}, {0.5f}, ivec2(1, 1));
    EXPECT_EQ(pixels[0].owner_, kOwner);
}

// Texture row 0 is the bottom row on screen; the output is top-left.
TEST(DepthTestedOwner, OutputRowsAreTopLeftAndTextureRowsAreFlipped) {
    const ivec2 size(3, 2);
    const IREntity::EntityId bottomRight = 11;
    const IREntity::EntityId topLeft = 12;
    std::vector<uvec2> owners(6, uvec2(0u));
    owners[2] = ownerWords(bottomRight);
    owners[3] = ownerWords(topLeft);
    std::vector<float> scatter{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
    const std::vector<Pixel> pixels = resolveAll(owners, scatter, scatter, size);
    ASSERT_EQ(pixels.size(), 6u);
    EXPECT_EQ(pixels[0].owner_, topLeft);
    EXPECT_EQ(pixels[0].depth_, 0.4f);
    EXPECT_EQ(pixels[5].owner_, bottomRight);
    EXPECT_EQ(pixels[5].depth_, 0.3f);
    for (int i : {1, 2, 3, 4})
        EXPECT_EQ(pixels[static_cast<std::size_t>(i)].owner_, IREntity::kNullEntity) << i;
}

TEST(DepthTestedOwner, EmptySizeYieldsNoPixels) {
    std::vector<Pixel> pixels(4);
    resolve(nullptr, nullptr, nullptr, ivec2(0, 5), pixels);
    EXPECT_TRUE(pixels.empty());
    resolve(nullptr, nullptr, nullptr, ivec2(-1, -1), pixels);
    EXPECT_TRUE(pixels.empty());
}
