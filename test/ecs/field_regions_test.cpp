#include <gtest/gtest.h>

#include <irreden/spatial/field_clearance.hpp>
#include <irreden/spatial/field_regions.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <random>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using IRPrefab::Spatial::ChunkedField2D;
using IRPrefab::Spatial::FieldChunkKey;
using IRPrefab::Spatial::FieldClearance;
using IRPrefab::Spatial::FieldRegionId;
using IRPrefab::Spatial::FieldRegions;
using IRPrefab::Spatial::kFieldChunkEdge;
using IRPrefab::Spatial::kInvalidFieldRegion;
using IRPrefab::Spatial::packFieldChunkKey;
using IRPrefab::Spatial::unpackFieldChunkKey;

using Occupancy = ChunkedField2D<std::uint8_t>;

void addFreeFieldChunk(Occupancy &occupancy, IRMath::ivec2 chunk) {
    occupancy.setCell(chunk * kFieldChunkEdge, 0);
}

void addOccupiedFieldChunk(Occupancy &occupancy, IRMath::ivec2 chunk) {
    const IRMath::ivec2 origin = chunk * kFieldChunkEdge;
    for (int y = 0; y < kFieldChunkEdge; ++y) {
        for (int x = 0; x < kFieldChunkEdge; ++x) {
            occupancy.setCell(origin + IRMath::ivec2{x, y}, 1);
        }
    }
}

void updateFromDirty(FieldRegions &regions, Occupancy &occupancy) {
    std::vector<FieldChunkKey> dirty;
    occupancy.dirtyKeys(dirty);
    regions.update(occupancy, dirty);
    occupancy.update();
}

std::vector<IRMath::ivec2> presentCells(const Occupancy &occupancy) {
    std::vector<FieldChunkKey> keys;
    occupancy.chunkKeys(keys);
    std::vector<IRMath::ivec2> cells;
    cells.reserve(keys.size() * IRPrefab::Spatial::kFieldChunkCells);
    for (FieldChunkKey key : keys) {
        const IRMath::ivec2 origin = unpackFieldChunkKey(key) * kFieldChunkEdge;
        for (int y = 0; y < kFieldChunkEdge; ++y) {
            for (int x = 0; x < kFieldChunkEdge; ++x) {
                cells.push_back(origin + IRMath::ivec2{x, y});
            }
        }
    }
    return cells;
}

std::vector<IRMath::ivec2> freeCells(const Occupancy &occupancy) {
    std::vector<IRMath::ivec2> cells;
    for (IRMath::ivec2 cell : presentCells(occupancy)) {
        std::uint8_t value = 1;
        if (occupancy.getCell(cell, value) && value == 0) {
            cells.push_back(cell);
        }
    }
    return cells;
}

std::set<FieldRegionId>
distinctLabels(const FieldRegions &regions, const std::vector<IRMath::ivec2> &cells) {
    std::set<FieldRegionId> labels;
    for (IRMath::ivec2 cell : cells) {
        labels.insert(regions.labelAt(cell));
    }
    return labels;
}

std::size_t countLabel(
    const FieldRegions &regions, const std::vector<IRMath::ivec2> &cells, FieldRegionId label
) {
    return static_cast<std::size_t>(
        std::count_if(cells.begin(), cells.end(), [&](IRMath::ivec2 cell) {
            return regions.labelAt(cell) == label;
        })
    );
}

// Whole-field 4-neighbour flood fill over present free cells, independent of
// the field-chunk decomposition. Labels are 1-based; absent/occupied cells are
// not in the map.
class BfsOracle {
  public:
    explicit BfsOracle(const Occupancy &occupancy) {
        int nextLabel = 0;
        std::deque<IRMath::ivec2> frontier;
        for (IRMath::ivec2 seed : freeCells(occupancy)) {
            if (m_labels.contains(packFieldChunkKey(seed))) {
                continue;
            }
            const int label = ++nextLabel;
            m_labels.emplace(packFieldChunkKey(seed), label);
            frontier.push_back(seed);
            while (!frontier.empty()) {
                const IRMath::ivec2 cell = frontier.front();
                frontier.pop_front();
                for (const IRMath::ivec2 step :
                     {IRMath::ivec2{1, 0},
                      IRMath::ivec2{-1, 0},
                      IRMath::ivec2{0, 1},
                      IRMath::ivec2{0, -1}}) {
                    const std::int64_t nx = static_cast<std::int64_t>(cell.x) + step.x;
                    const std::int64_t ny = static_cast<std::int64_t>(cell.y) + step.y;
                    if (!std::in_range<std::int32_t>(nx) || !std::in_range<std::int32_t>(ny)) {
                        continue;
                    }
                    const IRMath::ivec2 next{
                        static_cast<std::int32_t>(nx),
                        static_cast<std::int32_t>(ny)
                    };
                    std::uint8_t value = 1;
                    if (!occupancy.getCell(next, value) || value != 0 ||
                        m_labels.contains(packFieldChunkKey(next))) {
                        continue;
                    }
                    m_labels.emplace(packFieldChunkKey(next), label);
                    frontier.push_back(next);
                }
            }
        }
    }

