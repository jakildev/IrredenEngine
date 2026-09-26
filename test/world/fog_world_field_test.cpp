#include <gtest/gtest.h>

#include <irreden/render/fog_world_field.hpp>
#include <irreden/spatial/chunked_field.hpp>
#include <irreden/world/field_chunk_persistence.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

using IRComponents::kFogStateExplored;
using IRComponents::kFogStateUnexplored;
using IRComponents::kFogStateVisible;
using IRPrefab::Fog::WorldField;
using IRPrefab::Fog::detail::expandWindowChunks;
using IRPrefab::Fog::detail::planWindowGather;
using IRPrefab::Fog::detail::WindowGatherPlan;
using IRPrefab::Fog::detail::WindowUploadRect;
using IRPrefab::Spatial::FieldChunkKey;
using IRPrefab::Spatial::fieldChunkOf;
using IRPrefab::Spatial::kFieldChunkEdge;
using IRPrefab::Spatial::packFieldChunkKey;
using IRWorld::FieldChunkDiskPersistence;

const IRMath::ivec2 kLegacyOrigin{-128, -128};
constexpr int kLegacyEdge = 256;

std::int64_t discCellCount(std::int64_t radius) {
    std::int64_t count = 0;
    for (std::int64_t dy = -radius; dy <= radius; ++dy) {
        for (std::int64_t dx = -radius; dx <= radius; ++dx) {
            count += dx * dx + dy * dy <= radius * radius ? 1 : 0;
        }
    }
    return count;
}

std::set<FieldChunkKey> discRegions(IRMath::ivec2 centre, int radius) {
    std::set<FieldChunkKey> regions;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                regions.insert(packFieldChunkKey(
                    FieldChunkDiskPersistence::regionOf(
                        fieldChunkOf(centre + IRMath::ivec2{dx, dy})
                    )
                ));
            }
        }
    }
    return regions;
}

// Expands the whole window at @p origin into @p image (edge² RGBA8 texels).
void expandWholeWindow(
    WorldField &field, IRMath::ivec2 origin, int edge, std::vector<std::uint8_t> &image
) {
    WindowGatherPlan plan;
    planWindowGather(std::nullopt, origin, edge, {}, plan);
    image.assign(static_cast<std::size_t>(edge) * static_cast<std::size_t>(edge) * 4, 0xAB);
    std::vector<std::uint8_t> strip;
    for (const WindowUploadRect &rect : plan.rects_) {
        strip.assign(static_cast<std::size_t>(rect.size_.x * rect.size_.y) * 4, 0xCD);
        expandWindowChunks(field, origin, rect, strip);
        for (int y = 0; y < rect.size_.y; ++y) {
            std::copy_n(
                strip.begin() + static_cast<std::ptrdiff_t>(y * rect.size_.x * 4),
                rect.size_.x * 4,
                image.begin() +
                    static_cast<std::ptrdiff_t>(((rect.texel_.y + y) * edge + rect.texel_.x) * 4)
            );
        }
    }
}

class FogWorldFieldTest : public ::testing::Test {
  protected:
    void SetUp() override {
        static std::atomic<std::uint64_t> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("ir-fog-world-field-" + std::to_string(stamp) + "-" +
                  std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(m_root);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    FieldChunkDiskPersistence store(const std::string &subdirectory = "") const {
        return *FieldChunkDiskPersistence::create(
            (m_root / subdirectory).string(),
            IRPrefab::Fog::kFogFieldLayer,
            IRPrefab::Fog::kFogFieldBytesPerCell
        );
    }

    void persist(WorldField &field, const std::string &subdirectory = "") const {
        ASSERT_TRUE(field.setPersistence(store(subdirectory)));
    }

    std::filesystem::path m_root;
};

TEST_F(FogWorldFieldTest, ExploredStateRoundTripsThroughEvictionAndReload) {
    const IRMath::ivec2 centre{5000, -7000};
    const int radius = 20;
    const std::set<FieldChunkKey> regions = discRegions(centre, radius);
    const auto expectDisc = [&](WorldField &field) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy <= radius * radius) {
                    ASSERT_EQ(field.getCell(centre + IRMath::ivec2{dx, dy}), kFogStateVisible);
                }
            }
        }
        EXPECT_EQ(field.getCell(centre + IRMath::ivec2{radius + 1, 0}), kFogStateUnexplored);
    };

    {
        WorldField field;
        persist(field);
        field.revealRadius(centre, radius);
        field.stats();
        EXPECT_EQ(field.evict({-4, -4}, {3, 3}), 0);
        EXPECT_EQ(field.evict({-4, -4}, {3, 3}), static_cast<int>(regions.size()));
        const IRPrefab::Fog::WorldFieldStats stats = field.stats();
        EXPECT_EQ(stats.evictions_, static_cast<int>(regions.size()));
        EXPECT_EQ(stats.saves_, static_cast<int>(regions.size()));
        EXPECT_EQ(stats.residentChunks_, 0);
        EXPECT_EQ(stats.residentRegions_, 0);
        const FieldChunkDiskPersistence disk = store();
        for (FieldChunkKey key : regions) {
            EXPECT_TRUE(disk.regionExists(IRPrefab::Spatial::unpackFieldChunkKey(key)));
        }
        expectDisc(field);
        EXPECT_EQ(field.stats().loads_, static_cast<int>(regions.size()));
    }

    WorldField fresh;
    persist(fresh);
    expectDisc(fresh);

    WorldField transient;
    transient.revealRadius(centre, radius);
    const int chunks = transient.stats().residentChunks_;
    EXPECT_EQ(transient.evict({-4, -4}, {3, 3}), 0);
    EXPECT_EQ(transient.evict({-4, -4}, {3, 3}), 0);
    EXPECT_EQ(transient.stats().residentChunks_, chunks);
    expectDisc(transient);
}

