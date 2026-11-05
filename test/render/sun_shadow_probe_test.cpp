#include <irreden/render/sun_shadow_probe.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <vector>

namespace {

using namespace IRPrefab::SunShadow;

constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;

// Three slots over two texels in planar storage: min planes 0..2, then max
// planes 0..2. Texel 0 claimed one slab, so its min plane 1 is EMPTY (bit 31
// set) while its max plane 0 carries no flag; texel 1 overflowed, so its max
// plane 0 carries the flag while its min plane 1 holds a claimed depth with
// bit 31 clear. The two words disagree on every texel, so a row that reads
// the wrong plane flips both overflow columns.
TEST(SunPrimaryDepthProbeRowTest, OverflowColumnReadsMaxPlaneZero) {
    constexpr std::uint32_t slots = 3u;
    constexpr std::size_t stride = 2u;
    std::vector<std::uint32_t> planes(2u * slots * stride, 0u);
    const auto word = [&](std::uint32_t plane, std::size_t texel) -> std::uint32_t & {
        return planes[std::size_t(plane) * stride + texel];
    };
    word(0, 0) = 0x100u;
    word(1, 0) = kEmpty;
    word(2, 0) = kEmpty;
    word(slots + 0, 0) = 0x200u;
    word(0, 1) = 0x100u;
    word(1, 1) = 0x10000u;
    word(2, 1) = 0x20000u;
    word(slots + 0, 1) = 0x300u | kSunDepthSlabOverflow;
    word(slots + 1, 1) = 0x10100u;
    word(slots + 2, 1) = 0x20100u;

    std::ostringstream rows;
    writeSunPrimaryDepthProbeRow(rows, 0u, 5, 7, 0x100u, planes.data(), stride, 0, slots, false);
    writeSunPrimaryDepthProbeRow(rows, 1u, 6, 7, 0x100u, planes.data(), stride, 1, slots, false);
    EXPECT_EQ(
        rows.str(),
        "0,5,7,256,256,512,4294967295,0,4294967295,0,0,0\n"
        "1,6,7,256,256,768,65536,65792,131072,131328,1,0\n"
    );
}

TEST(SunPrimaryDepthProbeRowTest, ZeroSlotsEmitNearestAndNoOverflow) {
    std::ostringstream row;
    writeSunPrimaryDepthProbeRow(row, 1u, 3, 4, kEmpty, nullptr, 0, 0, 0u);
    EXPECT_EQ(row.str(), "1,3,4,4294967295,0,0\n");
}

TEST(SunPrimaryDepthProbeRowTest, DirectCollisionChecksEveryOccupiedSlotAndKeepsRawExtrema) {
    const std::vector<std::uint32_t>
        planes{kEmpty, 0x04000100u, 0x08000100u, 0u, 0x07000200u, 0x08000200u};
    std::ostringstream row;
    writeSunPrimaryDepthProbeRow(row, 1u, 3, 4, 0x04000100u, planes.data(), 1u, 0, 3u, true);
    EXPECT_EQ(
        row.str(),
        "1,3,4,67109120,4294967295,0,67109120,117441024,134217984,134218240,0,1\n"
    );
}

TEST(SunPrimaryDepthProbeRowTest, DirectSparseSlotsAndDuplicatesHaveNoCollision) {
    const std::vector<std::uint32_t> planes{kEmpty, kEmpty, 0x08000100u, 0u, 0u, 0x08000100u};
    std::ostringstream row;
    writeSunPrimaryDepthProbeRow(row, 0u, 0, 0, 0x08000100u, planes.data(), 1u, 0, 3u, true);
    EXPECT_EQ(row.str(), "0,0,0,134217984,4294967295,0,4294967295,0,134217984,134217984,0,0\n");
}

} // namespace
