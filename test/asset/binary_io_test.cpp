#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/json_sidecar.hpp>
#include <irreden/asset/name_table.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

const std::string kTmpDir = "/tmp";

// ---- BinaryWriter / BinaryReader primitive round-trips ------------------

TEST(BinaryIO, RoundTripIntegerPrimitives) {
    MemoryBinaryWriter w;
    w.writeU8(0x12);
    w.writeU16(0x1234);
    w.writeU32(0xDEADBEEFu);
    w.writeU64(0x0123456789ABCDEFull);
    w.writeI8(-1);
    w.writeI16(-12345);
    w.writeI32(-987654321);
    w.writeI64(-1234567890123456789ll);

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    EXPECT_EQ(r.readU8().value_, 0x12u);
    EXPECT_EQ(r.readU16().value_, 0x1234u);
    EXPECT_EQ(r.readU32().value_, 0xDEADBEEFu);
    EXPECT_EQ(r.readU64().value_, 0x0123456789ABCDEFull);
    EXPECT_EQ(r.readI8().value_, -1);
    EXPECT_EQ(r.readI16().value_, -12345);
    EXPECT_EQ(r.readI32().value_, -987654321);
    EXPECT_EQ(r.readI64().value_, -1234567890123456789ll);
    EXPECT_EQ(r.remaining(), 0u);
}

TEST(BinaryIO, RoundTripFloatsPreservesBits) {
    MemoryBinaryWriter w;
    w.writeF32(3.14159f);
    w.writeF32(-0.0f);
    w.writeF32(std::numeric_limits<float>::infinity());
    w.writeF64(2.718281828459045);
    w.writeF64(std::numeric_limits<double>::lowest());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    EXPECT_FLOAT_EQ(r.readF32().value_, 3.14159f);
    // -0.0 vs 0.0: compare bit patterns.
    float zero = r.readF32().value_;
    EXPECT_EQ(std::signbit(zero), 1);
    EXPECT_EQ(zero, 0.0f);
    EXPECT_TRUE(std::isinf(r.readF32().value_));
    EXPECT_DOUBLE_EQ(r.readF64().value_, 2.718281828459045);
    EXPECT_DOUBLE_EQ(r.readF64().value_, std::numeric_limits<double>::lowest());
}

TEST(BinaryIO, RoundTripLittleEndianBytePattern) {
    // Independent-of-host: the on-disk bytes for 0xDEADBEEF in little-endian
    // are EF BE AD DE. Verify the writer produces that exact pattern.
    MemoryBinaryWriter w;
    w.writeU32(0xDEADBEEFu);
    ASSERT_EQ(w.buffer().size(), 4u);
    EXPECT_EQ(w.buffer()[0], 0xEFu);
    EXPECT_EQ(w.buffer()[1], 0xBEu);
    EXPECT_EQ(w.buffer()[2], 0xADu);
    EXPECT_EQ(w.buffer()[3], 0xDEu);
}

TEST(BinaryIO, VarUIntEdgeCases) {
    struct Case {
        std::uint64_t value_;
        std::size_t expectedBytes_;
    };
    const Case cases[] = {
        {0, 1},
        {1, 1},
        {127, 1},
        {128, 2},
        {16383, 2},
        {16384, 3},
        {std::numeric_limits<std::uint32_t>::max(), 5},
        {std::numeric_limits<std::uint64_t>::max(), 10},
    };
    for (const auto &c : cases) {
        MemoryBinaryWriter w;
        w.writeVarUInt(c.value_);
        EXPECT_EQ(w.buffer().size(), c.expectedBytes_) << "value=" << c.value_;
        MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
        auto got = r.readVarUInt();
        ASSERT_TRUE(got.ok()) << got.status_.message_;
        EXPECT_EQ(got.value_, c.value_);
        EXPECT_EQ(r.remaining(), 0u);
    }
}

TEST(BinaryIO, VarUIntWithNoTerminatorErrors) {
    // 11 bytes all with continuation bit set — never terminates.
    std::vector<std::uint8_t> bad(11, 0xFF);
    MemoryBinaryReader r(bad.data(), bad.size());
    auto got = r.readVarUInt();
    EXPECT_FALSE(got.ok());
    EXPECT_EQ(got.status_.code_, BinaryIOError::InvalidVarUInt);
}

