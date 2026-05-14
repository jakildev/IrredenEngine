#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/name_table.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

TEST(NameTable, SingleEntryRoundTrip) {
    std::vector<NameTableEntry> entries = {{42, "SINGLE"}};
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 1u);
    EXPECT_EQ(loaded.value_[0].id_, 42u);
    EXPECT_EQ(loaded.value_[0].name_, "SINGLE");
}

TEST(NameTable, ManyEntriesRoundTrip) {
    std::vector<NameTableEntry> entries;
    for (std::uint32_t i = 0; i < 100; ++i) {
        entries.push_back({i, "SHAPE_" + std::to_string(i)});
    }
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 100u);
    for (std::size_t i = 0; i < 100; ++i) {
        EXPECT_EQ(loaded.value_[i].id_, static_cast<std::uint32_t>(i));
        EXPECT_EQ(loaded.value_[i].name_, "SHAPE_" + std::to_string(i));
    }
}

TEST(NameTable, NonAsciiNamesUtf8) {
    // Non-ASCII UTF-8 bytes pass through the string/varuint layer unchanged.
    std::vector<NameTableEntry> entries = {
        {1, "\xc3\x84rger"},                               // Ärger (German)
        {2, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},      // 日本語 (Japanese)
    };
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 2u);
    EXPECT_EQ(loaded.value_[0].name_, entries[0].name_);
    EXPECT_EQ(loaded.value_[1].name_, entries[1].name_);
}

TEST(NameTable, EmptyNameEntryRoundTrip) {
    // Empty name string: the format does not forbid it; lock current behavior.
    std::vector<NameTableEntry> entries = {{0, ""}};
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 1u);
    EXPECT_EQ(loaded.value_[0].id_, 0u);
    EXPECT_EQ(loaded.value_[0].name_, "");
}

TEST(NameTable, ConstructFromVectorMatchesSequentialAdd) {
    std::vector<NameTableEntry> entries = {{1, "SPHERE"}, {3, "TORUS"}, {7, "BOX"}};
    NameTable fromVector(entries);

    NameTable fromAdd;
    fromAdd.add(1, "SPHERE");
    fromAdd.add(3, "TORUS");
    fromAdd.add(7, "BOX");

    EXPECT_EQ(fromVector.idByName("SPHERE"), fromAdd.idByName("SPHERE"));
    EXPECT_EQ(fromVector.idByName("TORUS"),  fromAdd.idByName("TORUS"));
    EXPECT_EQ(fromVector.idByName("BOX"),    fromAdd.idByName("BOX"));
    EXPECT_EQ(fromVector.nameById(1).value(), fromAdd.nameById(1).value());
    EXPECT_EQ(fromVector.nameById(7).value(), fromAdd.nameById(7).value());
    EXPECT_EQ(fromVector.size(), fromAdd.size());
}

TEST(NameTable, DuplicateNameFirstInsertWins) {
    // emplace() on unordered_map keeps the first mapping when the key exists.
    NameTable nt;
    nt.add(1, "SPHERE");
    nt.add(99, "SPHERE"); // same name, different id

    EXPECT_EQ(nt.idByName("SPHERE").value(), 1u);
    EXPECT_EQ(nt.size(), 2u); // both entries in the vector
}

TEST(NameTable, DuplicateIdFirstInsertWins) {
    // emplace() keeps the first index mapping for id → entry.
    NameTable nt;
    nt.add(5, "FIRST");
    nt.add(5, "SECOND"); // same id, different name

    EXPECT_EQ(std::string(nt.nameById(5).value()), "FIRST");
}

} // namespace
