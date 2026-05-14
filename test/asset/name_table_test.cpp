#include <gtest/gtest.h>
#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/name_table.hpp>

#include <string>
#include <vector>

namespace {

using namespace IRAsset;

TEST(NameTable, Utf8NamesRoundTrip) {
    // Name strings are UTF-8 (Rule #2). Non-ASCII bytes survive the round-trip.
    std::vector<NameTableEntry> entries = {
        {0, "caf\xc3\xa9"},         // "café" in UTF-8
        {1, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}, // "日本語" in UTF-8
        {2, "\xf0\x9f\xa6\x80"},    // "🦀" (crab emoji) in UTF-8
    };
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 3u);
    EXPECT_EQ(loaded.value_[0].name_, entries[0].name_);
    EXPECT_EQ(loaded.value_[1].name_, entries[1].name_);
    EXPECT_EQ(loaded.value_[2].name_, entries[2].name_);
}

TEST(NameTable, DuplicateNameFirstWins) {
    // add() uses emplace; duplicate name → first id wins in the name→id map.
    NameTable nt;
    nt.add(0, "SPHERE");
    nt.add(1, "SPHERE");
    EXPECT_EQ(nt.idByName("SPHERE").value(), 0u);
    EXPECT_EQ(nt.size(), 2u);
}

TEST(NameTable, DuplicateIdFirstWins) {
    // add() uses emplace; duplicate id → first name wins in the id→name map.
    NameTable nt;
    nt.add(5, "FIRST");
    nt.add(5, "SECOND");
    EXPECT_EQ(nt.nameById(5).value(), "FIRST");
    EXPECT_EQ(nt.size(), 2u);
}

TEST(NameTable, SpanConstructorMatchesSequentialAdd) {
    // Both construction paths produce identical lookup results.
    std::vector<NameTableEntry> entries = {
        {0, "SPHERE"},
        {1, "CUBE"},
        {7, "CYLINDER"},
    };
    NameTable from_span(entries);
    NameTable from_add;
    for (const auto &e : entries) {
        from_add.add(e.id_, e.name_);
    }
    for (const auto &e : entries) {
        EXPECT_EQ(from_span.idByName(e.name_), from_add.idByName(e.name_)) << e.name_;
        EXPECT_EQ(from_span.nameById(e.id_), from_add.nameById(e.id_)) << e.id_;
    }
    EXPECT_EQ(from_span.size(), from_add.size());
}

TEST(NameTable, EmptyNameStringRoundTrip) {
    // Empty name string is not forbidden; lock the round-trip and lookup behavior.
    std::vector<NameTableEntry> entries = {{42, ""}};
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 1u);
    EXPECT_EQ(loaded.value_[0].id_, 42u);
    EXPECT_EQ(loaded.value_[0].name_, "");
    NameTable nt(loaded.value_);
    EXPECT_EQ(nt.idByName("").value(), 42u);
    EXPECT_EQ(nt.nameById(42).value(), "");
}

TEST(NameTable, SingleEntryRoundTrip) {
    std::vector<NameTableEntry> entries = {{3, "TORUS"}};
    MemoryBinaryWriter w;
    ASSERT_TRUE(writeNameTable(w, entries).ok());
    MemoryBinaryReader r(w.buffer().data(), w.buffer().size());
    auto loaded = readNameTable(r);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.value_.size(), 1u);
    EXPECT_EQ(loaded.value_[0].id_, 3u);
    EXPECT_EQ(loaded.value_[0].name_, "TORUS");
}

} // namespace
