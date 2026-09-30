#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/profile/logger_spd.hpp>
#include <irreden/spatial/chunked_field.hpp>
#include <irreden/world/field_chunk_persistence.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using IRPrefab::Spatial::kFieldChunkCells;
using IRWorld::FieldChunkDiskPersistence;
using IRWorld::FieldRegion;
using IRWorld::kFieldRegionChunks;

class EngineLogCapture {
  public:
    EngineLogCapture()
        : m_logger{LoggerSpd::instance()->getEngineLogger()}
        , m_sink{std::make_shared<spdlog::sinks::ostream_sink_st>(m_text)} {
        m_logger->sinks().push_back(m_sink);
    }

    ~EngineLogCapture() {
        auto &sinks = m_logger->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    std::string text() {
        m_logger->flush();
        return m_text.str();
    }

  private:
    std::ostringstream m_text;
    spdlog::logger *m_logger;
    std::shared_ptr<spdlog::sinks::sink> m_sink;
};

class FieldChunkPersistenceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        static std::atomic<std::uint64_t> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("ir-field-persistence-" + std::to_string(stamp) + "-" +
                  std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(m_root);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    FieldChunkDiskPersistence persistence(const std::string &layer = "fog") const {
        std::optional<FieldChunkDiskPersistence> created =
            FieldChunkDiskPersistence::create(m_root.string(), layer, 1);
        EXPECT_TRUE(created.has_value());
        return *created;
    }

    static FieldRegion patternedRegion(std::initializer_list<int> bits) {
        FieldRegion region;
        for (int bit : bits) {
            region.setChunk(bit);
            for (int cell = 0; cell < kFieldChunkCells; ++cell) {
                region.cells_.push_back(static_cast<std::uint8_t>((bit * 31 + cell * 7) & 0xff));
            }
        }
        return region;
    }

    static void writeBytes(const std::string &path, const std::vector<std::uint8_t> &bytes) {
        std::filesystem::create_directories(std::filesystem::path{path}.parent_path());
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        file.write(
            reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }

    static std::vector<std::uint8_t> readBytes(const std::string &path) {
        std::ifstream file{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    static std::vector<std::uint8_t>
    encode(IRMath::ivec2 region, const FieldRegion &data, std::size_t cellBytes, bool extraTag) {
        IRAsset::MemoryBinaryWriter header;
        header.writeI32(region.x);
        header.writeI32(region.y);
        header.writeU8(32);
        header.writeU8(16);
        header.writeU8(1);
        std::vector<IRAsset::ChunkPayload> payloads{
            {IRAsset::makeTag("FHDR"), header.takeBuffer()},
            {IRAsset::makeTag("CMSK"), {data.mask_.begin(), data.mask_.end()}},
            {IRAsset::makeTag("CELL"),
             {data.cells_.begin(), data.cells_.begin() + static_cast<std::ptrdiff_t>(cellBytes)}},
        };
        if (extraTag) {
            payloads.push_back({IRAsset::makeTag("XTRA"), {1, 2, 3}});
        }
        IRAsset::MemoryBinaryWriter file;
        EXPECT_TRUE(IRAsset::writeChunked(file, IRAsset::makeTag("IRFD"), 1, payloads).ok());
        return file.takeBuffer();
    }

    std::filesystem::path m_root;
};

TEST_F(FieldChunkPersistenceTest, PathFragmentsAtTheInt32Extremes) {
    const FieldChunkDiskPersistence store = persistence();
    constexpr int kMin = std::numeric_limits<std::int32_t>::min();
    constexpr int kMax = std::numeric_limits<std::int32_t>::max();
    const IRMath::ivec2 lowRegion =
        FieldChunkDiskPersistence::regionOf(IRPrefab::Spatial::fieldChunkOf({kMin, kMin}));
    const IRMath::ivec2 highRegion =
        FieldChunkDiskPersistence::regionOf(IRPrefab::Spatial::fieldChunkOf({kMax, kMax}));
    EXPECT_EQ(lowRegion, IRMath::ivec2(-4194304, -4194304));
    EXPECT_EQ(highRegion, IRMath::ivec2(4194303, 4194303));

    const std::filesystem::path layer = m_root / "fields" / "fog";
    EXPECT_EQ(
        std::filesystem::path{store.regionPath({lowRegion.x, highRegion.y})},
        layer / "-65536" / "65535" / "-0004194304_+0004194303.irfield"
    );
    EXPECT_EQ(
        std::filesystem::path{store.regionPath({kMin, kMax})},
        layer / "-33554432" / "33554431" / "-2147483648_+2147483647.irfield"
    );
    EXPECT_EQ(
        std::filesystem::path{store.regionPath({-1, 0})},
        layer / "-1" / "0" / "-0000000001_+0000000000.irfield"
    );
}

TEST_F(FieldChunkPersistenceTest, RegionsRoundTrip) {
    const FieldChunkDiskPersistence store = persistence();
    const FieldRegion sparse = patternedRegion({0, 17, 255});
    ASSERT_TRUE(store.saveRegion({-3, 5}, sparse));
    std::optional<FieldRegion> loaded = store.loadRegion({-3, 5});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->mask_, sparse.mask_);
    EXPECT_EQ(loaded->chunkCount(), 3);
    EXPECT_EQ(loaded->cells_, sparse.cells_);

    FieldRegion full;
    for (int bit = 0; bit < kFieldRegionChunks; ++bit) {
        full.setChunk(bit);
        for (int cell = 0; cell < kFieldChunkCells; ++cell) {
            full.cells_.push_back(static_cast<std::uint8_t>((bit + cell) & 0xff));
        }
    }
    ASSERT_TRUE(store.saveRegion({7, -9}, full));
    loaded = store.loadRegion({7, -9});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->chunkCount(), kFieldRegionChunks);
    EXPECT_EQ(loaded->cells_, full.cells_);
    EXPECT_FALSE(std::filesystem::exists(store.regionPath({7, -9}) + ".tmp"));
}

TEST_F(FieldChunkPersistenceTest, MissingFileIsAQuietAbsence) {
    const FieldChunkDiskPersistence store = persistence();
    EngineLogCapture log;
    EXPECT_FALSE(store.loadRegion({4, 4}).has_value());
    EXPECT_FALSE(store.regionExists({4, 4}));
    EXPECT_EQ(log.text(), "");
}

TEST_F(FieldChunkPersistenceTest, MalformedFilesFailSoftWithAWarning) {
    const FieldChunkDiskPersistence store = persistence();
    const FieldRegion region = patternedRegion({3, 40});
    const std::vector<std::uint8_t> good = encode({1, 1}, region, region.cells_.size(), false);

    std::vector<std::uint8_t> badMagic = good;
    badMagic[0] = 'X';
    writeBytes(store.regionPath({1, 1}), badMagic);
    {
        EngineLogCapture log;
        EXPECT_FALSE(store.loadRegion({1, 1}).has_value());
        EXPECT_NE(log.text().find("malformed"), std::string::npos);
    }

    const std::vector<std::uint8_t> truncated(good.begin(), good.begin() + good.size() / 2);
    writeBytes(store.regionPath({1, 1}), truncated);
    {
        EngineLogCapture log;
        EXPECT_FALSE(store.loadRegion({1, 1}).has_value());
        EXPECT_NE(log.text().find("malformed"), std::string::npos);
    }

    writeBytes(store.regionPath({2, 1}), good);
    {
        EngineLogCapture log;
        EXPECT_FALSE(store.loadRegion({2, 1}).has_value());
        EXPECT_NE(log.text().find("another region"), std::string::npos);
    }

    writeBytes(store.regionPath({1, 1}), encode({1, 1}, region, kFieldChunkCells, false));
    {
        EngineLogCapture log;
        EXPECT_FALSE(store.loadRegion({1, 1}).has_value());
        EXPECT_NE(log.text().find("CELL size"), std::string::npos);
    }

    writeBytes(store.regionPath({1, 1}), good);
    EXPECT_TRUE(store.loadRegion({1, 1}).has_value());
}

TEST_F(FieldChunkPersistenceTest, UnknownChunkTagStillLoads) {
    const FieldChunkDiskPersistence store = persistence();
    const FieldRegion region = patternedRegion({9});
    writeBytes(store.regionPath({0, -1}), encode({0, -1}, region, region.cells_.size(), true));
    std::optional<FieldRegion> loaded = store.loadRegion({0, -1});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->cells_, region.cells_);
}

TEST_F(FieldChunkPersistenceTest, SavingAnEmptyMaskRemovesTheFile) {
    const FieldChunkDiskPersistence store = persistence();
    ASSERT_TRUE(store.saveRegion({2, 2}, patternedRegion({1})));
    ASSERT_TRUE(store.regionExists({2, 2}));
    EXPECT_TRUE(store.saveRegion({2, 2}, FieldRegion{}));
    EXPECT_FALSE(store.regionExists({2, 2}));
    EXPECT_TRUE(store.saveRegion({3, 3}, FieldRegion{}));
    EXPECT_FALSE(store.regionExists({3, 3}));
}

TEST_F(FieldChunkPersistenceTest, CreateValidatesTheLayerAndRoot) {
    const std::string root = m_root.string();
    for (const std::string &layer :
         {std::string{""},
          std::string{"."},
          std::string{".."},
          std::string{"../x"},
          std::string{"a/b"},
          std::string{"a\\b"},
          std::string{"/abs"},
          std::string{"C:x"},
          std::string{"Fog"},
          std::string(33, 'a')}) {
        EXPECT_FALSE(FieldChunkDiskPersistence::create(root, layer, 1).has_value()) << layer;
    }
    EXPECT_FALSE(FieldChunkDiskPersistence::create("", "fog", 1).has_value());
    EXPECT_FALSE(FieldChunkDiskPersistence::create(root, "fog", 0).has_value());
    for (const std::string &layer :
         {std::string{"fog"}, std::string{"a"}, std::string{"a_9"}, std::string(32, 'z')}) {
        EXPECT_TRUE(FieldChunkDiskPersistence::create(root, layer, 1).has_value()) << layer;
    }
}

TEST_F(FieldChunkPersistenceTest, RemoveAllDeletesOnlyRegionFiles) {
    const FieldChunkDiskPersistence store = persistence();
    const FieldChunkDiskPersistence other = persistence("explored");
    const std::array<IRMath::ivec2, 3> regions{
        IRMath::ivec2{0, 0},
        IRMath::ivec2{-70, 3},
        IRMath::ivec2{64, -65},
    };
    for (IRMath::ivec2 region : regions) {
        ASSERT_TRUE(store.saveRegion(region, patternedRegion({0})));
    }
    ASSERT_TRUE(other.saveRegion({0, 0}, patternedRegion({0})));

    const std::filesystem::path layer = m_root / "fields" / "fog";
    writeBytes((layer / "keep.txt").string(), {1});
    const std::filesystem::path stray = layer / "0" / "0" / "notes.txt";
    writeBytes(stray.string(), {2});
    const std::filesystem::path outside = m_root / "outside";
    const std::filesystem::path outsideFile = outside / "0" / "+0000000001_+0000000001.irfield";
    writeBytes(outsideFile.string(), {3});
    std::error_code linkError;
    std::filesystem::create_directory_symlink(outside, layer / "5", linkError);

    EXPECT_EQ(store.removeAll(), 3);
    for (IRMath::ivec2 region : regions) {
        EXPECT_FALSE(store.regionExists(region));
    }
    EXPECT_TRUE(std::filesystem::exists(layer / "keep.txt"));
    EXPECT_TRUE(std::filesystem::exists(stray));
    EXPECT_TRUE(std::filesystem::exists(outsideFile));
    EXPECT_TRUE(other.regionExists({0, 0}));
    EXPECT_FALSE(std::filesystem::exists(layer / "-2"));
    if (!linkError) {
        EXPECT_TRUE(std::filesystem::is_symlink(layer / "5"));
    }

    const std::filesystem::path linkedRoot = m_root / "linked";
    const std::filesystem::path linkedTarget = m_root / "linked-target";
    const FieldChunkDiskPersistence target =
        *FieldChunkDiskPersistence::create(linkedTarget.string(), "fog", 1);
    ASSERT_TRUE(target.saveRegion({0, 0}, patternedRegion({0})));
    std::filesystem::create_directories(linkedRoot / "fields");
    std::filesystem::create_directory_symlink(
        linkedTarget / "fields" / "fog",
        linkedRoot / "fields" / "fog",
        linkError
    );
    if (!linkError) {
        const FieldChunkDiskPersistence linked =
            *FieldChunkDiskPersistence::create(linkedRoot.string(), "fog", 1);
        EXPECT_EQ(linked.removeAll(), 0);
        EXPECT_TRUE(target.regionExists({0, 0}));
    }
}

} // namespace
