#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/voxel_set_format.hpp>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

const std::string kTmpDir = "/tmp";

void expectVoxelEq(const VoxelRecord &a, const VoxelRecord &b) {
    EXPECT_EQ(a.color_.red_, b.color_.red_);
    EXPECT_EQ(a.color_.green_, b.color_.green_);
    EXPECT_EQ(a.color_.blue_, b.color_.blue_);
    EXPECT_EQ(a.color_.alpha_, b.color_.alpha_);
    EXPECT_EQ(a.material_id_, b.material_id_);
    EXPECT_EQ(a.flags_, b.flags_);
    EXPECT_EQ(a.bone_id_, b.bone_id_);
    EXPECT_EQ(a.layer_id_, b.layer_id_);
    EXPECT_EQ(a.reserved_, b.reserved_);
}

DenseVoxelSet make20CubeFixture() {
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0, 0, 0);
    dense.boundsMax_ = ivec3(20, 20, 20);
    const std::size_t count = dense.voxelCount();
    dense.voxels_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        VoxelRecord v;
        // Mix the index across all fields so a byte-shift bug surfaces.
        v.color_ = Color{
            static_cast<std::uint8_t>(i & 0xFFu),
            static_cast<std::uint8_t>((i >> 4) & 0xFFu),
            static_cast<std::uint8_t>((i >> 8) & 0xFFu),
            255,
        };
        v.material_id_ = static_cast<std::uint8_t>(i & 0x3Fu);
        v.flags_ = static_cast<std::uint8_t>((i & 1u) ? 0x01u : 0x02u);
        v.bone_id_ = static_cast<std::uint8_t>(i & 0x0Fu);
        v.layer_id_ = 0;
        v.reserved_ = static_cast<std::uint32_t>(i * 7u);
        dense.voxels_[i] = v;
    }
    return dense;
}

std::vector<std::uint8_t> readFileBytes(const std::string &path) {
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp)
        return {};
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size < 0 ? 0 : size));
    if (!bytes.empty()) {
        std::fread(bytes.data(), 1, bytes.size(), fp);
    }
    std::fclose(fp);
    return bytes;
}

// ---- BNDS chunk --------------------------------------------------------

TEST(VoxelSetDense, BoundsChunkRoundTrip) {
    const ivec3 mn(-5, 2, -100);
    const ivec3 mx(15, 42, 7);
    auto payload = makeBoundsChunk(mn, mx);
    ASSERT_EQ(payload.tag_, kChunkTagBounds);
    ASSERT_EQ(payload.data_.size(), 24u);

    auto loaded = readBoundsChunk(payload.data_);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.boundsMin_, mn);
    EXPECT_EQ(loaded.value_.boundsMax_, mx);
}

TEST(VoxelSetDense, BoundsChunkTruncatedReturnsError) {
    std::vector<std::uint8_t> body(8, 0); // half a BNDS body
    auto loaded = readBoundsChunk(body);
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::Truncated);
}

// ---- VOXR chunk --------------------------------------------------------

TEST(VoxelSetDense, VoxelRecordIs12Bytes) {
    static_assert(sizeof(VoxelRecord) == 12, "VoxelRecord must be 12 B");
    SUCCEED();
}

TEST(VoxelSetDense, VoxelRecordsChunkRoundTrip) {
    std::vector<VoxelRecord> voxels;
    for (std::uint32_t i = 0; i < 8; ++i) {
        VoxelRecord v;
        v.color_ = Color{static_cast<std::uint8_t>(i * 32u), 100, 200, 255};
        v.material_id_ = static_cast<std::uint8_t>(i);
        v.flags_ = 0x07u;
        v.bone_id_ = static_cast<std::uint8_t>(i * 3u);
        v.layer_id_ = 0;
        v.reserved_ = i * 0xCAFEu;
        voxels.push_back(v);
    }
    auto payload = makeVoxelRecordsChunk(voxels);
    ASSERT_EQ(payload.tag_, kChunkTagVoxelRecords);

    auto loaded = readVoxelRecordsChunk(payload.data_, voxels.size());
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.recordVersion_, kVoxelRecordVersion);
    ASSERT_EQ(loaded.value_.voxels_.size(), voxels.size());
    for (std::size_t i = 0; i < voxels.size(); ++i) {
        expectVoxelEq(voxels[i], loaded.value_.voxels_[i]);
    }
}

