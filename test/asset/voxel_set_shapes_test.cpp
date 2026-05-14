#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/asset/voxel_set_io.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

const std::string kTmpDir = "/tmp";

std::vector<ShapeRecord> makeFivePrimitiveGroup() {
    std::vector<ShapeRecord> records;

    ShapeRecord r1;
    r1.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE);
    r1.params_ = vec4(3.0f, 0.0f, 0.0f, 0.0f);
    r1.color_ = Color{200, 50, 25, 255};
    r1.flags_ = 1u << 3; // SHAPE_FLAG_VISIBLE
    r1.boneId_ = 0;
    r1.offset_ = vec3(1.0f, 2.0f, 3.0f);
    r1.rotation_ = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    r1.csgOp_ = CsgOp::NONE;
    records.push_back(r1);

    ShapeRecord r2;
    r2.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::BOX);
    r2.params_ = vec4(2.0f, 3.0f, 4.0f, 0.0f);
    r2.color_ = Color{0, 200, 0, 255};
    r2.flags_ = (1u << 3) | (1u << 0); // VISIBLE | HOLLOW
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

void expectRecordEq(const ShapeRecord &a, const ShapeRecord &b) {
    EXPECT_EQ(a.shapeTypeId_, b.shapeTypeId_);
    EXPECT_EQ(a.recordVersion_, b.recordVersion_);
    EXPECT_EQ(a.params_.x, b.params_.x);
    EXPECT_EQ(a.params_.y, b.params_.y);
    EXPECT_EQ(a.params_.z, b.params_.z);
    EXPECT_EQ(a.params_.w, b.params_.w);
    EXPECT_EQ(a.color_.red_, b.color_.red_);
    EXPECT_EQ(a.color_.green_, b.color_.green_);
    EXPECT_EQ(a.color_.blue_, b.color_.blue_);
    EXPECT_EQ(a.color_.alpha_, b.color_.alpha_);
    EXPECT_EQ(a.flags_, b.flags_);
    EXPECT_EQ(a.boneId_, b.boneId_);
    EXPECT_EQ(a.offset_.x, b.offset_.x);
    EXPECT_EQ(a.offset_.y, b.offset_.y);
    EXPECT_EQ(a.offset_.z, b.offset_.z);
    EXPECT_EQ(a.rotation_.x, b.rotation_.x);
    EXPECT_EQ(a.rotation_.y, b.rotation_.y);
    EXPECT_EQ(a.rotation_.z, b.rotation_.z);
    EXPECT_EQ(a.rotation_.w, b.rotation_.w);
    EXPECT_EQ(a.csgOp_, b.csgOp_);
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

// ---- MODE chunk --------------------------------------------------------

TEST(VoxelSetFormat, ModeChunkRoundTripsAllKnownModes) {
    for (auto mode : {VoxelSetMode::DENSE, VoxelSetMode::SHAPES, VoxelSetMode::HYBRID}) {
        ChunkPayload mp = makeModeChunk(mode);
        ASSERT_EQ(mp.tag_, kChunkTagMode);
        ASSERT_EQ(mp.data_.size(), 4u);
        std::vector<LoadedChunk> chunks = {LoadedChunk{mp.tag_, mp.data_}};
        EXPECT_EQ(readModeChunk(chunks), mode);
    }
}

TEST(VoxelSetFormat, ModeChunkAbsentDefaultsToShapes) {
    std::vector<LoadedChunk> empty;
    EXPECT_EQ(readModeChunk(empty), VoxelSetMode::SHAPES);
}

TEST(VoxelSetFormat, ModeChunkUnknownTagReturnsUnknown) {
    LoadedChunk lc;
    lc.tag_ = kChunkTagMode;
    lc.data_ = {'F', 'U', 'T', 'R'};
    std::vector<LoadedChunk> chunks = {lc};
    EXPECT_EQ(readModeChunk(chunks), VoxelSetMode::UNKNOWN);
}

// ---- SREF chunk --------------------------------------------------------

TEST(VoxelSetFormat, SrefChunkRoundTripsCurrentBuildEnum) {
    auto entries = buildCurrentShapeTypeNameTable();
    EXPECT_GE(entries.size(), 10u);

    ChunkPayload payload = makeShapeRefsChunk(entries);
    EXPECT_EQ(payload.tag_, kChunkTagShapeRefs);

    auto loaded = readShapeRefsChunk(payload.data_);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(loaded.value_[i].id_, entries[i].id_);
        EXPECT_EQ(loaded.value_[i].name_, entries[i].name_);
    }
}