TEST_F(FogWorldFieldTest, RegionIsProbedOnce) {
    {
        WorldField field;
        persist(field);
        for (int y = 0; y < 512; ++y) {
            for (int x = 0; x < 512; ++x) {
                ASSERT_EQ(field.getCell({1024 + x, -512 + y}), kFogStateUnexplored);
            }
        }
        const IRPrefab::Fog::WorldFieldStats stats = field.stats();
        EXPECT_EQ(stats.probes_, 1);
        EXPECT_EQ(stats.loads_, 0);
        EXPECT_EQ(stats.residentRegions_, 1);
    }

    {
        WorldField writer;
        persist(writer);
        writer.revealRadius({0, 0}, 10);
        EXPECT_EQ(writer.flush(), 4);
    }

    WorldField field;
    persist(field);
    std::vector<std::uint8_t> image;
    expandWholeWindow(field, kLegacyOrigin, kLegacyEdge, image);
    IRPrefab::Fog::WorldFieldStats stats = field.stats();
    EXPECT_EQ(stats.probes_, 4);
    EXPECT_EQ(stats.loads_, 4);
    expandWholeWindow(field, kLegacyOrigin, kLegacyEdge, image);
    EXPECT_EQ(field.stats().probes_, 0);

    field.clear();
    expandWholeWindow(field, kLegacyOrigin, kLegacyEdge, image);
    stats = field.stats();
    EXPECT_EQ(stats.probes_, 4);
    EXPECT_EQ(stats.loads_, 0);
    EXPECT_TRUE(std::all_of(image.begin(), image.end(), [](std::uint8_t byte) {
        return byte == 0;
    }));
}

TEST_F(FogWorldFieldTest, HotFarRegionSurvivesEviction) {
    WorldField field;
    persist(field);
    const IRMath::ivec2 far{100000, 100000};
    field.setCell(far, kFogStateExplored);
    field.stats();
    for (int pass = 0; pass < 4; ++pass) {
        EXPECT_EQ(field.getCell(far), kFogStateExplored);
        EXPECT_EQ(field.evict({-4, -4}, {3, 3}), 0);
    }
    const IRPrefab::Fog::WorldFieldStats stats = field.stats();
    EXPECT_EQ(stats.probes_, 0);
    EXPECT_EQ(stats.evictions_, 0);
    EXPECT_EQ(stats.residentRegions_, 1);

    EXPECT_EQ(field.evict({-4, -4}, {3, 3}), 1);
    EXPECT_EQ(field.getCell(far), kFogStateExplored);
    EXPECT_EQ(field.stats().probes_, 1);
}

