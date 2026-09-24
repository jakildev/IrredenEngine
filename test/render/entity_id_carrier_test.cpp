#include <gtest/gtest.h>

#include <irreden/render/ir_render_types.hpp>

#include <cstdint>

namespace {

constexpr std::uint64_t kId = 0x0000000200ABCDEFull;

IRMath::uvec2 carrierWords(std::uint64_t id, std::uint32_t highWordFlags) {
    return IRMath::uvec2(
        static_cast<std::uint32_t>(id),
        static_cast<std::uint32_t>(id >> 32) | highWordFlags
    );
}

} // namespace

TEST(EntityIdCarrier, CarrierBitsAreDisjointAndOutsideTheMask) {
    EXPECT_EQ(IRRender::kEntityIdFogWholeBodyMaskInHighWord, 1u << 28);
    EXPECT_EQ(IRRender::kEntityIdFogBodyMaskInHighWord, 1u << 28);
    EXPECT_EQ(IRRender::kEntityIdFogBodyFactorMaskInHighWord, 0xFFu << 20);
    EXPECT_EQ(
        IRRender::kEntityIdFogBodyFactorMaskInHighWord &
            (IRRender::kEntityIdFogBodyMaskInHighWord | IRRender::kEntityIdCutFaceMaskInHighWord |
             IRRender::kEntityIdPriorityMaskInHighWord),
        0u
    );
    EXPECT_EQ(IRRender::kEntityIdHighWordMask & IRRender::kEntityIdFogBodyFactorMaskInHighWord, 0u);
    EXPECT_EQ(
        IRRender::kEntityIdFogWholeBodyMaskInHighWord & IRRender::kEntityIdCutFaceMaskInHighWord,
        0u
    );
    EXPECT_EQ(
        IRRender::kEntityIdFogWholeBodyMaskInHighWord & IRRender::kEntityIdPriorityMaskInHighWord,
        0u
    );
    EXPECT_EQ(IRRender::kEntityIdHighWordMask & IRRender::kEntityIdFogWholeBodyMaskInHighWord, 0u);
}

TEST(EntityIdCarrier, FogWholeBodyBitAloneDecodesToTheBareId) {
    const IRMath::uvec2 packed = carrierWords(kId, IRRender::kEntityIdFogWholeBodyMaskInHighWord);
    EXPECT_EQ(IRRender::decodeCarrierEntityId(packed), kId);
    EXPECT_EQ(IRRender::decodeCarrierPriority(packed), 0u);
}

TEST(EntityIdCarrier, FogWholeBodyBitWithEveryOtherCarrierBitDecodesToTheBareId) {
    for (std::uint32_t priority = 0; priority < 4; ++priority) {
        const std::uint32_t flags = IRRender::kEntityIdFogWholeBodyMaskInHighWord |
                                    IRRender::kEntityIdCutFaceMaskInHighWord |
                                    (priority << IRRender::kEntityIdPriorityShiftInHighWord);
        const IRMath::uvec2 packed = carrierWords(kId, flags);
        EXPECT_EQ(IRRender::decodeCarrierEntityId(packed), kId) << "priority " << priority;
        EXPECT_EQ(IRRender::decodeCarrierPriority(packed), priority);
    }
}

// The class bit plus every factor value, under every priority tier and the
// cut-face bit, decodes back to the bare id with the priority untouched, and
// the factor reads back exactly.
TEST(EntityIdCarrier, EveryFogBodyFactorDecodesToTheBareIdAndRoundTrips) {
    for (std::uint32_t priority = 0; priority < 4; ++priority) {
        for (std::uint32_t factor = 0; factor < 256; ++factor) {
            const std::uint32_t flags =
                IRRender::kEntityIdFogBodyMaskInHighWord |
                IRRender::kEntityIdCutFaceMaskInHighWord |
                (factor << IRRender::kEntityIdFogBodyFactorShiftInHighWord) |
                (priority << IRRender::kEntityIdPriorityShiftInHighWord);
            const IRMath::uvec2 packed = carrierWords(kId, flags);
            ASSERT_EQ(IRRender::decodeCarrierEntityId(packed), kId)
                << "priority " << priority << " factor " << factor;
            ASSERT_EQ(IRRender::decodeCarrierPriority(packed), priority);
            ASSERT_TRUE(IRRender::decodeCarrierFogBody(packed));
            ASSERT_EQ(IRRender::decodeFogBodyFactor(packed), factor);
        }
    }
}

TEST(EntityIdCarrier, AnUnflaggedIdReadsNoFogBodyAndFactorZero) {
    const IRMath::uvec2 packed = carrierWords(kId, 0u);
    EXPECT_FALSE(IRRender::decodeCarrierFogBody(packed));
    EXPECT_EQ(IRRender::decodeFogBodyFactor(packed), 0u);
}