// ---- SHPG chunk: round-trip --------------------------------------------

TEST(VoxelSetFormat, FivePrimitiveGroupRoundTrip) {
    const auto records = makeFivePrimitiveGroup();
    const std::string path = kTmpDir + "/vxs_shape_group_roundtrip.vxs";

    ASSERT_TRUE(saveShapeGroup(path, records).ok());

    auto loaded = loadShapeGroup(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::SHAPES);
    EXPECT_EQ(loaded.value_.unknownShapesSkipped_, 0u);
    ASSERT_EQ(loaded.value_.shapeRecords_.size(), records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        expectRecordEq(records[i], loaded.value_.shapeRecords_[i]);
    }
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

TEST(VoxelSetFormat, ReSerializingLoadedFileMatchesOriginalBytes) {
    // Byte-compare: save once, load, save again — second file is byte-
    // identical to the first. Catches non-deterministic ordering in the
    // chunk-table or record loop.
    const auto records = makeFivePrimitiveGroup();
    const std::string p1 = kTmpDir + "/vxs_shape_group_bytecmp_1.vxs";
    const std::string p2 = kTmpDir + "/vxs_shape_group_bytecmp_2.vxs";

    ASSERT_TRUE(saveShapeGroup(p1, records).ok());
    auto loaded = loadShapeGroup(p1);
    ASSERT_TRUE(loaded.ok());
    ASSERT_TRUE(saveShapeGroup(p2, loaded.value_.shapeRecords_).ok());

    const auto b1 = readFileBytes(p1);
    const auto b2 = readFileBytes(p2);
    EXPECT_EQ(b1, b2);
    EXPECT_FALSE(b1.empty());

    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove((p1 + ".json").c_str());
    std::remove((p2 + ".json").c_str());
}

TEST(VoxelSetFormat, EmptyShapeGroupRoundTrip) {
    const std::string path = kTmpDir + "/vxs_shape_group_empty.vxs";
    ASSERT_TRUE(saveShapeGroup(path, {}).ok());

    auto loaded = loadShapeGroup(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.mode_, VoxelSetMode::SHAPES);
    EXPECT_TRUE(loaded.value_.shapeRecords_.empty());
    EXPECT_EQ(loaded.value_.unknownShapesSkipped_, 0u);
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());
}

TEST(VoxelSetFormat, SidecarEmittedAlongsideShapeGroupBinary) {
    const auto records = makeFivePrimitiveGroup();
    const std::string path = kTmpDir + "/vxs_shape_group_sidecar.vxs";
    const std::string sidecarPath = path + ".json";
    std::remove(sidecarPath.c_str());

    ASSERT_TRUE(saveShapeGroup(path, records).ok());

    const auto sidecarBytes = readFileBytes(sidecarPath);
    EXPECT_FALSE(sidecarBytes.empty());

    const std::string json(sidecarBytes.begin(), sidecarBytes.end());
    EXPECT_NE(json.find("\"mode\""), std::string::npos);
    EXPECT_NE(json.find("SHAPES"), std::string::npos);
    // SHAPES save passes dense=nullptr — bounds collapses to null and the
    // frame_count + layer_names sections degrade to defaults.
    EXPECT_NE(json.find("\"bounds\""), std::string::npos);
    EXPECT_NE(json.find("null"), std::string::npos);
    EXPECT_NE(json.find("\"frame_count\""), std::string::npos);
    EXPECT_NE(json.find("\"shape_primitives_summary\""), std::string::npos);
    EXPECT_NE(json.find("SPHERE"), std::string::npos);
    EXPECT_NE(json.find("BOX"), std::string::npos);

    std::remove(path.c_str());
    std::remove(sidecarPath.c_str());
}

// ---- SHPG chunk: forward-compat ----------------------------------------

