#include <gtest/gtest.h>

#include <irreden/profile/process_memory.hpp>

#include <cstddef>
#include <vector>

namespace {

TEST(ProcessMemory, ReportsResidentAndPeakBytes) {
    const auto memory = IRProfile::processMemoryStats();
    EXPECT_GT(memory.residentBytes_, 0u);
    EXPECT_GE(memory.peakResidentBytes_, memory.residentBytes_);
}

TEST(ProcessMemory, ResidentAndPeakTrackTouchedAllocation) {
    constexpr std::size_t kTouchedBytes = 64u * 1024u * 1024u;
    const auto before = IRProfile::processMemoryStats();
    std::vector<unsigned char> touched(kTouchedBytes, 1);
    const auto after = IRProfile::processMemoryStats();
    ASSERT_EQ(touched[kTouchedBytes / 2], 1);
    EXPECT_GE(after.residentBytes_, before.residentBytes_ + kTouchedBytes / 2);
    EXPECT_GE(after.peakResidentBytes_, after.residentBytes_);
    EXPECT_GE(after.peakResidentBytes_, before.peakResidentBytes_);
}

} // namespace
