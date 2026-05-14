#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/voxel_set_format.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

const std::string kTmpDir = "/tmp";

// ---- Fixtures ----------------------------------------------------------

std::vector<ShapeRecord> makeFivePrimitiveFixture() {
    std::vector<ShapeRecord> records;

    ShapeRecord r1;
    r1.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE);
    r1.params_ = vec4(3.0f, 0.0f, 0.0f, 0.0f);
    r1.color_ = Color{200, 50, 25, 255};
    r1.flags_ = 1u << 3;
    r1.boneId_ = 0;
    r1.offset_ = vec3(1.0f, 2.0f, 3.0f);
    r1.rotation_ = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    r1.csgOp_ = CsgOp::NONE;
    records.push_back(r1);

    ShapeRecord r2;
    r2.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::BOX);
    r2.params_ = vec4(2.0f, 3.0f, 4.0f, 0.0f);
    r2.color_ = Color{0, 200, 0, 255};
    r2.flags_ = (1u << 3) | (1u << 0);
    r2.boneId_ = 4;
    r2.offset_ = vec3(-5.0f, 0.0f, 1.0f);
    r2.rotation_ = vec4(0.7071068f, 0.0f, 0.0f, 0.7071068f);
    r2.csgOp_ = CsgOp::UNION;
    records.push_back(r2);

    ShapeRecord r3;
    r3.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::CYLINDER);
    r3.params_ = vec4(1.5f, 6.0f, 0.0f, 0.0f);
    r3.color_ = Color{0, 0, 255, 128};
    r3.flags_ = 1u << 3;
    r3.boneId_ = 7;
    r3.offset_ = vec3(2.5f, -1.0f, 0.0f);
    r3.rotation_ = vec4(0.0f, 0.7071068f, 0.0f, 0.7071068f);
    r3.csgOp_ = CsgOp::SMOOTH_UNION;
    records.push_back(r3);

    ShapeRecord r4;
    r4.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::ELLIPSOID);
    r4.params_ = vec4(2.0f, 1.0f, 3.0f, 0.0f);
    r4.color_ = Color{255, 255, 0, 255};
    r4.flags_ = 1u << 3;
    r4.boneId_ = 0;
    r4.offset_ = vec3(0.0f, 4.0f, -2.0f);
    r4.rotation_ = vec4(0.0f, 0.0f, 0.7071068f, 0.7071068f);
    r4.csgOp_ = CsgOp::SUBTRACT;
    records.push_back(r4);

    ShapeRecord r5;
    r5.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::WEDGE);
    r5.params_ = vec4(2.0f, 2.0f, 2.0f, 1.0f);
    r5.color_ = Color{128, 64, 200, 255};
    r5.flags_ = 1u << 3;
    r5.boneId_ = 2;
    r5.offset_ = vec3(-3.0f, -3.0f, 5.0f);
    r5.rotation_ = vec4(0.5f, 0.5f, 0.5f, 0.5f);
    r5.csgOp_ = CsgOp::INTERSECT;
    records.push_back(r5);

    return records;
}

// 5×5×2 = 50 voxels; each field mixed from the slot index so byte-shift
// bugs surface in byte-compare tests.
DenseVoxelSet make50VoxelFixture() {
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0, 0, 0);
    dense.boundsMax_ = ivec3(5, 5, 2);
    const std::size_t count = dense.voxelCount(); // 50
    dense.voxels_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        VoxelRecord v;
        v.color_ = Color{
            static_cast<std::uint8_t>(i & 0xFFu),
            static_cast<std::uint8_t>((i >> 2) & 0xFFu),
            static_cast<std::uint8_t>((i >> 4) & 0xFFu),
            255,
        };
        v.material_id_ = static_cast<std::uint8_t>(i % 8);
        v.flags_ = 1u;
        v.bone_id_ = 0;
        v.pad0_ = 0;
        v.reserved_ = 0;
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
    if (!bytes.empty())
        std::fread(bytes.data(), 1, bytes.size(), fp);
    std::fclose(fp);
    return bytes;
}