TEST(VoxelSetFormat, UnknownShapeTypeIdSkippedWithCount) {
    // Hand-craft a file with one known SPHERE record and one record
    // whose disk ShapeType id maps to a name not in the current build's
    // enum (simulates a future "TORUS_KNOT" save loaded on an older
    // build). The loader keeps the SPHERE record and counts the skipped
    // one — Save Format Extensibility Rule #2.
    const std::uint32_t kFutureId = 9999;
    const std::string kFutureName = "TORUS_KNOT";

    std::vector<NameTableEntry> diskEntries = {
        {static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE), "SPHERE"},
        {kFutureId, kFutureName},
    };

    std::vector<ShapeRecord> records;
    ShapeRecord sphere;
    sphere.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE);
    sphere.params_ = vec4(1.0f, 0.0f, 0.0f, 0.0f);
    sphere.color_ = Color{255, 255, 255, 255};
    sphere.flags_ = 1u << 3;
    records.push_back(sphere);

    ShapeRecord future;
    future.shapeTypeId_ = kFutureId;
    future.params_ = vec4(2.0f, 0.5f, 0.0f, 0.0f);
    future.color_ = Color{255, 0, 0, 255};
    future.flags_ = 1u << 3;
    records.push_back(future);

    MemoryBinaryWriter w;
    std::array<ChunkPayload, 3> chunks = {
        makeModeChunk(VoxelSetMode::SHAPES),
        makeShapeRefsChunk(diskEntries),
        makeShapeGroupChunk(records),
    };
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());

    // Load it through the chunk reader directly (no on-disk file).
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r, kVoxelSetMagic, kVoxelSetVersion);
    ASSERT_TRUE(chunksR.ok());

    auto srefChunk = findChunk(chunksR.value_, kChunkTagShapeRefs);
    ASSERT_NE(srefChunk, nullptr);
    auto diskRefs = readShapeRefsChunk(srefChunk->data_);
    ASSERT_TRUE(diskRefs.ok());
    NameTable diskShapeTypes(diskRefs.value_);

    auto shpgChunk = findChunk(chunksR.value_, kChunkTagShapeGroup);
    ASSERT_NE(shpgChunk, nullptr);
    auto loadedR = readShapeGroupChunk(shpgChunk->data_, diskShapeTypes);
    ASSERT_TRUE(loadedR.ok());
    EXPECT_EQ(loadedR.value_.records_.size(), 1u);
    EXPECT_EQ(loadedR.value_.unknownShapesSkipped_, 1u);
    EXPECT_EQ(
        loadedR.value_.records_[0].shapeTypeId_,
        static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE)
    );
}

TEST(VoxelSetFormat, FutureBuildRenamedSphereResolvesByName) {
    // Save written by a future build that gave SPHERE a different
    // numeric id (e.g. enum reorder). Current build's name table
    // still has "SPHERE" — the loader resolves by name and maps to
    // the current numeric id (Rule #2 happy path).
    const std::uint32_t kRenumberedSphereId = 42;

    std::vector<NameTableEntry> diskEntries = {{kRenumberedSphereId, "SPHERE"}};

    std::vector<ShapeRecord> records;
    ShapeRecord r;
    r.shapeTypeId_ = kRenumberedSphereId;
    r.params_ = vec4(2.5f, 0.0f, 0.0f, 0.0f);
    r.color_ = Color{100, 100, 100, 255};
    r.flags_ = 1u << 3;
    records.push_back(r);

    MemoryBinaryWriter w;
    std::array<ChunkPayload, 3> chunks = {
        makeModeChunk(VoxelSetMode::SHAPES),
        makeShapeRefsChunk(diskEntries),
        makeShapeGroupChunk(records),
    };
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());

    MemoryBinaryReader r2(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r2, kVoxelSetMagic, kVoxelSetVersion);
    ASSERT_TRUE(chunksR.ok());

    auto diskRefs = readShapeRefsChunk(findChunk(chunksR.value_, kChunkTagShapeRefs)->data_);
    ASSERT_TRUE(diskRefs.ok());
    NameTable diskShapeTypes(diskRefs.value_);

    auto loadedR =
        readShapeGroupChunk(findChunk(chunksR.value_, kChunkTagShapeGroup)->data_, diskShapeTypes);
    ASSERT_TRUE(loadedR.ok());
    EXPECT_EQ(loadedR.value_.unknownShapesSkipped_, 0u);
    ASSERT_EQ(loadedR.value_.records_.size(), 1u);
    // Resolved to the CURRENT build's SPHERE id, not the disk-side 42.
    EXPECT_EQ(
        loadedR.value_.records_[0].shapeTypeId_,
        static_cast<std::uint32_t>(IRMath::SDF::ShapeType::SPHERE)
    );
}