    FieldRegionId labelAt(IRMath::ivec2 cell) const {
        const auto it = m_labels.find(packFieldChunkKey(cell));
        return it == m_labels.end() ? kInvalidFieldRegion : static_cast<FieldRegionId>(it->second);
    }

  private:
    std::unordered_map<std::uint64_t, int> m_labels;
};

// Two labellings describe the same partition when invalid cells agree and the
// valid labels are related by a bijection. Raw id equality is deliberately not
// asserted: ids are epoch-scoped.
template <typename LabelA, typename LabelB>
void expectSamePartition(
    const std::vector<IRMath::ivec2> &cells, const LabelA &labelA, const LabelB &labelB
) {
    std::unordered_map<FieldRegionId, FieldRegionId> forward;
    std::unordered_map<FieldRegionId, FieldRegionId> backward;
    for (IRMath::ivec2 cell : cells) {
        const FieldRegionId a = labelA(cell);
        const FieldRegionId b = labelB(cell);
        ASSERT_EQ(a == kInvalidFieldRegion, b == kInvalidFieldRegion)
            << "validity differs at (" << cell.x << ", " << cell.y << ")";
        if (a == kInvalidFieldRegion) {
            continue;
        }
        const auto [forwardIt, forwardInserted] = forward.try_emplace(a, b);
        const auto [backwardIt, backwardInserted] = backward.try_emplace(b, a);
        ASSERT_EQ(forwardIt->second, b) << "label " << a << " maps to two labels";
        ASSERT_EQ(backwardIt->second, a) << "label " << b << " has two preimages";
    }
}

void expectMatchesOracleAndRebuild(const FieldRegions &incremental, const Occupancy &occupancy) {
    const BfsOracle oracle(occupancy);
    FieldRegions full;
    full.rebuild(occupancy);
    const std::vector<IRMath::ivec2> cells = presentCells(occupancy);
    const auto incrementalLabel = [&](IRMath::ivec2 cell) { return incremental.labelAt(cell); };
    const auto fullLabel = [&](IRMath::ivec2 cell) { return full.labelAt(cell); };
    const auto oracleLabel = [&](IRMath::ivec2 cell) { return oracle.labelAt(cell); };
    expectSamePartition(cells, incrementalLabel, oracleLabel);
    expectSamePartition(cells, fullLabel, oracleLabel);
    expectSamePartition(cells, incrementalLabel, fullLabel);
    EXPECT_EQ(incremental.chunkCount(), occupancy.chunkCount());
    EXPECT_EQ(full.chunkCount(), occupancy.chunkCount());
}

Occupancy threeChunkL() {
    Occupancy occupancy;
    addFreeFieldChunk(occupancy, {0, 0});
    addFreeFieldChunk(occupancy, {1, 0});
    addFreeFieldChunk(occupancy, {0, 1});
    return occupancy;
}

