#include <gtest/gtest.h>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
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
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::FogLosEyeHeights;
using IRComponents::FrameDataFogObservers;
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
constexpr int kDemoEdge = 1152;

// The world column texel @p texel of a window at @p origin shows: the inverse
// of the toroidal address restricted to the window.
IRMath::ivec2 windowColumnOfTexel(IRMath::ivec2 origin, int edge, IRMath::ivec2 texel) {
    return origin + IRMath::ivec2{
                        static_cast<int>(IRMath::floorMod(texel.x - origin.x, edge)),
                        static_cast<int>(IRMath::floorMod(texel.y - origin.y, edge))
                    };
}

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
        expandWindowChunks(field, origin, edge, rect, strip);
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

// The expanded window is toroidal: column `c` lands at texel
// `floorMod(c, edge)` whatever the origin, and the origin only decides which
// columns are shown. At the legacy origin that is a half-texture rotation of
// the old `c + 128` layout, and a second origin shows a different column set
// at the same texels.
TEST_F(FogWorldFieldTest, WindowExpansionUsesTheToroidalLayout) {
    WorldField field;
    field.revealRadius({0, 0}, 40);
    field.revealRadius({120, -120}, 30);
    field.setCell({-128, -128}, kFogStateExplored);
    field.setCell({127, 127}, kFogStateExplored);
    field.setCell({128, 0}, kFogStateVisible);
    field.setCell({0, -129}, kFogStateVisible);

    for (const IRMath::ivec2 origin : {kLegacyOrigin, IRMath::ivec2{0, -256}}) {
        std::vector<std::uint8_t> image;
        expandWholeWindow(field, origin, kLegacyEdge, image);
        int visible = 0;
        for (int ty = 0; ty < kLegacyEdge; ++ty) {
            for (int tx = 0; tx < kLegacyEdge; ++tx) {
                const IRMath::ivec2 column = windowColumnOfTexel(origin, kLegacyEdge, {tx, ty});
                ASSERT_GE(column.x, origin.x);
                ASSERT_LT(column.x, origin.x + kLegacyEdge);
                ASSERT_EQ(IRMath::floorMod(column.x, kLegacyEdge), tx);
                ASSERT_EQ(IRMath::floorMod(column.y, kLegacyEdge), ty);
                const std::size_t i = static_cast<std::size_t>(ty * kLegacyEdge + tx);
                const std::uint8_t expected = field.peekCell(column).value_or(kFogStateUnexplored);
                ASSERT_EQ(image[i * 4], expected) << "texel " << tx << "," << ty;
                ASSERT_EQ(image[i * 4 + 1], 0);
                ASSERT_EQ(image[i * 4 + 2], 0);
                ASSERT_EQ(image[i * 4 + 3], 0);
                visible += expected == kFogStateVisible ? 1 : 0;
            }
        }
        EXPECT_GT(visible, 0) << "origin " << origin.x << "," << origin.y;
    }

    std::vector<std::uint8_t> image;
    expandWholeWindow(field, kLegacyOrigin, kLegacyEdge, image);
    // Column (0, 0) is at texel (0, 0), not (128, 128); column (-128, -128)
    // at (128, 128); column (127, 127) at (127, 127).
    EXPECT_EQ(image[0], kFogStateVisible);
    EXPECT_EQ(image[static_cast<std::size_t>(128 * kLegacyEdge + 128) * 4], kFogStateExplored);
    EXPECT_EQ(image[static_cast<std::size_t>(127 * kLegacyEdge + 127) * 4], kFogStateExplored);
    EXPECT_EQ(image[static_cast<std::size_t>(255 * kLegacyEdge + 0) * 4], kFogStateVisible)
        << "texel row 255 shows column (0, -1), inside the disc";
    EXPECT_EQ(image[static_cast<std::size_t>(127 * kLegacyEdge + 0) * 4], kFogStateUnexplored)
        << "texel row 127 shows column (0, 127), not the visible column (0, -129) that shares "
           "its address outside the window";
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
            expandWindowChunks(reader, origin, row.edge_, rect, strip);
            elapsed += std::chrono::steady_clock::now() - expandStart;

            for (int y = 0; y < kFieldChunkEdge && readBack; ++y) {
                for (int x = 0; x < row.edge_; ++x) {
                    const IRMath::ivec2 cell =
                        windowColumnOfTexel(origin, row.edge_, rect.texel_ + IRMath::ivec2{x, y});
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

// Vision source @p index of the tier fixtures: far-apart centres with
// half-integer offsets, so the stamp's `roundHalfUp` centre is exercised on
// both signs.
IRMath::vec2 tierSourceCentre(int index) {
    return {static_cast<float>(index * 1000) + 0.5f, static_cast<float>(-index * 700) - 0.5f};
}

IRMath::ivec2 tierSourceCell(int index) {
    const IRMath::vec2 centre = tierSourceCentre(index);
    return {IRMath::roundHalfUp(centre.x), IRMath::roundHalfUp(centre.y)};
}

int admitTierSource(
    FrameDataFogObservers &observers,
    FogLosEyeHeights &eyes,
    WorldField &field,
    int index,
    float radius
) {
    const IRMath::vec2 centre = tierSourceCentre(index);
    return C_CanvasFogOfWar::addVisionCircle(
        observers,
        eyes,
        field,
        centre.x,
        centre.y,
        radius,
        0.0f,
        0.0f,
        0.0f,
        IRComponents::kFogVisionZCostMirrorUp,
        0.0f
    );
}

TEST_F(FogWorldFieldTest, SourcesPastTheCapRevealThroughTheField) {
    constexpr int kSources = 12;
    constexpr int kRadius = 6;
    static_assert(kSources > IRComponents::kMaxFogVisionCircles);
    WorldField field;
    FrameDataFogObservers observers;
    FogLosEyeHeights eyes{};
    eyes.fill(IRComponents::kFogVisionLosOff);

    for (int i = 0; i < kSources; ++i) {
        const int slot = admitTierSource(observers, eyes, field, i, static_cast<float>(kRadius));
        EXPECT_EQ(slot, i < IRComponents::kMaxFogVisionCircles ? i : -1) << "source " << i;
    }
    EXPECT_EQ(observers.visionCircleCount_, IRComponents::kMaxFogVisionCircles)
        << "the UBO still holds exactly the analytic cap";

    for (int i = 0; i < kSources; ++i) {
        const IRMath::ivec2 centre = tierSourceCell(i);
        if (i < IRComponents::kMaxFogVisionCircles) {
            EXPECT_EQ(field.getCell(centre), kFogStateUnexplored)
                << "analytic source " << i << " stamps nothing";
            continue;
        }
        EXPECT_EQ(field.getCell(centre), kFogStateVisible) << "tier source " << i;
        EXPECT_EQ(field.getCell(centre + IRMath::ivec2{kRadius + 1, 0}), kFogStateUnexplored);
        EXPECT_EQ(field.getCell(centre + IRMath::ivec2{0, -(kRadius + 1)}), kFogStateUnexplored);
        std::int64_t visible = 0;
        for (int dy = -kRadius - 1; dy <= kRadius + 1; ++dy) {
            for (int dx = -kRadius - 1; dx <= kRadius + 1; ++dx) {
                visible += field.getCell(centre + IRMath::ivec2{dx, dy}) == kFogStateVisible;
            }
        }
        EXPECT_EQ(visible, discCellCount(kRadius)) << "tier source " << i;
    }

    const IRMath::ivec2 written = tierSourceCell(kSources - 1);
    field.setCell(written, kFogStateExplored);
    EXPECT_EQ(field.getCell(written), kFogStateVisible)
        << "setCell writes the persistent layer; the tier disc still composes over it";

    C_CanvasFogOfWar::clearVisionCircles(observers, eyes, field);
    EXPECT_EQ(observers.visionCircleCount_, 0);
    for (int i = IRComponents::kMaxFogVisionCircles; i < kSources - 1; ++i) {
        EXPECT_EQ(field.getCell(tierSourceCell(i)), kFogStateUnexplored)
            << "tier source " << i << " after clearVisionCircles";
    }
    EXPECT_EQ(field.getCell(written), kFogStateExplored)
        << "the tier leaves no memory; the persistent write stands";
    EXPECT_EQ(field.stats().probes_, 0);
}

// An integer radius and centre stamp exactly the cells `revealRadius` marks.
TEST_F(FogWorldFieldTest, FieldTierDiscMatchesRevealRadius) {
    constexpr int kRadius = 16;
    const IRMath::ivec2 centre{-37, 90};
    WorldField tier;
    WorldField revealed;
    EXPECT_EQ(
        tier.stampTransientDisc(IRMath::vec2(centre), static_cast<float>(kRadius)),
        revealed.revealRadius(centre, kRadius)
    );
    for (int dy = -kRadius - 2; dy <= kRadius + 2; ++dy) {
        for (int dx = -kRadius - 2; dx <= kRadius + 2; ++dx) {
            const IRMath::ivec2 cell = centre + IRMath::ivec2{dx, dy};
            ASSERT_EQ(tier.getCell(cell), revealed.getCell(cell)) << dx << ", " << dy;
        }
    }
    EXPECT_EQ(tier.stampTransientDisc(IRMath::vec2(centre), 0.0f), 0);
    EXPECT_EQ(tier.stampTransientDisc(IRMath::vec2(centre), -3.0f), 0);
}

// The gather composes both layers per cell, and clearing the tier re-expands
// the chunks it covered.
TEST_F(FogWorldFieldTest, FieldTierDiscReachesTheWindowGather) {
    const IRMath::ivec2 centre{10, -20};
    const IRMath::ivec2 persistentCell{12, -20};
    WorldField field;
    field.setCell(persistentCell, kFogStateExplored);
    field.stampTransientDisc(IRMath::vec2(centre), 4.0f);
    std::vector<FieldChunkKey> pending;
    field.consumePending(pending);
    EXPECT_TRUE(
        std::binary_search(pending.begin(), pending.end(), packFieldChunkKey(fieldChunkOf(centre)))
    );

    std::vector<std::uint8_t> image;
    const auto texelState = [&](IRMath::ivec2 column) {
        const IRMath::ivec2 texel = IRPrefab::Fog::detail::windowTexel(column, kLegacyEdge);
        return image[static_cast<std::size_t>((texel.y * kLegacyEdge + texel.x) * 4)];
    };
    expandWholeWindow(field, kLegacyOrigin, kLegacyEdge, image);
    EXPECT_EQ(texelState(centre), kFogStateVisible);
    EXPECT_EQ(texelState(persistentCell), kFogStateVisible);
    EXPECT_EQ(texelState(centre + IRMath::ivec2{5, 0}), kFogStateUnexplored);

    field.clearTransient();
    field.consumePending(pending);
    EXPECT_TRUE(
        std::binary_search(pending.begin(), pending.end(), packFieldChunkKey(fieldChunkOf(centre)))
    ) << "clearing the tier re-expands the chunks it covered";
    expandWholeWindow(field, kLegacyOrigin, kLegacyEdge, image);
    EXPECT_EQ(texelState(centre), kFogStateUnexplored);
    EXPECT_EQ(texelState(persistentCell), kFogStateExplored);
}

TEST_F(FogWorldFieldTest, FieldTierStampsAreNotPersisted) {
    const int tierIndex = IRComponents::kMaxFogVisionCircles;
    const IRMath::ivec2 centre = tierSourceCell(tierIndex);
    const IRMath::ivec2 persistentCell = centre + IRMath::ivec2{3, 0};
    {
        WorldField field;
        persist(field);
        FrameDataFogObservers observers;
        FogLosEyeHeights eyes{};
        eyes.fill(IRComponents::kFogVisionLosOff);
        for (int i = 0; i <= tierIndex; ++i) {
            admitTierSource(observers, eyes, field, i, 5.0f);
        }
        field.setCell(persistentCell, kFogStateExplored);
        ASSERT_EQ(field.getCell(centre), kFogStateVisible);
        EXPECT_EQ(field.stats().saves_, 0);
        EXPECT_EQ(field.flush(), 1) << "only the persistent write's region is saved";

        const IRMath::ivec2 farChunk{-100000, -100000};
        field.evict(farChunk, farChunk);
        field.evict(farChunk, farChunk);
        const IRPrefab::Fog::WorldFieldStats stats = field.stats();
        EXPECT_EQ(stats.evictions_, 1);
        EXPECT_EQ(stats.residentRegions_, 0);
        EXPECT_EQ(field.peekCell(centre), std::optional<std::uint8_t>{kFogStateVisible})
            << "eviction never drops the transient layer";
    }
    WorldField fresh;
    persist(fresh);
    EXPECT_EQ(fresh.getCell(centre), kFogStateUnexplored);
    EXPECT_EQ(fresh.getCell(persistentCell), kFogStateExplored);
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

// A pending chunk lands at its toroidal texel, `floorMod(chunk, edgeChunks)`
// field chunks in, whatever the origin.
TEST_F(FogWindowGatherTest, OnePendingChunkPlansItsSquare) {
    WindowGatherPlan result = plan({{0, 0}});
    ASSERT_EQ(result.rects_.size(), 1u);
    expectRect(result.rects_[0], {0, 0}, {32, 32});
    EXPECT_EQ(result.chunks_, std::vector<IRMath::ivec2>{IRMath::ivec2(0, 0)});

    result = plan({{-4, -1}});
    ASSERT_EQ(result.rects_.size(), 1u);
    expectRect(result.rects_[0], {128, 224}, {32, 32});
}

// Adjacent pending chunks in a row upload as one span, split once where the
// span wraps past the texture edge.
TEST_F(FogWindowGatherTest, AdjacentChunksInARowCoalesce) {
    WindowGatherPlan result = plan({{1, 0}, {2, 0}});
    ASSERT_EQ(result.rects_.size(), 1u);
    expectRect(result.rects_[0], {32, 0}, {64, 32});

    result = plan({{0, 0}, {0, 1}});
    ASSERT_EQ(result.rects_.size(), 2u);
    expectRect(result.rects_[0], {0, 0}, {32, 32});
    expectRect(result.rects_[1], {0, 32}, {32, 32});

    result = plan({{-2, 2}, {0, 2}, {1, 2}});
    ASSERT_EQ(result.rects_.size(), 2u);
    expectRect(result.rects_[0], {192, 64}, {32, 32});
    expectRect(result.rects_[1], {0, 64}, {64, 32});

    // Chunks -1 and 0 are adjacent in the world but sit at texture columns 7
    // and 0: one span, two rectangles.
    result = plan({{-1, 0}, {0, 0}});
    ASSERT_EQ(result.rects_.size(), 2u);
    expectRect(result.rects_[0], {224, 0}, {32, 32});
    expectRect(result.rects_[1], {0, 0}, {32, 32});
}

TEST_F(FogWindowGatherTest, UnsetPreviousOriginPlansTheWholeWindowInStrips) {
    const WindowGatherPlan result = plan({}, std::nullopt);
    EXPECT_EQ(result.chunks_.size(), 64u);
    ASSERT_EQ(result.rects_.size(), 8u);
    for (int row = 0; row < 8; ++row) {
        expectRect(result.rects_[static_cast<std::size_t>(row)], {0, row * 32}, {256, 32});
    }
}

// The camera-anchored window: its edge per canvas, its origin from the view
// centre, the strips a move plans, and the region probes a crossing costs.
class FogWindowTest : public FogWorldFieldTest {
  protected:
    static bool rectInsideTexture(const WindowUploadRect &rect, int edge) {
        return rect.texel_.x >= 0 && rect.texel_.y >= 0 && rect.size_.x > 0 && rect.size_.y > 0 &&
               rect.texel_.x + rect.size_.x <= edge && rect.texel_.y + rect.size_.y <= edge &&
               rect.texel_.x % kFieldChunkEdge == 0 && rect.texel_.y % kFieldChunkEdge == 0 &&
               rect.size_.x % kFieldChunkEdge == 0 && rect.size_.y % kFieldChunkEdge == 0;
    }

    static int rectChunks(const WindowUploadRect &rect) {
        return (rect.size_.x / kFieldChunkEdge) * (rect.size_.y / kFieldChunkEdge);
    }

    static WindowGatherPlan move(IRMath::ivec2 previous, IRMath::ivec2 origin, int edge) {
        WindowGatherPlan result;
        planWindowGather(previous, origin, edge, {}, result);
        for (const WindowUploadRect &rect : result.rects_) {
            EXPECT_TRUE(rectInsideTexture(rect, edge))
                << "rect " << rect.texel_.x << "," << rect.texel_.y << " " << rect.size_.x << "x"
                << rect.size_.y;
        }
        return result;
    }
};

TEST_F(FogWindowTest, EdgeTable) {
    using IRPrefab::Fog::detail::windowEdgeForCanvas;
    using IRPrefab::Fog::detail::windowEdgeUncapped;
    EXPECT_EQ(windowEdgeForCanvas({642, 722}), 1152);
    EXPECT_EQ(windowEdgeForCanvas({962, 1082}), 1472);
    EXPECT_EQ(windowEdgeForCanvas({1282, 1442}), 1856);
    EXPECT_EQ(windowEdgeForCanvas({1922, 2162}), 2496);
    EXPECT_EQ(windowEdgeForCanvas({2562, 2882}), 3200);
    EXPECT_EQ(windowEdgeForCanvas({3842, 4322}), 4096);
    EXPECT_EQ(windowEdgeUncapped({3842, 4322}), 4544);
    EXPECT_EQ(IRPrefab::Fog::detail::windowCoveredRadius(1152), 543);
    for (const int edge : {1152, 1472, 1856, 2496, 3200, 4096}) {
        EXPECT_EQ(edge % IRPrefab::Fog::kFogWindowEdgeQuantum, 0);
    }
}

// The origin is the rounded centre snapped down to a field chunk, less half
// the edge, so it is always chunk-aligned; and the yawed inverse the centre
// comes through round-trips the forward projection.
TEST_F(FogWindowTest, CentreRoundTrips) {
    using IRPrefab::Fog::detail::windowOriginForCentre;
    EXPECT_EQ(windowOriginForCentre({0.0f, 0.0f}, kDemoEdge), IRMath::ivec2(-576, -576));
    EXPECT_EQ(windowOriginForCentre({100.4f, -0.6f}, kDemoEdge), IRMath::ivec2(-480, -608));
    EXPECT_EQ(windowOriginForCentre({-0.5f, 31.5f}, kDemoEdge), IRMath::ivec2(-576, -544));
    EXPECT_EQ(windowOriginForCentre({2400.0f, 0.0f}, kDemoEdge), IRMath::ivec2(1824, -576));
    for (const float centre : {-1000.75f, -33.0f, -0.5f, 0.0f, 0.49f, 31.9f, 32.0f, 4095.5f}) {
        const IRMath::ivec2 origin = windowOriginForCentre({centre, centre}, kDemoEdge);
        EXPECT_EQ(IRMath::floorMod(origin.x, kFieldChunkEdge), 0) << centre;
        const int rounded = IRMath::roundHalfUp(centre);
        EXPECT_LE(origin.x + kDemoEdge / 2, rounded);
        EXPECT_GT(origin.x + kDemoEdge / 2 + kFieldChunkEdge, rounded);
    }

    for (const float yaw : {0.0f, 0.3f, IRMath::kPi / 4.0f, IRMath::kHalfPi, 2.5f}) {
        for (const float z : {-128.0f, 0.0f, 127.0f}) {
            for (const IRMath::vec2 iso :
                 {IRMath::vec2(1.0f, 1.0f), IRMath::vec2(-2399.0f, 2401.0f)}) {
                const IRMath::vec3 world = IRMath::pos2DIsoToPos3DAtZLevelYawed(iso, z, yaw);
                const IRMath::vec2 back = IRMath::pos3DtoPos2DIsoYawed(world, yaw);
                EXPECT_NEAR(back.x, iso.x, 1e-3f) << "yaw " << yaw;
                EXPECT_NEAR(back.y, iso.y, 1e-3f) << "yaw " << yaw;
            }
        }
    }
}

TEST_F(FogWindowTest, StripPlan) {
    constexpr int kEdgeChunks = kDemoEdge / kFieldChunkEdge;

    // A one-chunk +X move: exactly one column of the window, one rectangle.
    WindowGatherPlan result = move({0, 0}, {32, 0}, kDemoEdge);
    EXPECT_EQ(result.chunks_.size(), static_cast<std::size_t>(kEdgeChunks));
    ASSERT_EQ(result.rects_.size(), 1u);
    EXPECT_EQ(rectChunks(result.rects_[0]), kEdgeChunks);
    EXPECT_EQ(result.rects_[0].size_.x, kFieldChunkEdge);
    EXPECT_EQ(result.rects_[0].size_.y, kDemoEdge);
    for (const IRMath::ivec2 chunk : result.chunks_) {
        EXPECT_EQ(chunk.x, kEdgeChunks) << "the exposed column is the window's last";
    }

    // A one-chunk -Y move: one row.
    result = move({0, 0}, {0, -32}, kDemoEdge);
    EXPECT_EQ(result.chunks_.size(), static_cast<std::size_t>(kEdgeChunks));
    ASSERT_EQ(result.rects_.size(), 1u);
    EXPECT_EQ(result.rects_[0].size_.x, kDemoEdge);
    EXPECT_EQ(result.rects_[0].size_.y, kFieldChunkEdge);
    for (const IRMath::ivec2 chunk : result.chunks_) {
        EXPECT_EQ(chunk.y, -1) << "the exposed row is the window's first";
    }

    // A two-chunk +X move whose exposed columns straddle the texture edge:
    // two rectangles covering the two columns.
    result = move({-32, 0}, {32, 0}, kDemoEdge);
    EXPECT_EQ(result.chunks_.size(), static_cast<std::size_t>(2 * kEdgeChunks));
    ASSERT_EQ(result.rects_.size(), 2u);
    EXPECT_EQ(rectChunks(result.rects_[0]) + rectChunks(result.rects_[1]), 2 * kEdgeChunks);
    EXPECT_EQ(result.rects_[0].texel_.x, kDemoEdge - kFieldChunkEdge);
    EXPECT_EQ(result.rects_[1].texel_.x, 0);

    // A diagonal move: one column plus one row, at most two rectangles each.
    result = move({0, 0}, {32, 32}, kDemoEdge);
    EXPECT_EQ(result.chunks_.size(), static_cast<std::size_t>(2 * kEdgeChunks - 1));
    ASSERT_EQ(result.rects_.size(), 2u);
    EXPECT_EQ(result.rects_[0].size_, IRMath::ivec2(kFieldChunkEdge, kDemoEdge));
    EXPECT_EQ(result.rects_[1].size_, IRMath::ivec2(kDemoEdge, kFieldChunkEdge));

    // A move of the window's width re-expands the whole window in row strips.
    result = move({0, 0}, {kDemoEdge, 0}, kDemoEdge);
    EXPECT_EQ(result.chunks_.size(), static_cast<std::size_t>(kEdgeChunks * kEdgeChunks));
    ASSERT_EQ(result.rects_.size(), static_cast<std::size_t>(kEdgeChunks));
    for (const WindowUploadRect &rect : result.rects_) {
        EXPECT_EQ(rect.size_, IRMath::ivec2(kDemoEdge, kFieldChunkEdge));
    }

    // No move plans nothing.
    result = move({32, 32}, {32, 32}, kDemoEdge);
    EXPECT_TRUE(result.chunks_.empty());
    EXPECT_TRUE(result.rects_.empty());
}

// The gather over a persisted, empty root: a one-chunk crossing probes only
// when it enters a new region column, then exactly one region per window row
// (four at this edge), and a static origin never evicts or probes.
TEST_F(FogWindowTest, CrossingProbeBound) {
    WorldField field;
    persist(field);
    IRPrefab::Fog::detail::WindowGatherScratch scratch;
    std::optional<IRMath::ivec2> windowOrigin;
    int uploads = 0;
    const auto upload = [&](const WindowUploadRect &, std::span<const std::uint8_t>) { ++uploads; };

    // Region-local chunk index 15 on both axes: the worst-case alignment.
    const IRMath::ivec2 originChunk{15, 15};
    IRMath::ivec2 origin = originChunk * kFieldChunkEdge;
    IRPrefab::Fog::detail::gatherWindow(field, windowOrigin, origin, kDemoEdge, scratch, upload);
    IRPrefab::Fog::WorldFieldStats stats = field.stats();
    EXPECT_EQ(stats.probes_, 16) << "the first frame probes every window region once";
    EXPECT_EQ(stats.loads_, 0);
    EXPECT_EQ(stats.evictions_, 0);
    EXPECT_EQ(uploads, kDemoEdge / kFieldChunkEdge);

    for (int repeat = 0; repeat < 3; ++repeat) {
        uploads = 0;
        IRPrefab::Fog::detail::gatherWindow(
            field,
            windowOrigin,
            origin,
            kDemoEdge,
            scratch,
            upload
        );
        stats = field.stats();
        EXPECT_EQ(stats.probes_, 0) << "a static origin never probes";
        EXPECT_EQ(stats.evictions_, 0) << "a static origin never evicts";
        EXPECT_EQ(uploads, 0);
    }

    int totalProbes = 0;
    int columnsEntered = 0;
    for (int step = 1; step <= 32; ++step) {
        origin.x += kFieldChunkEdge;
        uploads = 0;
        IRPrefab::Fog::detail::gatherWindow(
            field,
            windowOrigin,
            origin,
            kDemoEdge,
            scratch,
            upload
        );
        stats = field.stats();
        const int lastChunkX = originChunk.x + step + kDemoEdge / kFieldChunkEdge - 1;
        const bool enteredColumn = lastChunkX % IRWorld::kFieldRegionEdgeChunks == 0;
        EXPECT_LE(stats.probes_, 4) << "step " << step;
        EXPECT_EQ(stats.probes_, enteredColumn ? 4 : 0) << "step " << step;
        EXPECT_EQ(uploads, 1) << "one exposed column, one rectangle at step " << step;
        totalProbes += stats.probes_;
        columnsEntered += enteredColumn ? 1 : 0;
    }
    EXPECT_EQ(columnsEntered, 2);
    EXPECT_EQ(totalProbes, 4 * columnsEntered);
    EXPECT_LE(field.stats().residentRegions_, 20) << "regions the window left behind were evicted";
}

} // namespace