TEST(VoxelSetFormat, LegacySaveWithoutSrefPassesIdsVerbatim) {
    // A future-tolerant loader should still handle a save that omits
    // the SREF chunk: fall back to "the disk id IS the current id"
    // mode. Used for tests / asset-tool-generated minimal files.
    std::vector<ShapeRecord> records;
    ShapeRecord r;
    r.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::BOX);
    r.params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    r.color_ = Color{50, 50, 50, 255};
    records.push_back(r);

    MemoryBinaryWriter w;
    std::array<ChunkPayload, 2> chunks = {
        makeModeChunk(VoxelSetMode::SHAPES),
        makeShapeGroupChunk(records),
    };
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());

    MemoryBinaryReader r2(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r2, kVoxelSetMagic, kVoxelSetVersion);
    ASSERT_TRUE(chunksR.ok());

    NameTable empty;
    auto loadedR =
        readShapeGroupChunk(findChunk(chunksR.value_, kChunkTagShapeGroup)->data_, empty);
    ASSERT_TRUE(loadedR.ok());
    EXPECT_EQ(loadedR.value_.unknownShapesSkipped_, 0u);
    ASSERT_EQ(loadedR.value_.records_.size(), 1u);
    EXPECT_EQ(
        loadedR.value_.records_[0].shapeTypeId_,
        static_cast<std::uint32_t>(IRMath::SDF::ShapeType::BOX)
    );
}

// ---- High-level load: error paths --------------------------------------

TEST(VoxelSetFormat, LoadMissingFileReturnsOpenFailed) {
    auto loaded = loadShapeGroup(kTmpDir + "/this_file_does_not_exist.vxs");
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::OpenFailed);
}

TEST(VoxelSetFormat, LoadBadMagicReturnsBadMagic) {
    const std::string path = kTmpDir + "/vxs_bad_magic.vxs";
    {
        FileBinaryWriter fw(path);
        ASSERT_TRUE(fw.ok());
        // Write a wrong magic; rest of the header doesn't matter.
        fw.writeBytes("WRNG", 4);
        fw.writeU32(1);
        fw.writeU32(0);
    }
    auto loaded = loadShapeGroup(path);
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::BadMagic);
    std::remove(path.c_str());
}

TEST(VoxelSetFormat, LoadVersionTooNewReturnsVersionTooNew) {
    const std::string path = kTmpDir + "/vxs_future_version.vxs";
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kVoxelSetMagic, 99, {}).ok());
    {
        FileBinaryWriter fw(path);
        ASSERT_TRUE(fw.ok());
        fw.writeBytes(w.buffer().data(), w.buffer().size());
    }
    auto loaded = loadShapeGroup(path);
    EXPECT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::VersionTooNew);
    std::remove(path.c_str());
}

TEST(VoxelSetFormat, FutureChunkTagSilentlySkipped) {
    // A future writer may add a SHPT (shape tree) chunk. Older loaders
    // must tolerate it without erroring — Save Format Extensibility
    // Rule #1.
    std::vector<ShapeRecord> records;
    ShapeRecord r;
    r.shapeTypeId_ = static_cast<std::uint32_t>(IRMath::SDF::ShapeType::BOX);
    r.params_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    records.push_back(r);

    std::array<ChunkPayload, 4> chunks = {
        makeModeChunk(VoxelSetMode::SHAPES),
        makeShapeRefsChunk(buildCurrentShapeTypeNameTable()),
        makeShapeGroupChunk(records),
        ChunkPayload{makeTag("SHPT"), {0xDE, 0xAD, 0xBE, 0xEF}}, // future
    };

    const std::string path = kTmpDir + "/vxs_future_chunk.vxs";
    {
        FileBinaryWriter fw(path);
        ASSERT_TRUE(fw.ok());
        ASSERT_TRUE(writeChunked(fw, kVoxelSetMagic, kVoxelSetVersion, chunks).ok());
    }

    auto loaded = loadShapeGroup(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value_.shapeRecords_.size(), 1u);
    std::remove(path.c_str());
}

} // namespace