TEST_F(FogWorldFieldTest, WriteToAnEvictedRegionLoadsItFirst) {
    const IRMath::ivec2 first{600, 600};
    const IRMath::ivec2 second{1000, 1000};
    ASSERT_EQ(
        FieldChunkDiskPersistence::regionOf(fieldChunkOf(first)),
        FieldChunkDiskPersistence::regionOf(fieldChunkOf(second))
    );
    ASSERT_NE(fieldChunkOf(first), fieldChunkOf(second));
    {
        WorldField field;
        persist(field);
        field.setCell(first, kFogStateVisible);
        field.evict({-4, -4}, {3, 3});
        ASSERT_EQ(field.evict({-4, -4}, {3, 3}), 1);
        field.setCell(second, kFogStateExplored);
        field.evict({-4, -4}, {3, 3});
        ASSERT_EQ(field.evict({-4, -4}, {3, 3}), 1);
    }
    WorldField fresh;
    persist(fresh);
    EXPECT_EQ(fresh.getCell(first), kFogStateVisible);
    EXPECT_EQ(fresh.getCell(second), kFogStateExplored);
}

TEST_F(FogWorldFieldTest, ClearRemovesPersistedState) {
    const IRMath::ivec2 centre{-3000, 4000};
    {
        WorldField field;
        persist(field);
        field.revealRadius(centre, 5);
        EXPECT_GT(field.flush(), 0);
        field.clear();
        EXPECT_EQ(field.getCell(centre), kFogStateUnexplored);
    }
    EXPECT_FALSE(store().regionExists(FieldChunkDiskPersistence::regionOf(fieldChunkOf(centre))));
    WorldField fresh;
    persist(fresh);
    EXPECT_EQ(fresh.getCell(centre), kFogStateUnexplored);
}

TEST_F(FogWorldFieldTest, SetPersistenceRefusesAPopulatedField) {
    WorldField field;
    field.setCell({3, 3}, kFogStateVisible);
    EXPECT_FALSE(field.setPersistence(store()));
    EXPECT_FALSE(field.hasPersistence());

    WorldField empty;
    EXPECT_FALSE(empty.setCell({3, 3}, kFogStateUnexplored));
    EXPECT_TRUE(empty.setPersistence(store()));
}

TEST_F(FogWorldFieldTest, CoordinatesFarOutsideTheLegacyWindow) {
    WorldField field;
    field.setCell({1000000, -1000000}, kFogStateVisible);
    EXPECT_EQ(field.getCell({1000000, -1000000}), kFogStateVisible);

    const IRMath::ivec2 centre{50000, 50000};
    EXPECT_EQ(field.revealRadius(centre, 10), discCellCount(10));
    int visible = 0;
    for (int y = centre.y - 12; y <= centre.y + 12; ++y) {
        for (int x = centre.x - 12; x <= centre.x + 12; ++x) {
            visible += field.getCell({x, y}) == kFogStateVisible ? 1 : 0;
        }
    }
    EXPECT_EQ(visible, discCellCount(10));

    WorldField huge;
    EXPECT_EQ(
        huge.revealRadius({0, 0}, std::numeric_limits<int>::max()),
        discCellCount(IRPrefab::Fog::kFogRevealRadiusMax)
    );
    EXPECT_EQ(huge.getCell({IRPrefab::Fog::kFogRevealRadiusMax, 0}), kFogStateVisible);
    EXPECT_EQ(huge.getCell({IRPrefab::Fog::kFogRevealRadiusMax + 1, 0}), kFogStateUnexplored);

    constexpr std::int64_t kCellMax = std::numeric_limits<std::int32_t>::max();
    const IRMath::ivec2 edgeCentre{static_cast<int>(kCellMax - 5), 0};
    std::int64_t representable = 0;
    for (std::int64_t dy = -10; dy <= 10; ++dy) {
        for (std::int64_t dx = -10; dx <= 10; ++dx) {
            representable += dx * dx + dy * dy <= 100 && edgeCentre.x + dx <= kCellMax ? 1 : 0;
        }
    }
    WorldField edge;
    EXPECT_EQ(edge.revealRadius(edgeCentre, 10), representable);
    EXPECT_EQ(edge.getCell({static_cast<int>(kCellMax), 0}), kFogStateVisible);
}