TEST(BinaryIO, RoundTripStrings) {
    MemoryBinaryWriter w;
    w.writeString("");
    w.writeString("hello");
    w.writeString("UTF-8: café — emoji 🦀 — newline\nand tab\there");

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto a = r.readString();
    auto b = r.readString();
    auto c = r.readString();
    ASSERT_TRUE(a.ok());
    ASSERT_TRUE(b.ok());
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(a.value_, "");
    EXPECT_EQ(b.value_, "hello");
    EXPECT_EQ(c.value_, "UTF-8: café — emoji 🦀 — newline\nand tab\there");
}

TEST(BinaryIO, ReadTruncated) {
    std::vector<std::uint8_t> buf = {0x01, 0x02};
    MemoryBinaryReader r(buf.data(), buf.size());
    EXPECT_TRUE(r.readU8().ok());
    auto u32 = r.readU32(); // only 1 byte left
    EXPECT_FALSE(u32.ok());
    EXPECT_EQ(u32.status_.code_, BinaryIOError::Truncated);
}

TEST(BinaryIO, StringLengthExceedsBufferReturnsInvalid) {
    // Write a varuint length of 100 then no body.
    MemoryBinaryWriter w;
    w.writeVarUInt(100);
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto s = r.readString();
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.status_.code_, BinaryIOError::InvalidString);
}

TEST(BinaryIO, FileBackendRoundTrip) {
    const std::string path = kTmpDir + "/ir_test_binary_io_file.bin";
    {
        FileBinaryWriter w(path);
        ASSERT_TRUE(w.ok());
        w.writeU32(0xCAFEBABEu);
        w.writeString("trixel");
        w.writeF64(1.5);
    }
    {
        FileBinaryReader r(path);
        ASSERT_TRUE(r.ok());
        EXPECT_EQ(r.readU32().value_, 0xCAFEBABEu);
        EXPECT_EQ(r.readString().value_, "trixel");
        EXPECT_DOUBLE_EQ(r.readF64().value_, 1.5);
    }
    std::remove(path.c_str());
}

// ---- Chunk header tests -------------------------------------------------

TEST(ChunkHeader, RoundTripWithMultipleChunks) {
    const auto magic = makeTag("IRVS");
    std::vector<std::uint8_t> bndsData = {0x10, 0x20, 0x30};
    std::vector<std::uint8_t> voxrData = {0xAA, 0xBB, 0xCC, 0xDD};
    std::vector<ChunkPayload> chunks = {
        {makeTag("BNDS"), bndsData},
        {makeTag("VOXR"), voxrData},
    };

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, magic, 1, chunks).ok());

    // Header (12 bytes) + 2 entries (40 bytes each = 80 bytes total
    // for chunkCount=2... wait: 4+8+8 = 20 each) + body.
    // Expected total = 12 + 2*20 + 3 + 4 = 59.
    EXPECT_EQ(w.buffer().size(), 12u + 2u * 20u + 3u + 4u);

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    AssetHeader header;
    auto chunksR = readChunks(r, magic, 1, &header);
    ASSERT_TRUE(chunksR.ok()) << chunksR.status_.message_;
    ASSERT_EQ(chunksR.value_.size(), 2u);
    EXPECT_EQ(header.version_, 1u);
    EXPECT_EQ(header.chunkCount_, 2u);
    EXPECT_EQ(chunksR.value_[0].data_, bndsData);
    EXPECT_EQ(chunksR.value_[1].data_, voxrData);

    auto *bnds = findChunk(chunksR.value_, makeTag("BNDS"));
    auto *voxr = findChunk(chunksR.value_, makeTag("VOXR"));
    auto *miss = findChunk(chunksR.value_, makeTag("MISS"));
    ASSERT_NE(bnds, nullptr);
    ASSERT_NE(voxr, nullptr);
    EXPECT_EQ(miss, nullptr);
    EXPECT_EQ(bnds->data_, bndsData);
}