TEST(FieldRegionsSeamTest, LShapeIsOneRegionAndWallSplitsItInTwo) {
    Occupancy occupancy = threeChunkL();
    FieldRegions regions;
    updateFromDirty(regions, occupancy);

    const std::vector<IRMath::ivec2> allCells = presentCells(occupancy);
    ASSERT_EQ(allCells.size(), 3072u);
    const std::set<FieldRegionId> joined = distinctLabels(regions, allCells);
    ASSERT_EQ(joined.size(), 1u);
    EXPECT_NE(*joined.begin(), kInvalidFieldRegion);

    for (int y = 0; y < 2 * kFieldChunkEdge; ++y) {
        occupancy.setCell({16, y}, 1);
    }
    updateFromDirty(regions, occupancy);
    const std::vector<IRMath::ivec2> remaining = freeCells(occupancy);
    ASSERT_EQ(remaining.size(), 3008u);
    const std::set<FieldRegionId> split = distinctLabels(regions, remaining);
    ASSERT_EQ(split.size(), 2u);
    EXPECT_FALSE(split.contains(kInvalidFieldRegion));
    const FieldRegionId west = regions.labelAt({8, 8});
    const FieldRegionId east = regions.labelAt({24, 8});
    EXPECT_NE(west, east);
    EXPECT_EQ(countLabel(regions, remaining, west), 1024u);
    EXPECT_EQ(countLabel(regions, remaining, east), 1984u);
    EXPECT_EQ(regions.labelAt({16, 8}), kInvalidFieldRegion);
    EXPECT_TRUE(regions.sameRegion({8, 31}, {8, 32}));
    EXPECT_TRUE(regions.sameRegion({24, 31}, {24, 32}));
    EXPECT_TRUE(regions.sameRegion({24, 8}, {40, 8}));
    EXPECT_FALSE(regions.sameRegion({8, 40}, {24, 40}));

    for (int y = 0; y < 2 * kFieldChunkEdge; ++y) {
        occupancy.setCell({16, y}, 0);
    }
    updateFromDirty(regions, occupancy);
    EXPECT_EQ(distinctLabels(regions, allCells).size(), 1u);
    EXPECT_TRUE(regions.sameRegion({8, 40}, {24, 40}));
}

TEST(FieldRegionsConnectivityTest, DiagonalTouchesStayApartAndOrthogonalBridgeJoins) {
    const std::vector<std::pair<IRMath::ivec2, IRMath::ivec2>> diagonalPairs{
        {{10, 10}, {11, 11}},
        {{31, 10}, {32, 11}},
        {{10, 31}, {11, 32}},
        {{31, 31}, {32, 32}},
    };
    for (const auto &[a, b] : diagonalPairs) {
        Occupancy occupancy;
        for (int y = 0; y <= 1; ++y) {
            for (int x = 0; x <= 1; ++x) {
                addOccupiedFieldChunk(occupancy, {x, y});
            }
        }
        occupancy.setCell(a, 0);
        occupancy.setCell(b, 0);
        FieldRegions regions;
        regions.rebuild(occupancy);
        EXPECT_NE(regions.labelAt(a), kInvalidFieldRegion);
        EXPECT_NE(regions.labelAt(b), kInvalidFieldRegion);
        EXPECT_NE(regions.labelAt(a), regions.labelAt(b))
            << "diagonal pair (" << a.x << ", " << a.y << ")/(" << b.x << ", " << b.y << ")";
        EXPECT_FALSE(regions.sameRegion(a, b));
        EXPECT_EQ(distinctLabels(regions, freeCells(occupancy)).size(), 2u);
    }

    Occupancy bridged;
    for (int y = 0; y <= 1; ++y) {
        for (int x = 0; x <= 1; ++x) {
            addOccupiedFieldChunk(bridged, {x, y});
        }
    }
    bridged.setCell({31, 10}, 0);
    bridged.setCell({32, 11}, 0);
    bridged.setCell({31, 11}, 0);
    FieldRegions regions;
    regions.rebuild(bridged);
    EXPECT_TRUE(regions.sameRegion({31, 10}, {32, 11}));
    EXPECT_EQ(distinctLabels(regions, freeCells(bridged)).size(), 1u);
}