TEST(VoxelSetDense, VoxelRecordsCountMismatchStillLoads) {
    // Loader logs a warning but does not fail when the chunk's count
    // doesn't match the bounds-derived count — Rule #5.
    std::vector<VoxelRecord> voxels(3);
    auto payload = makeVoxelRecordsChunk(voxels);
    auto loaded = readVoxelRecordsChunk(payload.data_, /*expectedCount=*/99);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.voxels_.size(), 3u);
}

// ---- LAYR chunk --------------------------------------------------------

TEST(VoxelSetDense, LayersChunkRoundTrip) {
    std::vector<LayerInfo> layers;
    LayerInfo skin;
    skin.name_ = "skin";
    skin.bitmask_ = {0xDEADBEEFCAFEBABEull, 0x0123456789ABCDEFull};
    layers.push_back(skin);
    LayerInfo metal;
    metal.name_ = "metal-plates";
    metal.bitmask_ = {0xFFFFFFFFFFFFFFFFull};
    layers.push_back(metal);

    auto payload = makeLayersChunk(layers);
    ASSERT_EQ(payload.tag_, kChunkTagLayers);

    auto loaded = readLayersChunk(payload.data_);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 2u);
    EXPECT_EQ(loaded.value_[0].name_, "skin");
    EXPECT_EQ(loaded.value_[0].bitmask_, skin.bitmask_);
    EXPECT_EQ(loaded.value_[1].name_, "metal-plates");
    EXPECT_EQ(loaded.value_[1].bitmask_, metal.bitmask_);
}

TEST(VoxelSetDense, EmptyLayersListRoundTrip) {
    std::vector<LayerInfo> layers;
    auto payload = makeLayersChunk(layers);
    auto loaded = readLayersChunk(payload.data_);
    ASSERT_TRUE(loaded.ok());
    EXPECT_TRUE(loaded.value_.empty());
}

// ---- FRAM chunk --------------------------------------------------------

TEST(VoxelSetDense, FramesChunkRoundTripWhenOffsetCountsMatch) {
    constexpr std::size_t voxelCount = 4;
    std::vector<FramePose> frames;
    FramePose f0;
    f0.frameIndex_ = 0;
    f0.offsets_ = {vec3(0, 0, 0), vec3(0, 0, 0), vec3(0, 0, 0), vec3(0, 0, 0)};
    frames.push_back(f0);
    FramePose f1;
    f1.frameIndex_ = 7;
    f1.offsets_ = {vec3(1, 2, 3), vec3(-1, 0, 5), vec3(0, 0, 0), vec3(0, 0, 0)};
    frames.push_back(f1);

    auto payload = makeFramesChunk(frames);
    ASSERT_EQ(payload.tag_, kChunkTagFrames);

    auto loaded = readFramesChunk(payload.data_, voxelCount);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.frames_.size(), 2u);
    EXPECT_EQ(loaded.value_.skippedFrames_, 0u);
    EXPECT_EQ(loaded.value_.frames_[0].frameIndex_, 0u);
    EXPECT_EQ(loaded.value_.frames_[1].frameIndex_, 7u);
    ASSERT_EQ(loaded.value_.frames_[1].offsets_.size(), 4u);
    EXPECT_EQ(loaded.value_.frames_[1].offsets_[1].x, -1.0f);
    EXPECT_EQ(loaded.value_.frames_[1].offsets_[1].z, 5.0f);
}

TEST(VoxelSetDense, FrameWithMismatchedOffsetCountIsDroppedSiblingSurvives) {
    // Construct a chunk with two frames: the first has 2 offsets (wrong)
    // and the second has 3 offsets (matches expectedCount). The loader
    // must drop the first frame, keep the second, and surface the skip.
    constexpr std::size_t voxelCount = 3;
    std::vector<FramePose> frames;
    FramePose bad;
    bad.frameIndex_ = 11;
    bad.offsets_ = {vec3(9, 9, 9), vec3(8, 8, 8)};
    frames.push_back(bad);
    FramePose good;
    good.frameIndex_ = 12;
    good.offsets_ = {vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)};
    frames.push_back(good);

    auto payload = makeFramesChunk(frames);
    auto loaded = readFramesChunk(payload.data_, voxelCount);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.skippedFrames_, 1u);
    ASSERT_EQ(loaded.value_.frames_.size(), 1u);
    EXPECT_EQ(loaded.value_.frames_[0].frameIndex_, 12u);
}

// ---- META chunk --------------------------------------------------------