TEST(ChunkHeader, BadMagicReturnsError) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, makeTag("WRNG"), 1, {}).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r, makeTag("IRVS"), 1);
    EXPECT_FALSE(chunksR.ok());
    EXPECT_EQ(chunksR.status_.code_, BinaryIOError::BadMagic);
}

TEST(ChunkHeader, VersionTooNewReturnsError) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, makeTag("IRVS"), 99, {}).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r, makeTag("IRVS"), 1);
    EXPECT_FALSE(chunksR.ok());
    EXPECT_EQ(chunksR.status_.code_, BinaryIOError::VersionTooNew);
}

TEST(ChunkHeader, TruncatedHeaderReturnsTruncated) {
    std::vector<std::uint8_t> tiny = {'I', 'R', 'V', 'S'}; // only magic, no version/count
    MemoryBinaryReader r(tiny.data(), tiny.size());
    auto chunksR = readChunks(r, makeTag("IRVS"), 1);
    EXPECT_FALSE(chunksR.ok());
    EXPECT_EQ(chunksR.status_.code_, BinaryIOError::Truncated);
}

TEST(ChunkHeader, UnknownChunkTagSurvivesAsRawBytes) {
    // Save written by a future build with a new chunk tag the current
    // loader doesn't know. The reader must still produce the bytes so
    // the caller can skip-without-erroring (Extensibility Rule #1).
    std::vector<std::uint8_t> futureData = {0xDE, 0xAD};
    std::vector<ChunkPayload> chunks = {
        {makeTag("KNWN"), {0x01, 0x02}},
        {makeTag("NEWX"), futureData}, // hypothetical future chunk
    };

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, makeTag("IRVS"), 1, chunks).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r, makeTag("IRVS"), 1);
    ASSERT_TRUE(chunksR.ok());
    // Older loader walks all chunks, processes KNWN, surfaces NEWX as-is.
    const LoadedChunk *unknown = findChunk(chunksR.value_, makeTag("NEWX"));
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->data_, futureData);
}

TEST(ChunkHeader, ChunkTableOffsetOutOfBoundsRejected) {
    // Hand-craft a file with a chunk offset past EOF.
    MemoryBinaryWriter w;
    const auto magic = makeTag("IRVS");
    w.writeBytes(magic.data(), 4);
    w.writeU32(1); // version
    w.writeU32(1); // chunkCount
    w.writeBytes(makeTag("BNDS").data(), 4);
    w.writeU64(9999); // offset past EOF
    w.writeU64(10);   // size
    // No body — chunk offset+size would exceed buffer.

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r, magic, 1);
    EXPECT_FALSE(chunksR.ok());
    EXPECT_EQ(chunksR.status_.code_, BinaryIOError::ChunkOutOfBounds);
}

TEST(ChunkHeader, EmptyChunksRoundTrip) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, makeTag("EMPT"), 1, {}).ok());
    EXPECT_EQ(w.buffer().size(), 12u);

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto chunksR = readChunks(r, makeTag("EMPT"), 1);
    ASSERT_TRUE(chunksR.ok());
    EXPECT_TRUE(chunksR.value_.empty());
}

// ---- Name table tests --------------------------------------------------

TEST(NameTable, RoundTrip) {
    std::vector<NameTableEntry> entries = {
        {0, "SPHERE"},
        {1, "CUBE"},
        {7, "CYLINDER"},
        {12, "TORUS_KNOT"}, // future enum value
    };

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(loaded.value_[i].id_, entries[i].id_);
        EXPECT_EQ(loaded.value_[i].name_, entries[i].name_);
    }
}

TEST(NameTable, BidirectionalLookup) {
    NameTable nt;
    nt.add(0, "SPHERE");
    nt.add(1, "CUBE");
    nt.add(7, "CYLINDER");

    EXPECT_EQ(nt.idByName("CUBE").value(), 1u);
    EXPECT_EQ(nt.idByName("CYLINDER").value(), 7u);
    EXPECT_FALSE(nt.idByName("TORUS_KNOT").has_value());

    EXPECT_EQ(nt.nameById(0).value(), "SPHERE");
    EXPECT_EQ(nt.nameById(7).value(), "CYLINDER");
    EXPECT_FALSE(nt.nameById(99).has_value());
}