TEST(FieldRegionsInvalidTest, ZeroIsNeverAReachableRegion) {
    FieldRegions empty;
    EXPECT_EQ(empty.labelAt({0, 0}), kInvalidFieldRegion);
    EXPECT_FALSE(empty.sameRegion({0, 0}, {0, 0}));
    EXPECT_EQ(empty.chunkCount(), 0u);

    Occupancy noChunks;
    FieldRegions emptyField;
    emptyField.rebuild(noChunks);
    EXPECT_EQ(emptyField.labelAt({5, 5}), kInvalidFieldRegion);
    EXPECT_FALSE(emptyField.sameRegion({5, 5}, {5, 5}));

    Occupancy allOccupied;
    addOccupiedFieldChunk(allOccupied, {0, 0});
    FieldRegions occupiedLayer;
    occupiedLayer.rebuild(allOccupied);
    EXPECT_EQ(occupiedLayer.chunkCount(), 1u);
    EXPECT_EQ(
        distinctLabels(occupiedLayer, presentCells(allOccupied)),
        std::set<FieldRegionId>{kInvalidFieldRegion}
    );

    Occupancy occupancy;
    addFreeFieldChunk(occupancy, {0, 0});
    occupancy.setCell({3, 3}, 1);
    occupancy.setCell({3, 4}, 255);
    occupancy.setCell({20, 20}, 1);
    FieldRegions regions;
    regions.rebuild(occupancy);
    const IRMath::ivec2 occupied{3, 3};
    const IRMath::ivec2 occupiedByte255{3, 4};
    const IRMath::ivec2 otherOccupied{20, 20};
    const IRMath::ivec2 absent{40, 3};
    const IRMath::ivec2 otherAbsent{-1, 3};
    const IRMath::ivec2 freeCell{5, 5};
    const IRMath::ivec2 connectedFree{6, 5};

    EXPECT_EQ(regions.labelAt(occupied), kInvalidFieldRegion);
    EXPECT_EQ(regions.labelAt(occupiedByte255), kInvalidFieldRegion);
    EXPECT_EQ(regions.labelAt(absent), kInvalidFieldRegion);
    EXPECT_NE(regions.labelAt(freeCell), kInvalidFieldRegion);

    EXPECT_FALSE(regions.sameRegion(occupied, occupied));
    EXPECT_FALSE(regions.sameRegion(occupied, otherOccupied));
    EXPECT_FALSE(regions.sameRegion(occupied, occupiedByte255));
    EXPECT_FALSE(regions.sameRegion(absent, absent));
    EXPECT_FALSE(regions.sameRegion(absent, otherAbsent));
    EXPECT_FALSE(regions.sameRegion(occupied, absent));
    EXPECT_FALSE(regions.sameRegion(absent, occupied));
    EXPECT_FALSE(regions.sameRegion(occupied, freeCell));
    EXPECT_FALSE(regions.sameRegion(freeCell, occupied));
    EXPECT_FALSE(regions.sameRegion(absent, freeCell));
    EXPECT_FALSE(regions.sameRegion(freeCell, absent));
    EXPECT_TRUE(regions.sameRegion(freeCell, freeCell));
    EXPECT_TRUE(regions.sameRegion(freeCell, connectedFree));
}

TEST(FieldRegionsInteriorTest, EnclosedRegionGetsItsOwnLabel) {
    Occupancy occupancy;
    addFreeFieldChunk(occupancy, {0, 0});
    for (int i = 10; i <= 20; ++i) {
        occupancy.setCell({i, 10}, 1);
        occupancy.setCell({i, 20}, 1);
        occupancy.setCell({10, i}, 1);
        occupancy.setCell({20, i}, 1);
    }
    FieldRegions regions;
    regions.rebuild(occupancy);

    const std::vector<IRMath::ivec2> free = freeCells(occupancy);
    ASSERT_EQ(free.size(), 984u);
    const std::set<FieldRegionId> labels = distinctLabels(regions, free);
    ASSERT_EQ(labels.size(), 2u);
    EXPECT_FALSE(labels.contains(kInvalidFieldRegion));
    const FieldRegionId exterior = regions.labelAt({0, 0});
    const FieldRegionId interior = regions.labelAt({15, 15});
    EXPECT_NE(exterior, interior);
    EXPECT_EQ(countLabel(regions, free, exterior), 903u);
    EXPECT_EQ(countLabel(regions, free, interior), 81u);
    for (int y = 11; y <= 19; ++y) {
        for (int x = 11; x <= 19; ++x) {
            EXPECT_EQ(regions.labelAt({x, y}), interior);
        }
    }
    EXPECT_FALSE(regions.sameRegion({15, 15}, {0, 0}));
}