TEST_F(FogWorldFieldTest, LegacyWindowExpansionMatchesTheFixedGridLayout) {
    WorldField field;
    field.revealRadius({0, 0}, 40);
    field.revealRadius({120, -120}, 30);
    field.setCell({-128, -128}, kFogStateExplored);
    field.setCell({127, 127}, kFogStateExplored);
    field.setCell({128, 0}, kFogStateVisible);
    field.setCell({0, -129}, kFogStateVisible);

    std::vector<std::uint8_t> legacy(static_cast<std::size_t>(kLegacyEdge * kLegacyEdge), 0);
    const auto legacyReveal = [&](int cx, int cy, int radius) {
        for (int y = -128; y < 128; ++y) {
            for (int x = -128; x < 128; ++x) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius) {
                    legacy[static_cast<std::size_t>((y + 128) * kLegacyEdge + x + 128)] =
                        kFogStateVisible;
                }
            }
        }
    };
    legacyReveal(0, 0, 40);
    legacyReveal(120, -120, 30);
    legacy[0] = kFogStateExplored;
    legacy[static_cast<std::size_t>(kLegacyEdge * kLegacyEdge - 1)] = kFogStateExplored;

    std::vector<std::uint8_t> image;
    expandWholeWindow(field, kLegacyOrigin, kLegacyEdge, image);
    for (std::size_t i = 0; i < legacy.size(); ++i) {
        ASSERT_EQ(image[i * 4], legacy[i]) << "texel " << i;
        ASSERT_EQ(image[i * 4 + 1], 0);
        ASSERT_EQ(image[i * 4 + 2], 0);
        ASSERT_EQ(image[i * 4 + 3], 0);
    }
}

// The cold whole-window gather over a populated save, at every window edge
// the camera-anchored window can take. Counts are the oracle; elapsed times
// are printed for the budget check, never asserted.
TEST_F(FogWorldFieldTest, ColdWholeWindowGather) {
    struct Row {
        int edge_;
        bool dense_;
        int regions_;
    };
    constexpr std::array rows{
        Row{1152, false, 16},
        Row{1472, false, 16},
        Row{1856, false, 25},
        Row{2496, false, 36},
        Row{3200, false, 64},
        Row{4096, false, 81},
        Row{1152, true, 16},
        Row{4096, true, 81},
    };
    // Region-local index 15 on both axes: the worst-case alignment.
    const IRMath::ivec2 originChunk{
        2 * IRWorld::kFieldRegionEdgeChunks + 15,
        -IRWorld::kFieldRegionEdgeChunks + 15
    };
    const IRMath::ivec2 origin = originChunk * kFieldChunkEdge;
    const auto chunkValue = [](IRMath::ivec2 chunk, int localY) {
        return static_cast<std::uint8_t>(
            1 + ((chunk.x * 7 + chunk.y * 13 + localY * 3) & 0xff) % 254
        );
    };

    for (const Row &row : rows) {
        const std::string subdirectory =
            std::to_string(row.edge_) + (row.dense_ ? "-dense" : "-sparse");
        const int edgeChunks = row.edge_ / kFieldChunkEdge;
        const auto written = [&](IRMath::ivec2 chunk) {
            const IRMath::ivec2 local = chunk - originChunk;
            return row.dense_ || (local.x % 4 == 0 && local.y % 4 == 0);
        };

        std::set<FieldChunkKey> savedRegions;
        int savedChunks = 0;
        {
            WorldField writer;
            persist(writer, subdirectory);
            for (int cy = 0; cy < edgeChunks; ++cy) {
                for (int cx = 0; cx < edgeChunks; ++cx) {
                    const IRMath::ivec2 chunk = originChunk + IRMath::ivec2{cx, cy};
                    if (!written(chunk)) {
                        continue;
                    }
                    for (int y = 0; y < kFieldChunkEdge; ++y) {
                        writer.fillRow(
                            chunk * kFieldChunkEdge + IRMath::ivec2{0, y},
                            kFieldChunkEdge,
                            chunkValue(chunk, y)
                        );
                    }
                    savedRegions.insert(
                        packFieldChunkKey(FieldChunkDiskPersistence::regionOf(chunk))
                    );
                    ++savedChunks;
                }
            }
            ASSERT_EQ(writer.flush(), static_cast<int>(savedRegions.size()));
        }

        WorldField reader;
        persist(reader, subdirectory);
        WindowGatherPlan plan;
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(row.edge_) * kFieldChunkEdge * 4);
        const auto planStart = std::chrono::steady_clock::now();
        planWindowGather(std::nullopt, origin, row.edge_, {}, plan);
        std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - planStart;
        ASSERT_EQ(plan.rects_.size(), static_cast<std::size_t>(edgeChunks));

        bool readBack = true;
        for (const WindowUploadRect &rect : plan.rects_) {
            const auto expandStart = std::chrono::steady_clock::now();
            expandWindowChunks(reader, origin, rect, strip);
            elapsed += std::chrono::steady_clock::now() - expandStart;

            for (int y = 0; y < kFieldChunkEdge && readBack; ++y) {
                for (int x = 0; x < row.edge_; ++x) {
                    const IRMath::ivec2 cell = origin + rect.texel_ + IRMath::ivec2{x, y};
                    const IRMath::ivec2 chunk = fieldChunkOf(cell);
                    const std::uint8_t expected = written(chunk) ? chunkValue(chunk, y) : 0;
                    if (strip[static_cast<std::size_t>(y * row.edge_ + x) * 4] != expected) {
                        readBack = false;
                        break;
                    }
                }
            }
        }

        const IRPrefab::Fog::WorldFieldStats stats = reader.stats();
        const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        std::printf(
            "FOG-COLD-GATHER edge=%d fill=%s regions=%d probes=%d loads=%d chunks=%d ms=%.2f\n",
            row.edge_,
            row.dense_ ? "dense" : "sparse",
            row.regions_,
            stats.probes_,
            stats.loads_,
            stats.residentChunks_,
            ms
        );
        EXPECT_EQ(stats.probes_, row.regions_) << subdirectory;
        EXPECT_EQ(stats.loads_, static_cast<int>(savedRegions.size())) << subdirectory;
        EXPECT_GT(stats.loads_, 0) << subdirectory;
        EXPECT_EQ(stats.residentChunks_, savedChunks) << subdirectory;
        EXPECT_TRUE(readBack) << subdirectory;
    }
}