TEST(NameTable, UnknownIdSurfacesAsLookupMiss) {
    // Saved by a future build with shape id 12. Current build's enum has
    // only 0..1. After load, the consumer prefers name→current_enum lookup;
    // when both name and id are unknown, the consumer skips the entry.
    std::vector<NameTableEntry> entries = {{12, "TORUS_KNOT"}};
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    NameTable nt(loaded.value_);

    // Consumer-side build knows SPHERE=0, CUBE=1. Look up TORUS_KNOT → not in
    // the consumer's enum, so the loader logs "unknown shape, skipped" — but
    // the disk-side name table still contains it for diagnostics.
    EXPECT_EQ(nt.nameById(12).value(), "TORUS_KNOT");
    EXPECT_FALSE(nt.idByName("SPHERE").has_value()); // not in this disk table
}

TEST(NameTable, EmptyTableRoundTrip) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, {}).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    EXPECT_TRUE(loaded.value_.empty());
}

// ---- JSON sidecar tests ------------------------------------------------

TEST(JsonSidecar, FlatObject) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("version");
    j.valueInt(1);
    j.key("name");
    j.valueString("torus");
    j.key("count");
    j.valueUInt(42);
    j.endObject();

    const std::string out = j.str();
    // Pretty-printed but structural: contains keys and types.
    EXPECT_NE(out.find("\"version\": 1"), std::string::npos);
    EXPECT_NE(out.find("\"name\": \"torus\""), std::string::npos);
    EXPECT_NE(out.find("\"count\": 42"), std::string::npos);
    EXPECT_EQ(out.front(), '{');
    EXPECT_EQ(out.back(), '}');
}

TEST(JsonSidecar, NestedObjectsAndArrays) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("voxels");
    j.beginArray();
    j.beginObject();
    j.key("xyz");
    j.beginArray();
    j.valueInt(0);
    j.valueInt(1);
    j.valueInt(2);
    j.endArray();
    j.key("color");
    j.valueString("#ff8800");
    j.endObject();
    j.endArray();
    j.endObject();

    const std::string out = j.str();
    // Just make sure brackets nest correctly and key/value pairs appear.
    EXPECT_NE(out.find("\"voxels\""), std::string::npos);
    EXPECT_NE(out.find("\"xyz\""), std::string::npos);
    EXPECT_NE(out.find("[\n"), std::string::npos);
    EXPECT_NE(out.find("\"#ff8800\""), std::string::npos);
}

TEST(JsonSidecar, StringEscaping) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("path");
    j.valueString("C:\\foo\\bar");
    j.key("notes");
    j.valueString("line1\nline2\ttab\"quote");
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\"C:\\\\foo\\\\bar\""), std::string::npos);
    EXPECT_NE(out.find("\\n"), std::string::npos);
    EXPECT_NE(out.find("\\t"), std::string::npos);
    EXPECT_NE(out.find("\\\""), std::string::npos);
}

TEST(JsonSidecar, BoolNullFloat) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("enabled");
    j.valueBool(true);
    j.key("disabled");
    j.valueBool(false);
    j.key("missing");
    j.valueNull();
    j.key("ratio");
    j.valueFloat(0.5);
    j.key("nan");
    j.valueFloat(std::nan(""));
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\"enabled\": true"), std::string::npos);
    EXPECT_NE(out.find("\"disabled\": false"), std::string::npos);
    EXPECT_NE(out.find("\"missing\": null"), std::string::npos);
    EXPECT_NE(out.find("\"ratio\": 0.5"), std::string::npos);
    // NaN emits as null per the implementation.
    EXPECT_NE(out.find("\"nan\": null"), std::string::npos);
}

TEST(JsonSidecar, WriteToFile) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("v");
    j.valueInt(1);
    j.endObject();
    const std::string path = kTmpDir + "/ir_test_json_sidecar.json";
    ASSERT_TRUE(writeJsonSidecarToFile(path, j.str()));
    std::FILE *f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    char buf[64];
    const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';
    EXPECT_NE(std::strstr(buf, "\"v\": 1"), nullptr);
    std::remove(path.c_str());
}

} // namespace
