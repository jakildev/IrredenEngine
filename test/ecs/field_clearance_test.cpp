#include <gtest/gtest.h>

#include <irreden/spatial/field_clearance.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

using IRPrefab::Spatial::ChunkedField2D;
using IRPrefab::Spatial::FieldChunkKey;
using IRPrefab::Spatial::FieldClearance;
using IRPrefab::Spatial::kFieldChunkEdge;
using IRPrefab::Spatial::kMaxClearanceCells;

void addFreeFieldChunk(ChunkedField2D<std::uint8_t> &occupancy, IRMath::ivec2 chunk) {
    occupancy.setCell(chunk * kFieldChunkEdge, 0);
}

std::int32_t clearanceAt(const FieldClearance &clearance, IRMath::ivec2 cell) {
    std::int32_t value = -1;
    EXPECT_TRUE(clearance.values().getCell(cell, value));
    return value;
}

void expectSameField(const FieldClearance &left, const FieldClearance &right) {
    std::vector<FieldChunkKey> leftKeys;
    std::vector<FieldChunkKey> rightKeys;
    left.values().chunkKeys(leftKeys);
    right.values().chunkKeys(rightKeys);
    ASSERT_EQ(leftKeys, rightKeys);
    for (FieldChunkKey key : leftKeys) {
        const auto chunk = IRPrefab::Spatial::unpackFieldChunkKey(key);
        ASSERT_NE(left.values().findChunk(chunk), nullptr);
        ASSERT_NE(right.values().findChunk(chunk), nullptr);
        EXPECT_EQ(left.values().findChunk(chunk)->min_, right.values().findChunk(chunk)->min_);
        EXPECT_EQ(left.values().findChunk(chunk)->max_, right.values().findChunk(chunk)->max_);
        EXPECT_EQ(
            left.values().findChunk(chunk)->nonZeroCount_,
            right.values().findChunk(chunk)->nonZeroCount_
        );
        EXPECT_TRUE(
            std::equal(
                left.values().findChunk(chunk)->cells().begin(),
                left.values().findChunk(chunk)->cells().end(),
                right.values().findChunk(chunk)->cells().begin()
            )
        );
        const auto cells = left.values().findChunk(chunk)->cells();
        const auto [minimum, maximum] = std::minmax_element(cells.begin(), cells.end());
        const int nonZeroCount = static_cast<int>(
            std::count_if(cells.begin(), cells.end(), [](std::int32_t value) { return value != 0; })
        );
        EXPECT_EQ(left.values().findChunk(chunk)->min_, *minimum);
        EXPECT_EQ(left.values().findChunk(chunk)->max_, *maximum);
        EXPECT_EQ(left.values().findChunk(chunk)->nonZeroCount_, nonZeroCount);
    }
}

std::int32_t
bruteClearance(const ChunkedField2D<std::uint8_t> &occupancy, IRMath::ivec2 cell, int cap) {
    std::int64_t nearestSq = static_cast<std::int64_t>(cap) * cap;
    for (int yOffset = -cap; yOffset <= cap; ++yOffset) {
        for (int xOffset = -cap; xOffset <= cap; ++xOffset) {
            const std::int64_t candidateX = static_cast<std::int64_t>(cell.x) + xOffset;
            const std::int64_t candidateY = static_cast<std::int64_t>(cell.y) + yOffset;
            std::uint8_t occupied = 1;
            const bool inRange = candidateX >= std::numeric_limits<std::int32_t>::min() &&
                                 candidateX <= std::numeric_limits<std::int32_t>::max() &&
                                 candidateY >= std::numeric_limits<std::int32_t>::min() &&
                                 candidateY <= std::numeric_limits<std::int32_t>::max();
            const bool present = inRange && occupancy.getCell(
                                                {static_cast<std::int32_t>(candidateX),
                                                 static_cast<std::int32_t>(candidateY)},
                                                occupied
                                            );
            if (!present || occupied != 0) {
                const std::int64_t distanceSq = static_cast<std::int64_t>(xOffset) * xOffset +
                                                static_cast<std::int64_t>(yOffset) * yOffset;
                nearestSq = std::min(nearestSq, distanceSq);
            }
        }
    }
    return static_cast<std::int32_t>(nearestSq);
}

