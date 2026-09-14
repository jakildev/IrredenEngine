#include <gtest/gtest.h>

#include <irreden/spatial/chunked_field.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <vector>

namespace {

using IRPrefab::Spatial::ChunkedField2D;
using IRPrefab::Spatial::FieldChunkKey;
using IRPrefab::Spatial::fieldChunkLocal;
using IRPrefab::Spatial::fieldChunkLocalIndex;
using IRPrefab::Spatial::fieldChunkOf;
using IRPrefab::Spatial::kFieldChunkCells;
using IRPrefab::Spatial::packFieldChunkKey;
using IRPrefab::Spatial::unpackFieldChunkKey;

void expectCoord(IRMath::ivec2 actual, IRMath::ivec2 expected) {
    EXPECT_EQ(actual.x, expected.x);
    EXPECT_EQ(actual.y, expected.y);
}

TEST(ChunkedFieldMappingTest, NegativeCellsUseFloorChunksAndRowMajorLocals) {
    struct MappingCase {
        int cell;
        int fieldChunk;
        int local;
    };
    constexpr std::array cases{
        MappingCase{-33, -2, 31},
        MappingCase{-32, -1, 0},
        MappingCase{-1, -1, 31},
        MappingCase{0, 0, 0},
        MappingCase{31, 0, 31},
    };

    for (const MappingCase &mapping : cases) {
        expectCoord(
            fieldChunkOf({mapping.cell, mapping.cell}),
            {mapping.fieldChunk, mapping.fieldChunk}
        );
        expectCoord(fieldChunkLocal({mapping.cell, mapping.cell}), {mapping.local, mapping.local});
    }

    EXPECT_NE(-33 / 32, fieldChunkOf({-33, 0}).x);
    EXPECT_NE(-33 % 32, fieldChunkLocal({-33, 0}).x);
    EXPECT_EQ(-32 / 32, fieldChunkOf({-32, 0}).x);
    EXPECT_EQ(-32 % 32, fieldChunkLocal({-32, 0}).x);
    EXPECT_NE(-1 / 32, fieldChunkOf({-1, 0}).x);
    EXPECT_NE(-1 % 32, fieldChunkLocal({-1, 0}).x);
    EXPECT_EQ(0 / 32, fieldChunkOf({0, 0}).x);
    EXPECT_EQ(0 % 32, fieldChunkLocal({0, 0}).x);
    EXPECT_EQ(31 / 32, fieldChunkOf({31, 0}).x);
    EXPECT_EQ(31 % 32, fieldChunkLocal({31, 0}).x);
    EXPECT_EQ(fieldChunkLocalIndex({1, 2}), 65);

    ChunkedField2D<int> field;
    field.setCell({-1, -1}, 7);
    const auto *fieldChunk = field.findChunk({-1, -1});
    ASSERT_NE(fieldChunk, nullptr);
    EXPECT_EQ(fieldChunk->cells()[fieldChunkLocalIndex({31, 31})], 7);
}

TEST(ChunkedFieldKeyTest, PackingRoundTripsWithoutCollisions) {
    constexpr std::array<std::int32_t, 13> axes{
        0,
        1,
        -1,
        31,
        -31,
        32,
        -32,
        33,
        -33,
        65536,
        -65536,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
    };
    std::vector<FieldChunkKey> keys;

    for (std::int32_t y : axes) {
        for (std::int32_t x : axes) {
            const IRMath::ivec2 coord{x, y};
            expectCoord(unpackFieldChunkKey(packFieldChunkKey(coord)), coord);
            keys.push_back(packFieldChunkKey(coord));
        }
    }

    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(std::adjacent_find(keys.begin(), keys.end()), keys.end());
    EXPECT_EQ(packFieldChunkKey({1, 2}), UINT64_C(0x0000000200000001));
    EXPECT_EQ(packFieldChunkKey({-1, 0}), UINT64_C(0x00000000FFFFFFFF));
    EXPECT_EQ(packFieldChunkKey({0, -1}), UINT64_C(0xFFFFFFFF00000000));
}

TEST(ChunkedFieldSummaryTest, SeededMutationsMatchBruteForceRecount) {
    ChunkedField2D<int> field;
    std::map<FieldChunkKey, std::array<int, kFieldChunkCells>> reference;
    std::mt19937 rng(0x3160u);
    std::uniform_int_distribution<int> cellDistribution(-70, 70);
    std::uniform_int_distribution<int> valueDistribution(-3, 3);

    for (int i = 0; i < 4096; ++i) {
        const IRMath::ivec2 cell{cellDistribution(rng), cellDistribution(rng)};
        const int value = valueDistribution(rng);
        field.setCell(cell, value);
        reference[packFieldChunkKey(fieldChunkOf(cell))]
                 [fieldChunkLocalIndex(fieldChunkLocal(cell))] = value;
    }

    bool observedNonZero = false;
    for (const auto &[key, cells] : reference) {
        const auto *fieldChunk = field.findChunk(unpackFieldChunkKey(key));
        ASSERT_NE(fieldChunk, nullptr);
        const int nonZeroCount = static_cast<int>(
            std::count_if(cells.begin(), cells.end(), [](int value) { return value != 0; })
        );
        EXPECT_EQ(fieldChunk->nonZeroCount_, nonZeroCount);
        observedNonZero |= nonZeroCount > 0;
    }
    EXPECT_TRUE(observedNonZero);

    field.update();
    bool observedRange = false;
    for (const auto &[key, cells] : reference) {
        const auto *fieldChunk = field.findChunk(unpackFieldChunkKey(key));
        ASSERT_NE(fieldChunk, nullptr);
        const auto [minIt, maxIt] = std::minmax_element(cells.begin(), cells.end());
        const int nonZeroCount = static_cast<int>(
            std::count_if(cells.begin(), cells.end(), [](int value) { return value != 0; })
        );
        EXPECT_EQ(fieldChunk->min_, *minIt);
        EXPECT_EQ(fieldChunk->max_, *maxIt);
        EXPECT_EQ(fieldChunk->nonZeroCount_, nonZeroCount);
        observedRange |= *minIt != *maxIt;
    }
    EXPECT_TRUE(observedRange);
}

TEST(ChunkedFieldDirtyTest, ReportsExactlyMutatedFieldChunks) {
    ChunkedField2D<int> field;
    const std::array<IRMath::ivec2, 4> initialCells{
        IRMath::ivec2{1, 1},
        IRMath::ivec2{33, 1},
        IRMath::ivec2{-1, 1},
        IRMath::ivec2{1, -1},
    };
    for (IRMath::ivec2 cell : initialCells) {
        field.setCell(cell, 1);
    }
    field.update();

    const std::array<IRMath::ivec2, 3> mutations{
        IRMath::ivec2{2, 2},
        IRMath::ivec2{-2, 2},
        IRMath::ivec2{65, -1},
    };
    std::set<FieldChunkKey> mutatedKeys;
    for (IRMath::ivec2 cell : mutations) {
        field.setCell(cell, 2);
        mutatedKeys.insert(packFieldChunkKey(fieldChunkOf(cell)));
    }

    std::vector<FieldChunkKey> actual;
    field.dirtyKeys(actual);
    const std::vector<FieldChunkKey> expected(mutatedKeys.begin(), mutatedKeys.end());
    EXPECT_EQ(actual, expected);
}

TEST(ChunkedFieldDirtyTest, CoversCreationNoOpChangeClearAndRedirty) {
    ChunkedField2D<int> field;
    std::vector<FieldChunkKey> dirty;
    const IRMath::ivec2 firstCell{2, 3};
    const FieldChunkKey firstKey = packFieldChunkKey({0, 0});

    field.setCell(firstCell, 0);
    EXPECT_EQ(field.chunkCount(), 1u);
    field.dirtyKeys(dirty);
    EXPECT_EQ(dirty, std::vector<FieldChunkKey>{firstKey});

    field.update();
    field.dirtyKeys(dirty);
    EXPECT_TRUE(dirty.empty());
    field.setCell(firstCell, 0);
    field.dirtyKeys(dirty);
    EXPECT_TRUE(dirty.empty());

    field.setCell(firstCell, 5);
    field.dirtyKeys(dirty);
    EXPECT_EQ(dirty, std::vector<FieldChunkKey>{firstKey});
    field.update();
    field.dirtyKeys(dirty);
    EXPECT_TRUE(dirty.empty());
    const auto *fieldChunk = field.findChunk({0, 0});
    ASSERT_NE(fieldChunk, nullptr);
    EXPECT_EQ(fieldChunk->min_, 0);
    EXPECT_EQ(fieldChunk->max_, 5);

    field.setCell({33, 2}, 8);
    field.update();
    std::vector<FieldChunkKey> liveKeys;
    field.chunkKeys(liveKeys);
    ASSERT_EQ(liveKeys.size(), 2u);
    field.clear();
    field.dirtyKeys(dirty);
    EXPECT_EQ(dirty, liveKeys);
    EXPECT_EQ(field.chunkCount(), 0u);

    field.update();
    field.dirtyKeys(dirty);
    EXPECT_TRUE(dirty.empty());
    field.setCell(firstCell, 6);
    field.dirtyKeys(dirty);
    EXPECT_EQ(dirty, std::vector<FieldChunkKey>{firstKey});
}

TEST(ChunkedFieldDirtyTest, DeduplicatesAKeyRecreatedBeforeUpdate) {
    ChunkedField2D<int> field;
    const IRMath::ivec2 cell{4, 5};
    const FieldChunkKey key = packFieldChunkKey({0, 0});
    field.setCell(cell, 1);
    field.clear();
    field.setCell(cell, 2);

    std::vector<FieldChunkKey> dirty;
    field.dirtyKeys(dirty);
    EXPECT_EQ(dirty, std::vector<FieldChunkKey>{key});

    field.update();
    field.dirtyKeys(dirty);
    EXPECT_TRUE(dirty.empty());
    const auto *fieldChunk = field.findChunk({0, 0});
    ASSERT_NE(fieldChunk, nullptr);
    EXPECT_EQ(fieldChunk->max_, 2);
}

TEST(ChunkedFieldStorageTest, ClearRemovesPresenceAndReusesZeroedBuffers) {
    ChunkedField2D<int> field;
    const std::array<IRMath::ivec2, 3> fieldChunkCoords{
        IRMath::ivec2{-1, 0},
        IRMath::ivec2{0, 0},
        IRMath::ivec2{1, 0},
    };
    std::array<const int *, 3> originalBuffers{};

    for (std::size_t i = 0; i < fieldChunkCoords.size(); ++i) {
        const IRMath::ivec2 cell = fieldChunkCoords[i] * 32 + IRMath::ivec2{3, 4};
        field.setCell(cell, static_cast<int>(i) + 10);
        const auto *fieldChunk = field.findChunk(fieldChunkCoords[i]);
        ASSERT_NE(fieldChunk, nullptr);
        originalBuffers[i] = fieldChunk->cells().data();
    }

    field.clear();
    EXPECT_EQ(field.chunkCount(), 0u);
    std::vector<FieldChunkKey> keys;
    field.chunkKeys(keys);
    EXPECT_TRUE(keys.empty());
    for (IRMath::ivec2 coord : fieldChunkCoords) {
        EXPECT_EQ(field.findChunk(coord), nullptr);
        int value = -1;
        EXPECT_FALSE(field.getCell(coord * 32 + IRMath::ivec2{3, 4}, value));
        EXPECT_EQ(value, -1);
    }

    std::set<const int *> reusedBuffers;
    for (std::size_t i = 0; i < fieldChunkCoords.size(); ++i) {
        const IRMath::ivec2 writtenLocal{static_cast<int>(i) + 1, 7};
        const IRMath::ivec2 cell = fieldChunkCoords[i] * 32 + writtenLocal;
        field.setCell(cell, static_cast<int>(i) + 20);
        const auto *fieldChunk = field.findChunk(fieldChunkCoords[i]);
        ASSERT_NE(fieldChunk, nullptr);
        const int *buffer = fieldChunk->cells().data();
        EXPECT_NE(
            std::find(originalBuffers.begin(), originalBuffers.end(), buffer),
            originalBuffers.end()
        );
        EXPECT_TRUE(reusedBuffers.insert(buffer).second);
        for (int localIndex = 0; localIndex < kFieldChunkCells; ++localIndex) {
            const int expected =
                localIndex == fieldChunkLocalIndex(writtenLocal) ? static_cast<int>(i) + 20 : 0;
            EXPECT_EQ(fieldChunk->cells()[localIndex], expected);
        }
    }

    field.setCell({96, 0}, 30);
    const auto *newFieldChunk = field.findChunk({3, 0});
    ASSERT_NE(newFieldChunk, nullptr);
    EXPECT_EQ(reusedBuffers.count(newFieldChunk->cells().data()), 0u);
}

TEST(ChunkedFieldStorageTest, PresentAllZeroFieldChunkSurvivesUpdate) {
    ChunkedField2D<int> field;
    const IRMath::ivec2 cell{8, 9};
    field.setCell(cell, 1);
    field.setCell(cell, 0);
    const auto *before = field.findChunk({0, 0});
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->nonZeroCount_, 0);
    const std::size_t count = field.chunkCount();

    field.update();

    const auto *after = field.findChunk({0, 0});
    ASSERT_NE(after, nullptr);
    int value = -1;
    EXPECT_TRUE(field.getCell(cell, value));
    EXPECT_EQ(value, 0);
    EXPECT_EQ(field.chunkCount(), count);
    EXPECT_EQ(after->min_, 0);
    EXPECT_EQ(after->max_, 0);
}

} // namespace
