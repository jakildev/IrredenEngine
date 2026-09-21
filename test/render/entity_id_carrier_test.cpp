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
    EXPECT_EQ(
        IRRender::kEntityIdFogWholeBodyMaskInHighWord & IRRender::kEntityIdCutFaceMaskInHighWord,
        0u
    );
    EXPECT_EQ(
        IRRender::kEntityIdFogWholeBodyMaskInHighWord &
            IRRender::kEntityIdPriorityMaskInHighWord,
        0u
    );
    EXPECT_EQ(
        IRRender::kEntityIdHighWordMask & IRRender::kEntityIdFogWholeBodyMaskInHighWord,
        0u
    );
}

TEST(EntityIdCarrier, FogWholeBodyBitAloneDecodesToTheBareId) {
    const IRMath::uvec2 packed =
        carrierWords(kId, IRRender::kEntityIdFogWholeBodyMaskInHighWord);
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