TEST(FieldClearanceDomainTest, ConstructorAndQueriesEnforceTheirBounds) {
    EXPECT_NO_THROW((void)FieldClearance{1});
    EXPECT_NO_THROW((void)FieldClearance{kMaxClearanceCells});
    EXPECT_THROW(FieldClearance(0), std::invalid_argument);
    EXPECT_THROW(FieldClearance(-1), std::invalid_argument);
    EXPECT_THROW(FieldClearance(kMaxClearanceCells + 1), std::invalid_argument);

    ChunkedField2D<std::uint8_t> occupancy;
    addFreeFieldChunk(occupancy, {0, 0});
    occupancy.setCell({3, 3}, 1);
    FieldClearance clearance(4);
    clearance.rebuild(occupancy);
    EXPECT_EQ(clearanceAt(clearance, {3, 3}), 0);
    EXPECT_TRUE(clearance.hasClearance({4, 3}, 0));
    EXPECT_FALSE(clearance.hasClearance({3, 3}, 0));
    EXPECT_FALSE(clearance.hasClearance({32, 3}, 0));
    EXPECT_THROW(clearance.hasClearance({4, 3}, -1), std::invalid_argument);
    EXPECT_THROW(clearance.hasClearance({4, 3}, 5), std::invalid_argument);
}

TEST(FieldClearanceCapTest, EmptyPresentAreaSaturatesExactlyAtCap) {
    ChunkedField2D<std::uint8_t> occupancy;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    FieldClearance clearance(8);
    clearance.rebuild(occupancy);
    EXPECT_EQ(clearanceAt(clearance, {0, 0}), 64);

    ChunkedField2D<std::uint8_t> empty;
    FieldClearance emptyClearance(8);
    emptyClearance.rebuild(empty);
    EXPECT_EQ(emptyClearance.values().chunkCount(), 0u);
}