TEST(VoxelSetDense, MetaChunkRoundTrip) {
    std::vector<MetaEntry> meta;
    meta.push_back({"author", "alice"});
    meta.push_back({"material_registry_version", "v3"});
    meta.push_back({"empty-value", ""});

    auto payload = makeMetaChunk(meta);
    ASSERT_EQ(payload.tag_, kChunkTagMeta);

    auto loaded = readMetaChunk(payload.data_);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), meta.size());
    for (std::size_t i = 0; i < meta.size(); ++i) {
        EXPECT_EQ(loaded.value_[i].key_, meta[i].key_);
        EXPECT_EQ(loaded.value_[i].value_, meta[i].value_);
    }
}

// ---- High-level save/load: 20³ fixture ---------------------------------

TEST(VoxelSetDense, TwentyCubeFixtureRoundTrip) {
    const auto dense = make20CubeFixture();
    const std::string path = kTmpDir + "/vxs_dense_20cube.vxs";

    ASSERT_TRUE(saveDenseVoxelSet(path, dense).ok());

    auto loaded = loadDenseVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::DENSE);
    EXPECT_EQ(loaded.value_.dense_.boundsMin_, dense.boundsMin_);
    EXPECT_EQ(loaded.value_.dense_.boundsMax_, dense.boundsMax_);
    ASSERT_EQ(loaded.value_.dense_.voxels_.size(), dense.voxels_.size());
    for (std::size_t i = 0; i < dense.voxels_.size(); ++i) {
        expectVoxelEq(dense.voxels_[i], loaded.value_.dense_.voxels_[i]);
    }
    EXPECT_TRUE(loaded.value_.dense_.layers_.empty());
    EXPECT_TRUE(loaded.value_.dense_.frames_.empty());
    EXPECT_TRUE(loaded.value_.dense_.meta_.empty());
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

TEST(VoxelSetDense, AllSectionsPopulatedRoundTrip) {
    DenseVoxelSet dense = make20CubeFixture();
    // 8000-bit bitmask — 125 u64 words.
    const std::size_t wordCount = (dense.voxelCount() + 63) / 64;
    LayerInfo layer;
    layer.name_ = "interactive";
    layer.bitmask_.assign(wordCount, 0);
    layer.bitmask_[0] = 0xAAAAAAAAAAAAAAAAull;
    layer.bitmask_[wordCount - 1] = 0x1ull;
    dense.layers_.push_back(layer);

    FramePose frame;
    frame.frameIndex_ = 42;
    frame.offsets_.assign(dense.voxelCount(), vec3(0, 0, 0));
    frame.offsets_[3] = vec3(0.5f, -1.25f, 2.0f);
    dense.frames_.push_back(frame);

    dense.meta_.push_back({"author", "bob"});

    const std::string path = kTmpDir + "/vxs_dense_full.vxs";
    ASSERT_TRUE(saveDenseVoxelSet(path, dense).ok());

    auto loaded = loadDenseVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.skippedFrames_, 0u);
    ASSERT_EQ(loaded.value_.dense_.layers_.size(), 1u);
    EXPECT_EQ(loaded.value_.dense_.layers_[0].name_, "interactive");
    EXPECT_EQ(loaded.value_.dense_.layers_[0].bitmask_, layer.bitmask_);
    ASSERT_EQ(loaded.value_.dense_.frames_.size(), 1u);
    EXPECT_EQ(loaded.value_.dense_.frames_[0].frameIndex_, 42u);
    EXPECT_EQ(loaded.value_.dense_.frames_[0].offsets_[3].x, 0.5f);
    EXPECT_EQ(loaded.value_.dense_.frames_[0].offsets_[3].y, -1.25f);
    EXPECT_EQ(loaded.value_.dense_.frames_[0].offsets_[3].z, 2.0f);
    ASSERT_EQ(loaded.value_.dense_.meta_.size(), 1u);
    EXPECT_EQ(loaded.value_.dense_.meta_[0].key_, "author");
    EXPECT_EQ(loaded.value_.dense_.meta_[0].value_, "bob");
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

TEST(VoxelSetDense, EmptyVoxelSetRoundTrip) {
    DenseVoxelSet dense;
    // Bounds (0,0,0) → (0,0,0) ⇒ zero voxels. The VOXR chunk still
    // round-trips as a zero-count blob.
    const std::string path = kTmpDir + "/vxs_dense_empty.vxs";
    ASSERT_TRUE(saveDenseVoxelSet(path, dense).ok());

    auto loaded = loadDenseVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::DENSE);
    EXPECT_EQ(loaded.value_.dense_.voxelCount(), 0u);
    EXPECT_TRUE(loaded.value_.dense_.voxels_.empty());
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