TEST(FieldRegionsCanonicalTest, IdsArePinnedRegardlessOfInsertionOrder) {
    const std::vector<IRMath::ivec2> chunks{{0, 0}, {-1, 0}, {1, 0}, {0, 1}, {0, -1}};
    const std::vector<IRMath::ivec2>
        free{{2, 2}, {4, 2}, {0, 6}, {-1, 6}, {31, 6}, {32, 6}, {2, 34}, {2, -30}};
    const std::vector<std::pair<IRMath::ivec2, FieldRegionId>> expected{
        {{2, 2}, 1},
        {{4, 2}, 2},
        {{0, 6}, 3},
        {{-1, 6}, 3},
        {{31, 6}, 4},
        {{32, 6}, 4},
        {{2, 34}, 5},
        {{2, -30}, 6},
    };

    std::vector<std::vector<IRMath::ivec2>> chunkOrders{chunks, chunks, chunks};
    std::vector<std::vector<IRMath::ivec2>> freeOrders{free, free, free};
    std::reverse(chunkOrders[1].begin(), chunkOrders[1].end());
    std::reverse(freeOrders[1].begin(), freeOrders[1].end());
    std::mt19937 rng(0xC4u);
    std::shuffle(chunkOrders[2].begin(), chunkOrders[2].end(), rng);
    std::shuffle(freeOrders[2].begin(), freeOrders[2].end(), rng);

    for (std::size_t order = 0; order < chunkOrders.size(); ++order) {
        Occupancy occupancy;
        for (IRMath::ivec2 chunk : chunkOrders[order]) {
            addOccupiedFieldChunk(occupancy, chunk);
        }
        for (IRMath::ivec2 cell : freeOrders[order]) {
            occupancy.setCell(cell, 0);
        }
        FieldRegions regions;
        regions.rebuild(occupancy);
        for (const auto &[cell, id] : expected) {
            EXPECT_EQ(regions.labelAt(cell), id)
                << "order " << order << " cell (" << cell.x << ", " << cell.y << ")";
        }
        FieldRegions incremental;
        Occupancy history;
        for (IRMath::ivec2 chunk : chunkOrders[order]) {
            addOccupiedFieldChunk(history, chunk);
            updateFromDirty(incremental, history);
        }
        for (IRMath::ivec2 cell : freeOrders[order]) {
            history.setCell(cell, 0);
            updateFromDirty(incremental, history);
        }
        for (const auto &[cell, id] : expected) {
            EXPECT_EQ(incremental.labelAt(cell), id)
                << "incremental order " << order << " cell (" << cell.x << ", " << cell.y << ")";
        }
    }
}

TEST(FieldRegionsIncrementalTest, SeamBridgeSplitsAndMerges) {
    Occupancy occupancy;
    addOccupiedFieldChunk(occupancy, {0, 0});
    addOccupiedFieldChunk(occupancy, {1, 0});
    for (int x = 20; x <= 43; ++x) {
        occupancy.setCell({x, 5}, 0);
    }
    FieldRegions regions;
    updateFromDirty(regions, occupancy);
    EXPECT_TRUE(regions.sameRegion({20, 5}, {43, 5}));
    expectMatchesOracleAndRebuild(regions, occupancy);

    occupancy.setCell({32, 5}, 1);
    updateFromDirty(regions, occupancy);
    EXPECT_FALSE(regions.sameRegion({20, 5}, {43, 5}));
    EXPECT_TRUE(regions.sameRegion({20, 5}, {31, 5}));
    EXPECT_TRUE(regions.sameRegion({33, 5}, {43, 5}));
    expectMatchesOracleAndRebuild(regions, occupancy);

    occupancy.setCell({31, 5}, 1);
    updateFromDirty(regions, occupancy);
    EXPECT_FALSE(regions.sameRegion({20, 5}, {43, 5}));
    expectMatchesOracleAndRebuild(regions, occupancy);

    occupancy.setCell({32, 5}, 0);
    updateFromDirty(regions, occupancy);
    EXPECT_FALSE(regions.sameRegion({20, 5}, {43, 5}));
    expectMatchesOracleAndRebuild(regions, occupancy);

    occupancy.setCell({31, 5}, 0);
    updateFromDirty(regions, occupancy);
    EXPECT_TRUE(regions.sameRegion({20, 5}, {43, 5}));
    expectMatchesOracleAndRebuild(regions, occupancy);
}

