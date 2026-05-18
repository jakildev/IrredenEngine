#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/voxel_set_format.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

const std::string kTmpDir = "/tmp";

void expectVoxelEq(const VoxelRecord &a, const VoxelRecord &b) {
    EXPECT_EQ(a.color_.red_,    b.color_.red_);
    EXPECT_EQ(a.color_.green_,  b.color_.green_);
    EXPECT_EQ(a.color_.blue_,   b.color_.blue_);
    EXPECT_EQ(a.color_.alpha_,  b.color_.alpha_);
    EXPECT_EQ(a.material_id_,   b.material_id_);
    EXPECT_EQ(a.flags_,         b.flags_);
    EXPECT_EQ(a.bone_id_,       b.bone_id_);
    EXPECT_EQ(a.layer_id_,      b.layer_id_);
    EXPECT_EQ(a.reserved_,      b.reserved_);
}

VoxelRecord makeFilledVoxel(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                            std::uint8_t mat = 1) {
    VoxelRecord v;
    v.color_ = Color{r, g, b, 255};
    v.material_id_ = mat;
    return v;
}

VoxelRecord makeEmptyVoxel() { return VoxelRecord{}; }

// Build a hollow 64³ voxel set: surface voxels filled, interior empty.
DenseVoxelSet makeHollow64Fixture() {
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0, 0, 0);
    dense.boundsMax_ = ivec3(64, 64, 64);
    const std::size_t count = dense.voxelCount();
    dense.voxels_.resize(count);
    for (int z = 0; z < 64; ++z) {
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                bool surface = (x == 0 || x == 63 || y == 0 || y == 63 ||
                                z == 0 || z == 63);
                const std::size_t idx =
                    static_cast<std::size_t>(z) * 64 * 64 +
                    static_cast<std::size_t>(y) * 64 +
                    static_cast<std::size_t>(x);
                if (surface) {
                    dense.voxels_[idx].color_ = Color{200, 200, 200, 255};
                    dense.voxels_[idx].material_id_ = 1;
                }
                // Interior: default VoxelRecord{} (alpha==0)
            }
        }
    }
    return dense;
}

// ---- VRLE chunk — low-level round-trip tests ---------------------------

TEST(VoxelSetRle, AllEmptyEncoding) {
    // All empty slots → zero triples
    const std::size_t count = 100;
    std::vector<VoxelRecord> voxels(count); // all default (alpha=0)
    auto payload = makeVoxelRecordsRleChunk(voxels);
    ASSERT_EQ(payload.tag_, kChunkTagVoxelRecordsRle);

    auto loaded = readVoxelRecordsRleChunk(payload.data_, count);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.voxels_.size(), count);
    for (const auto &v : loaded.value_.voxels_) {
        EXPECT_EQ(v.color_.alpha_, 0);
    }
}

TEST(VoxelSetRle, AllFilledEncoding) {
    // All filled → one triple with emptyRun=0
    const std::size_t count = 50;
    std::vector<VoxelRecord> voxels(count);
    for (std::size_t i = 0; i < count; ++i) {
        voxels[i] = makeFilledVoxel(
            static_cast<std::uint8_t>(i & 0xFF),
            static_cast<std::uint8_t>((i * 3) & 0xFF),
            100, 2
        );
    }
    auto payload = makeVoxelRecordsRleChunk(voxels);
    auto loaded = readVoxelRecordsRleChunk(payload.data_, count);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.voxels_.size(), count);
    for (std::size_t i = 0; i < count; ++i) {
        expectVoxelEq(loaded.value_.voxels_[i], voxels[i]);
    }
}

TEST(VoxelSetRle, HollowRoundTrip) {
    // Surface voxels filled, interior empty — the canonical hollow case
    DenseVoxelSet dense = makeHollow64Fixture();
    auto payload = makeVoxelRecordsRleChunk(dense.voxels_);
    const std::size_t count = dense.voxelCount();
    auto loaded = readVoxelRecordsRleChunk(payload.data_, count);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.voxels_.size(), count);
    for (std::size_t i = 0; i < count; ++i) {
        expectVoxelEq(loaded.value_.voxels_[i], dense.voxels_[i]);
    }
}

TEST(VoxelSetRle, StripedRoundTrip) {
    // Alternating filled/empty voxels — stress-tests triple emission
    const std::size_t count = 200;
    std::vector<VoxelRecord> voxels(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 2 == 0) {
            voxels[i] = makeFilledVoxel(
                static_cast<std::uint8_t>(i & 0xFF), 100, 50
            );
        }
    }
    auto payload = makeVoxelRecordsRleChunk(voxels);
    auto loaded = readVoxelRecordsRleChunk(payload.data_, count);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.voxels_.size(), count);
    for (std::size_t i = 0; i < count; ++i) {
        expectVoxelEq(loaded.value_.voxels_[i], voxels[i]);
    }
}

TEST(VoxelSetRle, TrailingEmptyImplicit) {
    // Filled voxels in the middle, empty on both ends
    const std::size_t count = 10;
    std::vector<VoxelRecord> voxels(count);
    voxels[3] = makeFilledVoxel(1, 2, 3);
    voxels[4] = makeFilledVoxel(4, 5, 6);
    auto payload = makeVoxelRecordsRleChunk(voxels);
    auto loaded = readVoxelRecordsRleChunk(payload.data_, count);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.voxels_.size(), count);
    expectVoxelEq(loaded.value_.voxels_[3], voxels[3]);
    expectVoxelEq(loaded.value_.voxels_[4], voxels[4]);
    // Trailing empty slots are implicit
    EXPECT_EQ(loaded.value_.voxels_[5].color_.alpha_, 0);
    EXPECT_EQ(loaded.value_.voxels_[9].color_.alpha_, 0);
}