TEST(FieldClearanceCapTest, MaximumCapSaturatesWithoutOverflow) {
    ChunkedField2D<std::uint8_t> occupancy;
    for (int y = -32; y <= 32; ++y) {
        for (int x = -32; x <= 32; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    FieldClearance clearance(kMaxClearanceCells);
    clearance.rebuild(occupancy);
    EXPECT_EQ(clearanceAt(clearance, {0, 0}), INT32_C(1048576));
    EXPECT_TRUE(clearance.hasClearance({0, 0}, kMaxClearanceCells));
}

TEST(FieldClearanceBoundaryTest, NeighbourObstacleStrictlyLowersEdgeClearance) {
    ChunkedField2D<std::uint8_t> freeNeighbour;
    addFreeFieldChunk(freeNeighbour, {0, 0});
    addFreeFieldChunk(freeNeighbour, {1, 0});
    FieldClearance freeClearance(8);
    freeClearance.rebuild(freeNeighbour);

    ChunkedField2D<std::uint8_t> occupiedNeighbour;
    addFreeFieldChunk(occupiedNeighbour, {0, 0});
    addFreeFieldChunk(occupiedNeighbour, {1, 0});
    occupiedNeighbour.setCell({33, 16}, 1);
    FieldClearance occupiedClearance(8);
    occupiedClearance.rebuild(occupiedNeighbour);

    EXPECT_LT(clearanceAt(occupiedClearance, {31, 16}), clearanceAt(freeClearance, {31, 16}));
    EXPECT_EQ(clearanceAt(occupiedClearance, {31, 16}), 4);
}

TEST(FieldClearanceBoundaryTest, AbsentAndOccupiedNeighboursAreEquallyConservative) {
    ChunkedField2D<std::uint8_t> absentNeighbour;
    addFreeFieldChunk(absentNeighbour, {0, 0});
    FieldClearance absentClearance(8);
    absentClearance.rebuild(absentNeighbour);

    ChunkedField2D<std::uint8_t> occupiedNeighbour;
    addFreeFieldChunk(occupiedNeighbour, {0, 0});
    addFreeFieldChunk(occupiedNeighbour, {1, 0});
    occupiedNeighbour.setCell({32, 16}, 1);
    FieldClearance occupiedClearance(8);
    occupiedClearance.rebuild(occupiedNeighbour);

    EXPECT_EQ(clearanceAt(absentClearance, {31, 16}), 1);
    EXPECT_EQ(clearanceAt(occupiedClearance, {31, 16}), 1);
}

TEST(FieldClearanceBoundaryTest, HandlesVerticalDiagonalAndNegativeCoordinates) {
    ChunkedField2D<std::uint8_t> occupancy;
    for (int y = -2; y <= -1; ++y) {
        for (int x = -2; x <= -1; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    occupancy.setCell({-33, -33}, 1);
    FieldClearance clearance(8);
    clearance.rebuild(occupancy);
    EXPECT_EQ(clearanceAt(clearance, {-32, -32}), 2);
    EXPECT_EQ(clearanceAt(clearance, {-33, -32}), 1);
    EXPECT_EQ(clearanceAt(clearance, {-32, -33}), 1);
}

TEST(FieldClearanceIncrementalTest, RemovalUsesTwoCapComputeAndOneCapWriteBack) {
    ChunkedField2D<std::uint8_t> occupancy;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -2; x <= 2; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    occupancy.setCell({31, 16}, 1);
    occupancy.setCell({41, 16}, 1);
    occupancy.setCell({49, 16}, 1);
    FieldClearance clearance(8);
    clearance.rebuild(occupancy);
    EXPECT_EQ(clearanceAt(clearance, {35, 16}), 16);
    EXPECT_EQ(clearanceAt(clearance, {46, 16}), 9);
    occupancy.update();

    occupancy.setCell({31, 16}, 0);
    std::vector<FieldChunkKey> dirty;
    occupancy.dirtyKeys(dirty);
    clearance.update(occupancy, dirty);
    EXPECT_EQ(clearanceAt(clearance, {35, 16}), 36);
    EXPECT_EQ(clearanceAt(clearance, {46, 16}), 9);
}

TEST(FieldClearanceIncrementalTest, SeededAdditionsRemovalsAndClearMatchFullRebuild) {
    ChunkedField2D<std::uint8_t> occupancy;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    FieldClearance incremental(7);
    FieldClearance full(7);
    incremental.rebuild(occupancy);
    occupancy.update();

    std::mt19937 rng(0x3161u);
    std::uniform_int_distribution<int> coordinate(-48, 47);
    const std::int32_t initialProbe = clearanceAt(incremental, {0, 0});

    occupancy.setCell({1, 0}, 1);
    std::vector<FieldChunkKey> dirty;
    occupancy.dirtyKeys(dirty);
    incremental.update(occupancy, dirty);
    full.rebuild(occupancy);
    expectSameField(incremental, full);
    const std::int32_t lowerProbe = clearanceAt(incremental, {0, 0});
    EXPECT_LT(lowerProbe, initialProbe);
    occupancy.update();

    occupancy.setCell({1, 0}, 0);
    occupancy.dirtyKeys(dirty);
    incremental.update(occupancy, dirty);
    full.rebuild(occupancy);
    expectSameField(incremental, full);
    const std::int32_t raisedProbe = clearanceAt(incremental, {0, 0});
    EXPECT_GT(raisedProbe, lowerProbe);
    occupancy.update();

    for (int batch = 0; batch < 24; ++batch) {
        for (int mutation = 0; mutation < 12; ++mutation) {
            occupancy.setCell(
                {coordinate(rng), coordinate(rng)},
                static_cast<std::uint8_t>((batch + mutation) % 3 == 0)
            );
        }
        occupancy.dirtyKeys(dirty);
        incremental.update(occupancy, dirty);
        full.rebuild(occupancy);
        expectSameField(incremental, full);
        std::vector<FieldChunkKey> stillDirty;
        occupancy.dirtyKeys(stillDirty);
        EXPECT_EQ(stillDirty, dirty);
        occupancy.update();
    }

    occupancy.clear();
    addFreeFieldChunk(occupancy, {0, 0});
    occupancy.setCell({5, 5}, 1);
    occupancy.dirtyKeys(dirty);
    incremental.update(occupancy, dirty);
    full.rebuild(occupancy);
    expectSameField(incremental, full);
    EXPECT_EQ(incremental.values().chunkCount(), occupancy.chunkCount());

    const std::int32_t stableValue = clearanceAt(incremental, {7, 7});
    incremental.update(occupancy, {});
    EXPECT_EQ(clearanceAt(incremental, {7, 7}), stableValue);
}

TEST(FieldClearanceIncrementalTest, SmallFieldMatchesIndependentOccupiedBoundaryOracle) {
    ChunkedField2D<std::uint8_t> occupancy;
    for (int y = -1; y <= 0; ++y) {
        for (int x = -1; x <= 0; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    occupancy.setCell({-20, -4}, 1);
    occupancy.setCell({7, 9}, 1);
    FieldClearance clearance(5);
    clearance.rebuild(occupancy);

    for (int y = -32; y < 32; ++y) {
        for (int x = -32; x < 32; ++x) {
            EXPECT_EQ(clearanceAt(clearance, {x, y}), bruteClearance(occupancy, {x, y}, 5));
        }
    }
}

TEST(FieldClearanceIncrementalTest, DistantFieldChunkRemainsByteIdentical) {
    ChunkedField2D<std::uint8_t> occupancy;
    addFreeFieldChunk(occupancy, {0, 0});
    addFreeFieldChunk(occupancy, {20, 0});
    FieldClearance clearance(4);
    clearance.rebuild(occupancy);
    occupancy.update();
    const auto before = clearance.values().findChunk({20, 0})->cells();
    std::array<std::int32_t, IRPrefab::Spatial::kFieldChunkCells> snapshot{};
    std::copy(before.begin(), before.end(), snapshot.begin());

    occupancy.setCell({5, 5}, 1);
    std::vector<FieldChunkKey> dirty;
    occupancy.dirtyKeys(dirty);
    clearance.update(occupancy, dirty);
    const auto after = clearance.values().findChunk({20, 0})->cells();
    EXPECT_TRUE(std::equal(snapshot.begin(), snapshot.end(), after.begin()));
}

TEST(FieldClearanceIncrementalTest, SparseDiagonalKeepsWindowsBounded) {
    ChunkedField2D<std::uint8_t> occupancy;
    for (int coordinate = 0; coordinate < 1000; ++coordinate) {
        addFreeFieldChunk(occupancy, {coordinate, coordinate});
    }
    FieldClearance clearance(1);
    EXPECT_NO_THROW(clearance.rebuild(occupancy));
    EXPECT_EQ(clearance.values().chunkCount(), 1000u);
}

TEST(FieldClearanceCoordinateTest, TranslationNearInt32LimitsDoesNotWrap) {
    ChunkedField2D<std::uint8_t> occupancy;
    const IRMath::ivec2 high{std::numeric_limits<std::int32_t>::max(), 0};
    const IRMath::ivec2 low{std::numeric_limits<std::int32_t>::min(), 0};
    occupancy.setCell(high, 0);
    occupancy.setCell(low, 0);
    FieldClearance clearance(4);
    clearance.rebuild(occupancy);
    EXPECT_EQ(clearanceAt(clearance, high), 1);
    EXPECT_EQ(clearanceAt(clearance, low), 1);
}

} // namespace