TEST(FieldRegionsIncrementalTest, SeededMutationsMatchRebuildAndOracleThroughLifecycle) {
    Occupancy occupancy;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    occupancy.update();

    FieldRegions regions;
    regions.update(occupancy, {});
    expectMatchesOracleAndRebuild(regions, occupancy);

    std::mt19937 rng(0x3162u);
    std::uniform_int_distribution<int> coordinate(-48, 47);
    for (int batch = 0; batch < 32; ++batch) {
        for (int mutation = 0; mutation < 40; ++mutation) {
            occupancy.setCell(
                {coordinate(rng), coordinate(rng)},
                static_cast<std::uint8_t>((batch + mutation) % 3 != 0)
            );
        }
        std::vector<FieldChunkKey> dirty;
        occupancy.dirtyKeys(dirty);
        regions.update(occupancy, dirty);
        std::vector<FieldChunkKey> stillDirty;
        occupancy.dirtyKeys(stillDirty);
        EXPECT_EQ(stillDirty, dirty);
        expectMatchesOracleAndRebuild(regions, occupancy);
        occupancy.update();
    }

    regions.update(occupancy, {});
    expectMatchesOracleAndRebuild(regions, occupancy);

    addFreeFieldChunk(occupancy, {7, -9});
    updateFromDirty(regions, occupancy);
    EXPECT_NE(regions.labelAt({7 * kFieldChunkEdge, -9 * kFieldChunkEdge}), kInvalidFieldRegion);
    expectMatchesOracleAndRebuild(regions, occupancy);

    occupancy.clear();
    addFreeFieldChunk(occupancy, {0, 0});
    occupancy.setCell({5, 5}, 1);
    updateFromDirty(regions, occupancy);
    EXPECT_EQ(regions.chunkCount(), 1u);
    EXPECT_EQ(regions.labelAt({40, 0}), kInvalidFieldRegion);
    EXPECT_EQ(regions.labelAt({7 * kFieldChunkEdge, -9 * kFieldChunkEdge}), kInvalidFieldRegion);
    expectMatchesOracleAndRebuild(regions, occupancy);

    occupancy.clear();
    addFreeFieldChunk(occupancy, {0, 0});
    for (int y = 0; y < kFieldChunkEdge; ++y) {
        occupancy.setCell({16, y}, 1);
    }
    updateFromDirty(regions, occupancy);
    EXPECT_FALSE(regions.sameRegion({8, 8}, {24, 8}));
    EXPECT_EQ(regions.labelAt({5, 5}), regions.labelAt({8, 8}));
    expectMatchesOracleAndRebuild(regions, occupancy);
}

TEST(FieldRegionsIncrementalTest, CheckerboardHasOneComponentPerFreeCell) {
    Occupancy occupancy;
    addFreeFieldChunk(occupancy, {0, 0});
    for (int y = 0; y < kFieldChunkEdge; ++y) {
        for (int x = 0; x < kFieldChunkEdge; ++x) {
            occupancy.setCell({x, y}, static_cast<std::uint8_t>((x + y) % 2));
        }
    }
    FieldRegions regions;
    updateFromDirty(regions, occupancy);
    const std::vector<IRMath::ivec2> free = freeCells(occupancy);
    ASSERT_EQ(free.size(), 512u);
    const std::set<FieldRegionId> labels = distinctLabels(regions, free);
    EXPECT_EQ(labels.size(), 512u);
    EXPECT_FALSE(labels.contains(kInvalidFieldRegion));
    EXPECT_EQ(*labels.rbegin(), 512u);
    expectMatchesOracleAndRebuild(regions, occupancy);

    addFreeFieldChunk(occupancy, {1, 0});
    updateFromDirty(regions, occupancy);
    EXPECT_TRUE(regions.sameRegion({31, 1}, {32, 1}));
    EXPECT_FALSE(regions.sameRegion({31, 1}, {29, 1}));
    expectMatchesOracleAndRebuild(regions, occupancy);
}

TEST(FieldRegionsCoordinateTest, NegativeSeamsAndDistantIslands) {
    Occupancy occupancy;
    for (int y = -2; y <= -1; ++y) {
        for (int x = -2; x <= -1; ++x) {
            addFreeFieldChunk(occupancy, {x, y});
        }
    }
    addFreeFieldChunk(occupancy, {1000, -1000});
    addFreeFieldChunk(occupancy, {-1000, 1000});
    FieldRegions regions;
    updateFromDirty(regions, occupancy);
    EXPECT_TRUE(regions.sameRegion({-33, -33}, {-32, -32}));
    EXPECT_TRUE(regions.sameRegion({-64, -1}, {-1, -64}));
    EXPECT_EQ(distinctLabels(regions, presentCells(occupancy)).size(), 3u);
    EXPECT_FALSE(regions.sameRegion({-1, -1}, {1000 * kFieldChunkEdge, -1000 * kFieldChunkEdge}));
    EXPECT_FALSE(regions.sameRegion(
        {1000 * kFieldChunkEdge, -1000 * kFieldChunkEdge},
        {-1000 * kFieldChunkEdge, 1000 * kFieldChunkEdge}
    ));
    expectMatchesOracleAndRebuild(regions, occupancy);

    for (int y = -64; y < 0; ++y) {
        occupancy.setCell({-32, y}, 1);
    }
    updateFromDirty(regions, occupancy);
    EXPECT_FALSE(regions.sameRegion({-33, -33}, {-31, -32}));
    EXPECT_TRUE(regions.sameRegion({-33, -33}, {-33, -1}));
    expectMatchesOracleAndRebuild(regions, occupancy);
}

