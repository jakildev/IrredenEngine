#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/key_value_store.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

const std::string kTmpDir = "/tmp";

KeyValueStore makeSampleStore() {
    KeyValueStore store;
    store.set("highScore", Value{12345.0});
    store.set("playerName", Value{std::string{"ACE"}});
    store.set("soundOn", Value{true});
    store.set("musicOn", Value{false});
    store.set(
        "topScores",
        Value{std::vector<ListElem>{ListElem{5000.0}, ListElem{3000.0}, ListElem{1000.0}}}
    );
    store.set(
        "recentPlayers",
        Value{std::vector<ListElem>{ListElem{std::string{"ACE"}}, ListElem{std::string{"BEE"}}}}
    );
    return store;
}

// Compares two stores key-for-key, including value types + payloads.
void expectStoresEqual(const KeyValueStore &a, const KeyValueStore &b) {
    ASSERT_EQ(a.size(), b.size());
    for (const std::string &key : a.keys()) {
        const Value *va = a.get(key);
        const Value *vb = b.get(key);
        ASSERT_NE(va, nullptr) << key;
        ASSERT_NE(vb, nullptr) << "missing key after round-trip: " << key;
        ASSERT_EQ(valueType(*va), valueType(*vb)) << key;
        EXPECT_EQ(*va, *vb) << key;
    }
}

// ---- Buffer-mode round-trips ---------------------------------------------

TEST(KeyValueStore, EmptyStoreRoundTrip) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeKeyValueStore(w, KeyValueStore{}).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readKeyValueStore(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    EXPECT_EQ(loaded.value_.size(), 0u);
}

TEST(KeyValueStore, AllValueTypesRoundTrip) {
    const KeyValueStore original = makeSampleStore();

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeKeyValueStore(w, original).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readKeyValueStore(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    expectStoresEqual(original, loaded.value_);

    // Spot-check typed accessors against the loaded copy.
    EXPECT_EQ(loaded.value_.getNumber("highScore"), 12345.0);
    EXPECT_EQ(loaded.value_.getString("playerName"), "ACE");
    EXPECT_TRUE(loaded.value_.getBool("soundOn"));
    EXPECT_FALSE(loaded.value_.getBool("musicOn"));
    const Value *list = loaded.value_.get("topScores");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(valueType(*list), ValueType::LIST);
    EXPECT_EQ(std::get<std::vector<ListElem>>(*list).size(), 3u);
}

TEST(KeyValueStore, EmptyListRoundTrip) {
    KeyValueStore original;
    original.set("noScoresYet", Value{std::vector<ListElem>{}});

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeKeyValueStore(w, original).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readKeyValueStore(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    const Value *list = loaded.value_.get("noScoresYet");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(valueType(*list), ValueType::LIST);
    EXPECT_TRUE(std::get<std::vector<ListElem>>(*list).empty());
}

// ---- In-memory store semantics -------------------------------------------

TEST(KeyValueStore, OverwriteRemoveClearAndTypedReads) {
    KeyValueStore store;

    store.set("score", Value{10.0});
    EXPECT_EQ(store.getNumber("score"), 10.0);
    store.set("score", Value{99.0}); // overwrite
    EXPECT_EQ(store.getNumber("score"), 99.0);

    // Typed read of a wrong-typed / absent key returns the fallback.
    EXPECT_EQ(store.getString("score", "n/a"), "n/a"); // score is a number
    EXPECT_EQ(store.getNumber("missing", -1.0), -1.0);
    EXPECT_FALSE(store.getBool("missing", false));

    EXPECT_TRUE(store.has("score"));
    EXPECT_TRUE(store.remove("score"));
    EXPECT_FALSE(store.has("score"));
    EXPECT_FALSE(store.remove("score")) << "removing an absent key returns false";

    store.set("a", Value{1.0});
    store.set("b", Value{2.0});
    EXPECT_EQ(store.size(), 2u);
    store.clear();
    EXPECT_EQ(store.size(), 0u);
}

// ---- Recoverable errors (Extensibility Rule #5) --------------------------

TEST(KeyValueStore, BadMagicIsRecoverable) {
    std::vector<std::uint8_t> bad(64, 0);
    bad[0] = 'X';
    bad[1] = 'X';
    bad[2] = 'X';
    bad[3] = 'X';
    MemoryBinaryReader r(bad.data(), bad.size());
    auto loaded = readKeyValueStore(r);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::BadMagic);
    EXPECT_EQ(loaded.value_.size(), 0u);
}

TEST(KeyValueStore, VersionTooNewIsRecoverable) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeKeyValueStore(w, makeSampleStore()).ok());
    auto buffer = w.takeBuffer();
    // Patch the version dword (bytes 4..7) to a future value.
    buffer[4] = 0xFF;
    buffer[5] = 0xFF;
    buffer[6] = 0xFF;
    buffer[7] = 0xFF;
    MemoryBinaryReader r(buffer.data(), buffer.size());
    auto loaded = readKeyValueStore(r);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::VersionTooNew);
}