// ---- Sanity: single-mode files load correctly via loadVoxelSet ---------

TEST(VoxelSetHybrid, LoadShapesOnlyFileSanity) {
    const auto shapes = makeFivePrimitiveFixture();
    const std::string path = kTmpDir + "/vxs_hybrid_shapes_sanity.vxs";
    ASSERT_TRUE(saveShapeGroup(path, shapes).ok());

    auto loaded = loadVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::SHAPES);
    ASSERT_EQ(loaded.value_.shapeRecords_.size(), shapes.size());
    EXPECT_EQ(loaded.value_.unknownShapesSkipped_, 0u);
    EXPECT_TRUE(loaded.value_.dense_.voxels_.empty());
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

TEST(VoxelSetHybrid, LoadDenseOnlyFileSanity) {
    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0);
    dense.boundsMax_ = ivec3(20, 20, 20);
    dense.voxels_.assign(dense.voxelCount(), VoxelRecord{Color{100, 150, 200, 255}, 1, 0, 0, 0, 0});
    const std::string path = kTmpDir + "/vxs_hybrid_dense_sanity.vxs";
    ASSERT_TRUE(saveDenseVoxelSet(path, dense).ok());

    auto loaded = loadVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::DENSE);
    EXPECT_TRUE(loaded.value_.shapeRecords_.empty());
    EXPECT_EQ(loaded.value_.dense_.voxelCount(), 8000u);
    EXPECT_EQ(loaded.value_.dense_.voxels_.size(), 8000u);
    EXPECT_EQ(loaded.value_.dense_.voxels_[0].color_.red_, 100);
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

// ---- Hybrid round-trip --------------------------------------------------

TEST(VoxelSetHybrid, HybridRoundTrip) {
    const auto shapes = makeFivePrimitiveFixture();
    const auto dense = make50VoxelFixture();
    const std::string path = kTmpDir + "/vxs_hybrid_roundtrip.vxs";

    ASSERT_TRUE(saveVoxelSet(path, shapes, dense).ok());

    auto loaded = loadVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::HYBRID);

    // Shape half
    ASSERT_EQ(loaded.value_.shapeRecords_.size(), 5u);
    EXPECT_EQ(loaded.value_.unknownShapesSkipped_, 0u);
    EXPECT_EQ(
        loaded.value_.shapeRecords_[0].shapeTypeId_,
        static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE)
    );
    EXPECT_EQ(loaded.value_.shapeRecords_[0].params_.x, 3.0f);
    EXPECT_EQ(
        loaded.value_.shapeRecords_[4].shapeTypeId_,
        static_cast<std::uint32_t>(IRMath::SDF::ShapeType::WEDGE)
    );
    EXPECT_EQ(loaded.value_.shapeRecords_[4].csgOp_, CsgOp::INTERSECT);

    // Dense half
    EXPECT_EQ(loaded.value_.dense_.boundsMin_, dense.boundsMin_);
    EXPECT_EQ(loaded.value_.dense_.boundsMax_, dense.boundsMax_);
    ASSERT_EQ(loaded.value_.dense_.voxels_.size(), 50u);
    for (std::size_t i = 0; i < dense.voxels_.size(); ++i) {
        EXPECT_EQ(loaded.value_.dense_.voxels_[i].material_id_, dense.voxels_[i].material_id_);
        EXPECT_EQ(loaded.value_.dense_.voxels_[i].color_.red_, dense.voxels_[i].color_.red_);
    }

    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

// ---- Byte-stable reserialization ----------------------------------------

TEST(VoxelSetHybrid, ReserializingHybridMatchesOriginalBytes) {
    const auto shapes = makeFivePrimitiveFixture();
    const auto dense = make50VoxelFixture();
    const std::string p1 = kTmpDir + "/vxs_hybrid_bytecmp_1.vxs";
    const std::string p2 = kTmpDir + "/vxs_hybrid_bytecmp_2.vxs";

    ASSERT_TRUE(saveVoxelSet(p1, shapes, dense).ok());
    auto loaded = loadVoxelSet(p1);
    ASSERT_TRUE(loaded.ok());
    ASSERT_TRUE(saveVoxelSet(p2, loaded.value_.shapeRecords_, loaded.value_.dense_).ok());

    const auto b1 = readFileBytes(p1);
    const auto b2 = readFileBytes(p2);
    EXPECT_EQ(b1, b2);
    EXPECT_FALSE(b1.empty());

    std::remove(p1.c_str());
    std::remove((p1 + ".json").c_str());
    std::remove(p2.c_str());
    std::remove((p2 + ".json").c_str());
}