TEST(FieldRegionsCoordinateTest, Int32LimitsDoNotWrapNeighbours) {
    Occupancy occupancy;
    const IRMath::ivec2 high{std::numeric_limits<std::int32_t>::max(), 0};
    const IRMath::ivec2 low{std::numeric_limits<std::int32_t>::min(), 0};
    const IRMath::ivec2 highY{0, std::numeric_limits<std::int32_t>::max()};
    const IRMath::ivec2 lowY{0, std::numeric_limits<std::int32_t>::min()};
    occupancy.setCell(high, 0);
    occupancy.setCell(low, 0);
    occupancy.setCell(highY, 0);
    occupancy.setCell(lowY, 0);
    FieldRegions regions;
    regions.rebuild(occupancy);
    EXPECT_NE(regions.labelAt(high), kInvalidFieldRegion);
    EXPECT_NE(regions.labelAt(low), kInvalidFieldRegion);
    EXPECT_FALSE(regions.sameRegion(high, low));
    EXPECT_FALSE(regions.sameRegion(highY, lowY));
    EXPECT_EQ(distinctLabels(regions, presentCells(occupancy)).size(), 4u);
    expectMatchesOracleAndRebuild(regions, occupancy);
}

TEST(FieldRegionsCompositionTest, OneSnapshotFeedsClearanceAndRegionsBeforeAcknowledgment) {
    Occupancy occupancy;
    addFreeFieldChunk(occupancy, {0, 0});
    addFreeFieldChunk(occupancy, {1, 0});
    for (int y = 0; y < kFieldChunkEdge; ++y) {
        occupancy.setCell({31, y}, 1);
    }
    FieldClearance clearance(4);
    FieldRegions regions;

    std::vector<FieldChunkKey> dirty;
    occupancy.dirtyKeys(dirty);
    ASSERT_EQ(dirty.size(), 2u);
    clearance.update(occupancy, dirty);
    regions.update(occupancy, dirty);
    std::vector<FieldChunkKey> stillDirty;
    occupancy.dirtyKeys(stillDirty);
    EXPECT_EQ(stillDirty, dirty);
    occupancy.update();
    occupancy.dirtyKeys(stillDirty);
    EXPECT_TRUE(stillDirty.empty());

    const IRMath::ivec2 wall{31, 16};
    const IRMath::ivec2 absent{64, 16};
    const IRMath::ivec2 west{20, 16};
    const IRMath::ivec2 east{40, 16};
    EXPECT_FALSE(clearance.hasClearance(wall, 0));
    EXPECT_EQ(regions.labelAt(wall), kInvalidFieldRegion);
    EXPECT_FALSE(regions.sameRegion(wall, wall));
    EXPECT_FALSE(clearance.hasClearance(absent, 0));
    EXPECT_EQ(regions.labelAt(absent), kInvalidFieldRegion);
    EXPECT_TRUE(clearance.hasClearance(west, 0));
    EXPECT_TRUE(clearance.hasClearance(east, 0));
    EXPECT_TRUE(regions.sameRegion(west, {21, 16}));
    EXPECT_FALSE(regions.sameRegion(west, east));

    occupancy.setCell(wall, 0);
    occupancy.dirtyKeys(dirty);
    ASSERT_EQ(dirty.size(), 1u);
    clearance.update(occupancy, dirty);
    regions.update(occupancy, dirty);
    occupancy.dirtyKeys(stillDirty);
    EXPECT_EQ(stillDirty, dirty);
    occupancy.update();
    EXPECT_TRUE(clearance.hasClearance(wall, 0));
    EXPECT_TRUE(clearance.hasClearance(wall, 1));
    EXPECT_FALSE(clearance.hasClearance(wall, 2));
    EXPECT_TRUE(regions.sameRegion(west, east));
    EXPECT_TRUE(regions.sameRegion(wall, east));
    expectMatchesOracleAndRebuild(regions, occupancy);
}

} // namespace