class FogWindowGatherTest : public ::testing::Test {
  protected:
    WindowGatherPlan plan(
        std::vector<IRMath::ivec2> pending, std::optional<IRMath::ivec2> previous = kLegacyOrigin
    ) {
        std::vector<FieldChunkKey> keys;
        for (IRMath::ivec2 chunk : pending) {
            keys.push_back(packFieldChunkKey(chunk));
        }
        std::sort(keys.begin(), keys.end());
        WindowGatherPlan result;
        planWindowGather(previous, kLegacyOrigin, kLegacyEdge, keys, result);
        return result;
    }

    static void expectRect(const WindowUploadRect &rect, IRMath::ivec2 texel, IRMath::ivec2 size) {
        EXPECT_EQ(rect.texel_, texel);
        EXPECT_EQ(rect.size_, size);
    }
};

TEST_F(FogWindowGatherTest, NothingPendingPlansNothing) {
    const WindowGatherPlan result = plan({});
    EXPECT_TRUE(result.chunks_.empty());
    EXPECT_TRUE(result.rects_.empty());
}

TEST_F(FogWindowGatherTest, PendingOutsideTheWindowPlansNothing) {
    const WindowGatherPlan result = plan({{4, 0}, {-5, 0}, {0, 4}, {1000, -1000}});
    EXPECT_TRUE(result.chunks_.empty());
    EXPECT_TRUE(result.rects_.empty());
}

TEST_F(FogWindowGatherTest, OnePendingChunkPlansItsSquare) {
    WindowGatherPlan result = plan({{0, 0}});
    ASSERT_EQ(result.rects_.size(), 1u);
    expectRect(result.rects_[0], {128, 128}, {32, 32});
    EXPECT_EQ(result.chunks_, std::vector<IRMath::ivec2>{IRMath::ivec2(0, 0)});

    result = plan({{-4, -1}});
    ASSERT_EQ(result.rects_.size(), 1u);
    expectRect(result.rects_[0], {0, 96}, {32, 32});
}

TEST_F(FogWindowGatherTest, AdjacentChunksInARowCoalesce) {
    WindowGatherPlan result = plan({{0, 0}, {-1, 0}});
    ASSERT_EQ(result.rects_.size(), 1u);
    expectRect(result.rects_[0], {96, 128}, {64, 32});

    result = plan({{0, 0}, {0, 1}});
    ASSERT_EQ(result.rects_.size(), 2u);
    expectRect(result.rects_[0], {128, 128}, {32, 32});
    expectRect(result.rects_[1], {128, 160}, {32, 32});

    result = plan({{-2, 2}, {0, 2}, {1, 2}});
    ASSERT_EQ(result.rects_.size(), 2u);
    expectRect(result.rects_[0], {64, 192}, {32, 32});
    expectRect(result.rects_[1], {128, 192}, {64, 32});
}

TEST_F(FogWindowGatherTest, UnsetPreviousOriginPlansTheWholeWindowInStrips) {
    const WindowGatherPlan result = plan({}, std::nullopt);
    EXPECT_EQ(result.chunks_.size(), 64u);
    ASSERT_EQ(result.rects_.size(), 8u);
    for (int row = 0; row < 8; ++row) {
        expectRect(result.rects_[static_cast<std::size_t>(row)], {0, row * 32}, {256, 32});
    }
}

} // namespace