// ---- JSON sidecar -------------------------------------------------------

TEST(VoxelSetHybrid, SidecarEmittedAlongsideBinary) {
    const auto shapes = makeFivePrimitiveFixture();
    const auto dense = make50VoxelFixture();
    const std::string path = kTmpDir + "/vxs_hybrid_sidecar.vxs";
    const std::string sidecarPath = path + ".json";
    std::remove(sidecarPath.c_str());

    ASSERT_TRUE(saveVoxelSet(path, shapes, dense).ok());

    const auto sidecarBytes = readFileBytes(sidecarPath);
    EXPECT_FALSE(sidecarBytes.empty());

    const std::string json(sidecarBytes.begin(), sidecarBytes.end());
    EXPECT_NE(json.find("\"version\""), std::string::npos);
    EXPECT_NE(json.find("\"mode\""), std::string::npos);
    EXPECT_NE(json.find("HYBRID"), std::string::npos);
    EXPECT_NE(json.find("\"bounds\""), std::string::npos);
    EXPECT_NE(json.find("\"material_registry_refs\""), std::string::npos);
    EXPECT_NE(json.find("\"layer_names\""), std::string::npos);
    EXPECT_NE(json.find("\"frame_count\""), std::string::npos);
    EXPECT_NE(json.find("\"shape_primitives_summary\""), std::string::npos);
    // The fixture uses SPHERE and BOX among its 5 shapes.
    EXPECT_NE(json.find("SPHERE"), std::string::npos);
    EXPECT_NE(json.find("BOX"), std::string::npos);

    std::remove(path.c_str());
    std::remove(sidecarPath.c_str());
}

// ---- Error paths (via loadVoxelSet) ------------------------------------

TEST(VoxelSetHybrid, CorruptMagicReturnsBadMagic) {
    const std::string path = kTmpDir + "/vxs_hybrid_bad_magic.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        const char header[12] = {'B', 'A', 'D', '!', 1, 0, 0, 0, 0, 0, 0, 0};
        std::fwrite(header, 1, sizeof(header), fp);
        std::fclose(fp);
    }
    auto loaded = loadVoxelSet(path);
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::BadMagic);
    std::remove(path.c_str());
}

TEST(VoxelSetHybrid, VersionTooNewReturnsVersionTooNew) {
    const std::string path = kTmpDir + "/vxs_hybrid_too_new.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        char header[12] = {};
        header[0] = 'V';
        header[1] = 'X';
        header[2] = 'S';
        header[3] = '1';
        header[4] = static_cast<char>(kVoxelSetVersion + 1u);
        std::fwrite(header, 1, sizeof(header), fp);
        std::fclose(fp);
    }
    auto loaded = loadVoxelSet(path);
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::VersionTooNew);
    std::remove(path.c_str());
}