TEST(VoxelSetDense, FullVoxelSetEveryCellActive) {
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0);
    dense.boundsMax_ = ivec3(4, 4, 4);
    dense.voxels_.assign(
        dense.voxelCount(),
        VoxelRecord{Color{255, 255, 255, 255}, 1, 0x07, 0, 0, 0}
    );

    const std::string path = kTmpDir + "/vxs_dense_full4.vxs";
    ASSERT_TRUE(saveDenseVoxelSet(path, dense).ok());
    auto loaded = loadDenseVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.dense_.voxels_.size(), 64u);
    for (const auto &v : loaded.value_.dense_.voxels_) {
        EXPECT_EQ(v.color_.red_, 255);
        EXPECT_EQ(v.material_id_, 1);
        EXPECT_EQ(v.flags_, 0x07);
    }
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

// ---- Byte-stability ----------------------------------------------------

TEST(VoxelSetDense, ReSerializingLoadedFileMatchesOriginalBytes) {
    // save → load → save → bytes equal. Catches non-deterministic
    // ordering inside chunks or fields.
    DenseVoxelSet dense = make20CubeFixture();
    dense.layers_.push_back(LayerInfo{"a", {0xAAull, 0x55ull}});
    dense.meta_.push_back({"k", "v"});
    const std::string p1 = kTmpDir + "/vxs_dense_bytecmp_1.vxs";
    const std::string p2 = kTmpDir + "/vxs_dense_bytecmp_2.vxs";

    ASSERT_TRUE(saveDenseVoxelSet(p1, dense).ok());
    auto loaded = loadDenseVoxelSet(p1);
    ASSERT_TRUE(loaded.ok());
    ASSERT_TRUE(saveDenseVoxelSet(p2, loaded.value_.dense_).ok());

    const auto b1 = readFileBytes(p1);
    const auto b2 = readFileBytes(p2);
    EXPECT_EQ(b1, b2);
    EXPECT_FALSE(b1.empty());

    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove((p1 + ".json").c_str());
    std::remove((p2 + ".json").c_str());
}

// ---- Container errors --------------------------------------------------

TEST(VoxelSetDense, BadMagicSurfacesAsBadMagic) {
    const std::string path = kTmpDir + "/vxs_dense_bad_magic.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        // 12-byte header with wrong magic + valid version + 0 chunks.
        const char header[12] = {'B', 'A', 'D', '!', 1, 0, 0, 0, 0, 0, 0, 0};
        std::fwrite(header, 1, sizeof(header), fp);
        std::fclose(fp);
    }
    auto loaded = loadDenseVoxelSet(path);
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::BadMagic);
    std::remove(path.c_str());
}

TEST(VoxelSetDense, VersionTooNewSurfacesAsVersionTooNew) {
    const std::string path = kTmpDir + "/vxs_dense_too_new.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        // Valid magic but a future version number.
        char header[12] = {0};
        header[0] = 'V';
        header[1] = 'X';
        header[2] = 'S';
        header[3] = '1';
        header[4] = static_cast<char>(kVoxelSetVersion + 1u);
        std::fwrite(header, 1, sizeof(header), fp);
        std::fclose(fp);
    }
    auto loaded = loadDenseVoxelSet(path);
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::VersionTooNew);
    std::remove(path.c_str());
}

TEST(VoxelSetDense, TruncatedVoxRChunkSurfacesAsTruncated) {
    // Build a valid file in memory, then truncate it mid-VOXR.
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0);
    dense.boundsMax_ = ivec3(2, 2, 2);
    dense.voxels_.assign(dense.voxelCount(), VoxelRecord{});

    const std::string p1 = kTmpDir + "/vxs_dense_full_for_trunc.vxs";
    ASSERT_TRUE(saveDenseVoxelSet(p1, dense).ok());

    auto full = readFileBytes(p1);
    ASSERT_FALSE(full.empty());
    // Truncate ~12 bytes off the end (mid-VOXR record).
    const std::size_t cut = full.size() > 16 ? full.size() - 12 : full.size() / 2;
    full.resize(cut);

    const std::string p2 = kTmpDir + "/vxs_dense_truncated.vxs";
    {
        std::FILE *fp = std::fopen(p2.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        std::fwrite(full.data(), 1, full.size(), fp);
        std::fclose(fp);
    }

    auto loaded = loadDenseVoxelSet(p2);
    EXPECT_FALSE(loaded.ok());
    // Either ChunkOutOfBounds (truncation inside chunk table) or
    // Truncated (truncation inside chunk body). Both are recoverable
    // diagnostics per Rule #5.
    EXPECT_TRUE(
        loaded.status_.code_ == BinaryIOError::Truncated ||
        loaded.status_.code_ == BinaryIOError::ChunkOutOfBounds
    );
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove((p1 + ".json").c_str());
}