TEST(KeyValueStore, TruncatedMidChunkIsRecoverable) {
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeKeyValueStore(w, makeSampleStore()).ok());
    auto buffer = w.takeBuffer();
    // Lop off the back half of the file so a chunk body reads past EOF. The
    // chunk-table offset/size validation or a value read surfaces the error;
    // either way it must be recoverable, not a crash.
    ASSERT_GT(buffer.size(), 20u);
    buffer.resize(buffer.size() - (buffer.size() / 2));
    MemoryBinaryReader r(buffer.data(), buffer.size());
    auto loaded = readKeyValueStore(r);
    EXPECT_FALSE(loaded.ok());
}

TEST(KeyValueStore, UnknownValueTagIsRecoverable) {
    // Hand-roll a KVPR chunk with a single entry whose value tag is a future
    // value (99). The loader can't length-skip an unknown payload, so it
    // surfaces UnknownTag rather than crashing or mis-reading.
    MemoryBinaryWriter body;
    body.writeVarUInt(1u);       // entryCount
    body.writeString("mystery"); // key
    body.writeU8(99u);           // unknown value tag
    body.writeF64(1.0);          // some payload bytes

    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{makeTag("KVPR"), body.takeBuffer()});

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kKeyValueMagic, kKeyValueFormatVersion, chunks).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readKeyValueStore(r);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::UnknownTag);
}

TEST(KeyValueStore, UnknownChunkIsSilentlySkipped) {
    // A KVPR chunk plus a truly-unknown future chunk: a v1 loader pulls KVPR
    // through and ignores the unknown (Extensibility Rule #1).
    const KeyValueStore original = makeSampleStore();
    MemoryBinaryWriter kvprBody;
    ASSERT_TRUE(writeKeyValueStore(kvprBody, original).ok());
    // Re-extract just the KVPR chunk body by re-encoding through the public
    // buffer path: read it back, then re-emit with an extra unknown chunk.
    MemoryBinaryReader probe(kvprBody.buffer().data(), kvprBody.buffer().size());
    AssetHeader header{};
    auto chunksR = readChunks(probe, kKeyValueMagic, kKeyValueFormatVersion, &header);
    ASSERT_TRUE(chunksR.ok());
    const LoadedChunk *kvpr = findChunk(chunksR.value_, makeTag("KVPR"));
    ASSERT_NE(kvpr, nullptr);

    std::vector<ChunkPayload> chunks;
    chunks.push_back(ChunkPayload{makeTag("FUTR"), {0xDE, 0xAD, 0xBE, 0xEF}});
    chunks.push_back(ChunkPayload{makeTag("KVPR"), kvpr->data_});

    MemoryBinaryWriter w;
    ASSERT_TRUE(writeChunked(w, kKeyValueMagic, kKeyValueFormatVersion, chunks).ok());

    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readKeyValueStore(r);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    expectStoresEqual(original, loaded.value_);
}

// ---- File-mode round-trip ------------------------------------------------

TEST(KeyValueStore, FileRoundTrip) {
    const std::string path = kTmpDir + "/kv_store_test.irkv";
    std::remove(path.c_str());

    const KeyValueStore original = makeSampleStore();
    ASSERT_TRUE(saveKeyValueStore(path, original).ok());

    auto loaded = loadKeyValueStore(path);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    expectStoresEqual(original, loaded.value_);
}

TEST(KeyValueStore, MissingFileIsRecoverable) {
    auto loaded = loadKeyValueStore(kTmpDir + "/definitely_not_a_real_kv_store.irkv");
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.status_.code_, BinaryIOError::OpenFailed);
    EXPECT_EQ(loaded.value_.size(), 0u);
}

TEST(KeyValueStore, SaveCreatesMissingParentDirectory) {
    // The save path must mkdir -p its parent (userDataDir / joinPath don't),
    // so a first-launch write into a never-created dir succeeds.
    const std::string dir = kTmpDir + "/ir_kv_test_nested/deeper";
    const std::string path = dir + "/settings.irkv";
    std::remove(path.c_str());

    KeyValueStore store;
    store.set("volume", Value{0.8});
    ASSERT_TRUE(saveKeyValueStore(path, store).ok());

    auto loaded = loadKeyValueStore(path);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    EXPECT_EQ(loaded.value_.getNumber("volume"), 0.8);

    std::remove(path.c_str());
}

} // namespace