TEST(VoxelSetHybrid, TruncatedMidVoxrReturnsTruncatedOrChunkOOB) {
    const auto shapes = makeFivePrimitiveFixture();
    const auto dense = make50VoxelFixture();
    const std::string p1 = kTmpDir + "/vxs_hybrid_full_for_trunc.vxs";
    ASSERT_TRUE(saveVoxelSet(p1, shapes, dense).ok());

    auto full = readFileBytes(p1);
    ASSERT_FALSE(full.empty());
    const std::size_t cut = full.size() > 16 ? full.size() - 12 : full.size() / 2;
    full.resize(cut);

    const std::string p2 = kTmpDir + "/vxs_hybrid_truncated.vxs";
    {
        std::FILE *fp = std::fopen(p2.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        std::fwrite(full.data(), 1, full.size(), fp);
        std::fclose(fp);
    }

    auto loaded = loadVoxelSet(p2);
    EXPECT_FALSE(loaded.ok());
    EXPECT_TRUE(
        loaded.status_.code_ == BinaryIOError::Truncated ||
        loaded.status_.code_ == BinaryIOError::ChunkOutOfBounds
    );

    std::remove(p1.c_str());
    std::remove((p1 + ".json").c_str());
    std::remove(p2.c_str());
}

TEST(VoxelSetHybrid, UnknownChunkTagSilentlySkipped) {
    const auto shapes = makeFivePrimitiveFixture();
    const auto dense = make50VoxelFixture();
    const auto shapeTypeEntries = buildCurrentShapeTypeNameTable();

    ChunkPayload future;
    future.tag_ = {'F', 'U', 'T', 'R'};
    future.data_ = {0xDEu, 0xADu, 0xBEu, 0xEFu};

    std::vector<ChunkPayload> chunks = {
        makeModeChunk(VoxelSetMode::HYBRID),
        makeShapeRefsChunk(shapeTypeEntries),
        makeShapeGroupChunk(shapes),
        makeBoundsChunk(dense.boundsMin_, dense.boundsMax_),
        makeVoxelRecordsChunk(dense.voxels_),
        future,
    };

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());

    const std::string path = kTmpDir + "/vxs_hybrid_unknown_chunk.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        std::fwrite(w.buffer().data(), 1, w.buffer().size(), fp);
        std::fclose(fp);
    }

    auto loaded = loadVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::HYBRID);
    EXPECT_EQ(loaded.value_.shapeRecords_.size(), 5u);
    ASSERT_EQ(loaded.value_.dense_.voxels_.size(), 50u);
    std::remove(path.c_str());
}

TEST(VoxelSetHybrid, UnknownShapeTypeIdLoggedAndSkipped) {
    // One SPHERE record and one record with a future ShapeType not in the
    // current build's enum. Loader keeps SPHERE and surfaces skipped=1.
    const std::uint32_t kFutureId = 9999;
    std::vector<NameTableEntry> diskEntries = {
        {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE), "SPHERE"},
        {kFutureId, "FUTURE_SHAPE"},
    };

    std::vector<ShapeRecord> shapeRecords;
    ShapeRecord sphere;
    sphere.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE);
    sphere.params_ = vec4(1.0f);
    sphere.color_ = Color{255, 255, 255, 255};
    sphere.flags_ = 1u << 3;
    shapeRecords.push_back(sphere);
    ShapeRecord future;
    future.shapeTypeId_ = kFutureId;
    future.params_ = vec4(2.0f);
    future.color_ = Color{255, 0, 0, 255};
    future.flags_ = 1u << 3;
    shapeRecords.push_back(future);

    DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0);
    dense.boundsMax_ = ivec3(2, 2, 2);
    dense.voxels_.assign(dense.voxelCount(), VoxelRecord{});

    std::vector<ChunkPayload> chunks = {
        makeModeChunk(VoxelSetMode::HYBRID),
        makeShapeRefsChunk(diskEntries),
        makeShapeGroupChunk(shapeRecords),
        makeBoundsChunk(dense.boundsMin_, dense.boundsMax_),
        makeVoxelRecordsChunk(dense.voxels_),
    };

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());

    const std::string path = kTmpDir + "/vxs_hybrid_unknown_shape.vxs";
    {
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        ASSERT_NE(fp, nullptr);
        std::fwrite(w.buffer().data(), 1, w.buffer().size(), fp);
        std::fclose(fp);
    }

    auto loaded = loadVoxelSet(path);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.shapeRecords_.size(), 1u);
    EXPECT_EQ(loaded.value_.unknownShapesSkipped_, 1u);
    EXPECT_EQ(
        loaded.value_.shapeRecords_[0].shapeTypeId_,
        static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE)
    );
    EXPECT_EQ(loaded.value_.dense_.voxels_.size(), 8u);
    std::remove(path.c_str());
}

} // namespace