// ---- Unknown chunk silently skipped (Rule #1) --------------------------

TEST(VoxelSetDense, UnknownChunkSilentlySkipped) {
    // Hand-build a chunk list containing the standard DENSE chunks
    // plus a future "FUTR" chunk that the current loader doesn't know.
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0);
    dense.boundsMax_ = ivec3(2, 2, 2);
    dense.voxels_.assign(dense.voxelCount(), VoxelRecord{Color{1, 2, 3, 4}, 5, 6, 7, 0, 0});

    ChunkPayload future;
    future.tag_ = {'F', 'U', 'T', 'R'};
    future.data_ = {0xDEu, 0xADu, 0xBEu, 0xEFu};

    std::vector<ChunkPayload> chunks = {
        makeModeChunk(VoxelSetMode::DENSE),
        makeBoundsChunk(dense.boundsMin_, dense.boundsMax_),
        makeVoxelRecordsChunk(dense.voxels_),
        future,
    };

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());

    const std::string path = kTmpDir + "/vxs_dense_unknown_chunk.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        std::fwrite(w.buffer().data(), 1, w.buffer().size(), fp);
        std::fclose(fp);
    }

    auto loaded = loadDenseVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::DENSE);
    ASSERT_EQ(loaded.value_.dense_.voxels_.size(), 8u);
    EXPECT_EQ(loaded.value_.dense_.voxels_[0].material_id_, 5u);
    std::remove(path.c_str());
}

TEST(VoxelSetDense, BoundsPresentVoxRMissingLoadsWithEmptyVoxels) {
    // Malformed save: BNDS chunk claims a non-zero voxel volume but the
    // VOXR chunk is absent. Loader must succeed (Rule #5) with bounds
    // populated and `voxels_` empty — the doc-comment on
    // `DenseVoxelSetFile` requires the caller to validate
    // `voxels_.size() == voxelCount()` before indexing.
    std::vector<ChunkPayload> chunks = {
        makeModeChunk(VoxelSetMode::DENSE),
        makeBoundsChunk(ivec3(0), ivec3(2, 2, 2)),
        // No VOXR.
    };

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());

    const std::string path = kTmpDir + "/vxs_dense_missing_voxr.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        std::fwrite(w.buffer().data(), 1, w.buffer().size(), fp);
        std::fclose(fp);
    }

    auto loaded = loadDenseVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::DENSE);
    EXPECT_EQ(loaded.value_.dense_.voxelCount(), 8u);
    EXPECT_TRUE(loaded.value_.dense_.voxels_.empty());
    std::remove(path.c_str());
}

TEST(VoxelSetDense, SidecarEmittedAlongsideBinary) {
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(-2, 0, 1);
    dense.boundsMax_ = ivec3(3, 4, 4);
    dense.voxels_.assign(dense.voxelCount(), VoxelRecord{});
    LayerInfo layer;
    layer.name_ = "active";
    layer.bitmask_.assign((dense.voxelCount() + 63) / 64, 0);
    dense.layers_.push_back(layer);
    FramePose frame;
    frame.frameIndex_ = 0;
    frame.offsets_.assign(dense.voxelCount(), vec3(0.0f));
    dense.frames_.push_back(frame);

    const std::string path = kTmpDir + "/vxs_dense_sidecar.vxs";
    const std::string sidecarPath = path + ".json";
    std::remove(sidecarPath.c_str());

    ASSERT_TRUE(saveDenseVoxelSet(path, dense).ok());

    const auto sidecarBytes = readFileBytes(sidecarPath);
    EXPECT_FALSE(sidecarBytes.empty());

    const std::string json(sidecarBytes.begin(), sidecarBytes.end());
    EXPECT_NE(json.find("\"mode\""), std::string::npos);
    EXPECT_NE(json.find("DENSE"), std::string::npos);
    EXPECT_NE(json.find("\"bounds\""), std::string::npos);
    EXPECT_NE(json.find("\"layer_names\""), std::string::npos);
    EXPECT_NE(json.find("active"), std::string::npos);
    EXPECT_NE(json.find("\"frame_count\""), std::string::npos);
    // DENSE save passes an empty shape span — the summary object exists but
    // contains no per-type counts.
    EXPECT_NE(json.find("\"shape_primitives_summary\""), std::string::npos);
    EXPECT_EQ(json.find("SPHERE"), std::string::npos);
    EXPECT_EQ(json.find("BOX"), std::string::npos);

    std::remove(path.c_str());
    std::remove(sidecarPath.c_str());
}

} // namespace