// ---- Compression ratio -------------------------------------------------

TEST(VoxelSetRle, HollowCompressionRatio) {
    // VRLE chunk must be ≤10% of the VOXR chunk for a hollow 64³ set
    DenseVoxelSet dense = makeHollow64Fixture();
    auto vrle = makeVoxelRecordsRleChunk(dense.voxels_);
    auto voxr = makeVoxelRecordsChunk(dense.voxels_);
    ASSERT_FALSE(voxr.data_.empty());
    const double ratio =
        static_cast<double>(vrle.data_.size()) /
        static_cast<double>(voxr.data_.size());
    EXPECT_LE(ratio, 0.10)
        << "VRLE chunk (" << vrle.data_.size() << " B) should be <=10% of "
        << "VOXR chunk (" << voxr.data_.size() << " B), got "
        << (ratio * 100.0) << "%";
}

// ---- Extensibility: old loader skips VRLE, new loader prefers VRLE ------

TEST(VoxelSetRle, NewLoaderPrefersVrleOverVoxr) {
    // Build a file with VRLE-different data than VOXR so we can tell which
    // one was used. Write: BNDS + VOXR (blue voxels) + VRLE (red voxels).
    // An old loader (VOXR-only) must return blue; a new loader must return red.
    const ivec3 mn(0, 0, 0);
    const ivec3 mx(2, 2, 2);
    const std::size_t count = 8;

    std::vector<VoxelRecord> blueVoxels(count), redVoxels(count);
    for (auto &v : blueVoxels)
        v = makeFilledVoxel(0, 0, 200);
    for (auto &v : redVoxels)
        v = makeFilledVoxel(200, 0, 0);

    auto bnds = makeBoundsChunk(mn, mx);
    auto voxrChunk = makeVoxelRecordsChunk(blueVoxels);
    auto vrleChunk = makeVoxelRecordsRleChunk(redVoxels);

    // New loader: VRLE preferred → red
    {
        const std::string path = kTmpDir + "/rle_pref_test.vxs";
        {
            std::vector<ChunkPayload> chunks;
            chunks.push_back(makeModeChunk(VoxelSetMode::DENSE));
            chunks.push_back(bnds);
            chunks.push_back(voxrChunk);
            chunks.push_back(vrleChunk);
            FileBinaryWriter fw(path);
            ASSERT_TRUE(fw.ok());
            writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks);
        } // fw flushed and closed here before reading

        auto result = loadDenseVoxelSet(path);
        ASSERT_TRUE(result.ok());
        ASSERT_EQ(result.value_.dense_.voxels_.size(), count);
        // Should have loaded VRLE (red) not VOXR (blue)
        EXPECT_EQ(result.value_.dense_.voxels_[0].color_.red_, 200);
        EXPECT_EQ(result.value_.dense_.voxels_[0].color_.blue_, 0);
        std::remove(path.c_str());
    }

    // Old loader simulation: VOXR-only file → blue
    {
        const std::string path = kTmpDir + "/rle_voxr_only_test.vxs";
        {
            std::vector<ChunkPayload> chunks;
            chunks.push_back(makeModeChunk(VoxelSetMode::DENSE));
            chunks.push_back(bnds);
            chunks.push_back(voxrChunk);
            // No VRLE chunk — simulates old-format file
            FileBinaryWriter fw(path);
            ASSERT_TRUE(fw.ok());
            writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks);
        }

        auto result = loadDenseVoxelSet(path);
        ASSERT_TRUE(result.ok());
        ASSERT_EQ(result.value_.dense_.voxels_.size(), count);
        EXPECT_EQ(result.value_.dense_.voxels_[0].color_.blue_, 200);
        EXPECT_EQ(result.value_.dense_.voxels_[0].color_.red_, 0);
        std::remove(path.c_str());
    }
}

// ---- High-level save/load round-trip -----------------------------------

TEST(VoxelSetRle, HighLevelDenseRoundTrip) {
    // saveDenseVoxelSet writes VRLE; loadDenseVoxelSet reads it back correctly
    DenseVoxelSet dense = makeHollow64Fixture();
    const std::string path = kTmpDir + "/rle_hollow64_rt.vxs";
    auto status = saveDenseVoxelSet(path, dense);
    ASSERT_TRUE(status.ok()) << status.message_;

    auto loaded = loadDenseVoxelSet(path);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    ASSERT_EQ(loaded.value_.dense_.voxels_.size(), dense.voxels_.size());
    for (std::size_t i = 0; i < dense.voxels_.size(); ++i) {
        expectVoxelEq(loaded.value_.dense_.voxels_[i], dense.voxels_[i]);
    }
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

TEST(VoxelSetRle, VrleChunkPresentInSavedFile) {
    // Verify saveDenseVoxelSet includes a VRLE chunk in the written file
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0, 0, 0);
    dense.boundsMax_ = ivec3(4, 4, 4);
    dense.voxels_.resize(dense.voxelCount());
    for (auto &v : dense.voxels_)
        v = makeFilledVoxel(100, 150, 200);

    const std::string path = kTmpDir + "/rle_chunk_present.vxs";
    ASSERT_TRUE(saveDenseVoxelSet(path, dense).ok());

    FileBinaryReader fr(path);
    ASSERT_TRUE(fr.ok());
    auto chunksR = readChunks(fr, kVoxelSetMagic, kVoxelSetVersion);
    ASSERT_TRUE(chunksR.ok());
    EXPECT_NE(findChunk(chunksR.value_, kChunkTagVoxelRecordsRle), nullptr)
        << "Expected VRLE chunk in saved file";
    EXPECT_NE(findChunk(chunksR.value_, kChunkTagVoxelRecords), nullptr)
        << "Expected VOXR chunk in saved file (for old-loader compat)";
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

} // namespace
