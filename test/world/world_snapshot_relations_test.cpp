#include <gtest/gtest.h>

#include <irreden/world/save_registry.hpp>
#include <irreden/world/save_trait.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/asset/name_table.hpp>
#include <irreden/ir_entity.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

// Persist P3 (#2214): the RELN relation chunk. One trivially-copyable
// stand-in component drives the round-trip; the plumbing under test is the
// CHILD_OF edge serializer, not the component bytes (P2 covers those).
namespace RelSnap {
struct C_RelTag {
    std::int32_t value_ = 0;
};
} // namespace RelSnap

IR_SAVE_OPT_IN(RelSnap::C_RelTag, 1)

namespace {

using IREntity::EntityId;
using RelSnap::C_RelTag;

class WorldSnapshotRelationsTest : public testing::Test {
  protected:
    IRWorld::SaveRegistry registry() {
        IRWorld::SaveRegistry reg;
        reg.registerComponent<C_RelTag>();
        return reg;
    }

    std::string tempPath(const char *name) const {
        return testing::TempDir() + "/ir_ws_reln_" + name + ".irws";
    }

    // A fresh gameplay entity carrying C_RelTag, masked id (ids can carry
    // high-bit flags; the snapshot works in masked-id space).
    EntityId makeEntity(std::int32_t value) {
        return m_em.createEntity(C_RelTag{value}) & IREntity::IR_ENTITY_ID_BITS;
    }

    // The CHILD_OF parent of @p child, or kNullEntity — the query the entity
    // CLAUDE.md prescribes (relation entities are index-space artifacts).
    EntityId parentOf(EntityId child) {
        return m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(child));
    }

    IREntity::EntityManager m_em; // ctor sets g_entityManager -> this
};

std::vector<std::uint8_t> readFileBytes(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

// One decoded RELN chunk: the name table plus the raw triples.
struct ParsedReln {
    std::vector<IRAsset::NameTableEntry> names_;
    std::vector<std::array<std::uint64_t, 3>> triples_; // {relationTypeId, child, parent}
};

// Decode the RELN chunk of an IRWS file. ASSERT_* inside a non-void helper
// isn't allowed, so failures surface as an empty optional the caller checks.
std::optional<ParsedReln> parseReln(const std::string &path) {
    IRAsset::FileBinaryReader fr(path);
    if (!fr.ok()) {
        return std::nullopt;
    }
    auto chunksRes =
        IRAsset::readChunks(fr, IRWorld::kWorldSnapshotMagic, IRWorld::kWorldSnapshotVersion);
    if (!chunksRes.ok()) {
        return std::nullopt;
    }
    const IRAsset::LoadedChunk *reln =
        IRAsset::findChunk(chunksRes.value_, IRAsset::makeTag("RELN"));
    if (reln == nullptr) {
        return std::nullopt;
    }
    IRAsset::MemoryBinaryReader r(reln->data_.data(), reln->data_.size(), "RELN");
    auto names = IRAsset::readNameTable(r);
    if (!names.ok()) {
        return std::nullopt;
    }
    auto count = r.readVarUInt();
    if (!count.ok()) {
        return std::nullopt;
    }
    ParsedReln out;
    out.names_ = std::move(names.value_);
    for (std::uint64_t i = 0; i < count.value_; ++i) {
        auto relId = r.readVarUInt();
        auto child = r.readVarUInt();
        auto parent = r.readVarUInt();
        if (!relId.ok() || !child.ok() || !parent.ok()) {
            return std::nullopt;
        }
        out.triples_.push_back({relId.value_, child.value_, parent.value_});
    }
    return out;
}

// Criterion: a three-level CHILD_OF chain round-trips with exact edge parity.
TEST_F(WorldSnapshotRelationsTest, ThreeLevelChildOfChainRoundTrips) {
    const EntityId grandparent = makeEntity(1);
    const EntityId parent = makeEntity(2);
    const EntityId child = makeEntity(3);
    IREntity::setParent(parent, grandparent);
    IREntity::setParent(child, parent);
    ASSERT_EQ(parentOf(parent), grandparent);
    ASSERT_EQ(parentOf(child), parent);
    ASSERT_EQ(parentOf(grandparent), IREntity::kNullEntity);

    IRWorld::SaveRegistry reg = registry();
    const std::string path = tempPath("chain");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    m_em.destroyAllEntities();
    ASSERT_EQ(m_em.getLiveEntityCount(), 0u);

    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 3u);
    EXPECT_EQ(result.relationsRestored_, 2u);
    EXPECT_EQ(result.relationsSkipped_, 0u);

    EXPECT_EQ(parentOf(parent), grandparent);
    EXPECT_EQ(parentOf(child), parent);
    EXPECT_EQ(parentOf(grandparent), IREntity::kNullEntity);
    EXPECT_EQ(m_em.getComponent<C_RelTag>(child).value_, 3);
}

// Criterion: a parent with several children round-trips every edge.
TEST_F(WorldSnapshotRelationsTest, MultiChildParentRoundTrips) {
    const EntityId parent = makeEntity(10);
    std::vector<EntityId> children;
    for (int i = 0; i < 5; ++i) {
        const EntityId c = makeEntity(100 + i);
        IREntity::setParent(c, parent);
        children.push_back(c);
    }
    for (EntityId c : children) {
        ASSERT_EQ(parentOf(c), parent);
    }

    IRWorld::SaveRegistry reg = registry();
    const std::string path = tempPath("multichild");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());
    m_em.destroyAllEntities();

    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.relationsRestored_, 5u);
    for (EntityId c : children) {
        EXPECT_EQ(parentOf(c), parent) << "child " << c << " lost its parent";
    }
}

// Criterion: only CHILD_OF triples are emitted (PARENT_TO/SIBLING_OF are
// named in the table for forward-compat but never produce a triple), and the
// triple count equals the edge count.
TEST_F(WorldSnapshotRelationsTest, RelnChunkContainsOnlyChildOfTriples) {
    const EntityId parent = makeEntity(1);
    const EntityId childA = makeEntity(2);
    const EntityId childB = makeEntity(3);
    IREntity::setParent(childA, parent);
    IREntity::setParent(childB, parent);

    IRWorld::SaveRegistry reg = registry();
    const std::string path = tempPath("childof_only");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    std::optional<ParsedReln> parsed = parseReln(path);
    ASSERT_TRUE(parsed.has_value());
    // Forward-compat name table: CHILD_OF, PARENT_TO, SIBLING_OF.
    EXPECT_EQ(parsed->names_.size(), 3u);
    // Exactly the two CHILD_OF edges; every triple is CHILD_OF.
    ASSERT_EQ(parsed->triples_.size(), 2u);
    for (const auto &triple : parsed->triples_) {
        EXPECT_EQ(triple[0], static_cast<std::uint64_t>(IREntity::CHILD_OF));
        EXPECT_EQ(triple[2], parent); // parent endpoint
    }
}

// Criterion: two consecutive saves of a world with relations are byte-identical
// (RELN triples are sorted by child id -> deterministic).
TEST_F(WorldSnapshotRelationsTest, DoubleSaveWithRelationsByteIdentical) {
    const EntityId root = makeEntity(0);
    for (int i = 0; i < 20; ++i) {
        const EntityId c = makeEntity(i);
        IREntity::setParent(c, root);
    }
    IRWorld::SaveRegistry reg = registry();
    const std::string pathA = tempPath("det_a");
    const std::string pathB = tempPath("det_b");
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathA).ok());
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathB).ok());
    EXPECT_EQ(readFileBytes(pathA), readFileBytes(pathB));
}

// Criterion: a triple whose relation name does not resolve to a current enum
// value is skipped with a diagnostic, leaving the world's other state intact.
TEST_F(WorldSnapshotRelationsTest, UnknownRelationNameSkipped) {
    const EntityId parent = makeEntity(1);
    const EntityId child = makeEntity(2);
    IREntity::setParent(child, parent);

    IRWorld::SaveRegistry reg = registry();
    const std::string path = tempPath("unknown_name");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    // Rename the on-disk "CHILD_OF" entry to an equal-length name this build
    // does not know. The triple still points at that entry's id, so on load
    // the name -> current-enum lookup misses and the edge is skipped. Equal
    // length keeps every chunk offset/length in the header table valid.
    std::vector<std::uint8_t> bytes = readFileBytes(path);
    std::string blob(bytes.begin(), bytes.end());
    const std::size_t pos = blob.find("CHILD_OF");
    ASSERT_NE(pos, std::string::npos);
    ASSERT_EQ(blob.find("CHILD_OF", pos + 1), std::string::npos) << "expected one CHILD_OF";
    blob.replace(pos, 8, "OWNSABCD"); // 8-byte unknown name, same length
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    }

    m_em.destroyAllEntities();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 2u); // entities restore normally
    EXPECT_EQ(result.relationsRestored_, 0u);
    EXPECT_EQ(result.relationsSkipped_, 1u); // the renamed edge is dropped
    EXPECT_EQ(parentOf(child), IREntity::kNullEntity);
}

// Criterion (regression): a RELN chunk whose triple count claims more triples
// than the body actually holds — a truncated / half-written /
// hand-edited chunk — must abort the load with ZERO relation edges applied, not
// replay the valid leading triples and *then* fail. The pre-fix loop parsed and
// setParent'd in the same pass, so it committed every valid edge before hitting
// the truncation, leaving the live world partially mutated while loadWorld
// reported failure (Rule #5 violation). The staged decode-then-apply pass fixes
// it: all triples decode in a mutation-free pass first, so the phantom read
// fails before any setParent runs.
TEST_F(WorldSnapshotRelationsTest, TruncatedTripleCountAppliesZeroRelations) {
    const EntityId parent = makeEntity(1);
    std::vector<EntityId> children;
    for (int i = 0; i < 3; ++i) {
        const EntityId c = makeEntity(10 + i);
        IREntity::setParent(c, parent);
        children.push_back(c);
    }

    IRWorld::SaveRegistry reg = registry();
    const std::string path = tempPath("truncated_count");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    // Locate the tripleCount varuint inside the RELN chunk body (it sits right
    // after the name table), then over-state it so the loader tries to read
    // more triples than the body holds. Keeping it a single-byte varuint leaves
    // the body length — and thus every chunk-table offset/size in the header —
    // unchanged, so the corruption surfaces only mid-decode, when the reader
    // runs off the chunk body parsing the phantom triple.
    IRAsset::FileBinaryReader fr(path);
    ASSERT_TRUE(fr.ok());
    auto chunksRes =
        IRAsset::readChunks(fr, IRWorld::kWorldSnapshotMagic, IRWorld::kWorldSnapshotVersion);
    ASSERT_TRUE(chunksRes.ok());
    const IRAsset::LoadedChunk *reln =
        IRAsset::findChunk(chunksRes.value_, IRAsset::makeTag("RELN"));
    ASSERT_NE(reln, nullptr);

    IRAsset::MemoryBinaryReader r(reln->data_.data(), reln->data_.size(), "RELN");
    auto names = IRAsset::readNameTable(r);
    ASSERT_TRUE(names.ok());
    const std::uint64_t countOffsetInBody = r.tell();
    auto count = r.readVarUInt();
    ASSERT_TRUE(count.ok());
    ASSERT_EQ(count.value_, 3u); // three CHILD_OF edges -> single-byte varuint 0x03

    // Find the chunk body verbatim in the file, then patch the count byte.
    std::vector<std::uint8_t> fileBytes = readFileBytes(path);
    const auto bodyIt =
        std::search(fileBytes.begin(), fileBytes.end(), reln->data_.begin(), reln->data_.end());
    ASSERT_NE(bodyIt, fileBytes.end());
    const std::size_t absCountOffset =
        static_cast<std::size_t>(std::distance(fileBytes.begin(), bodyIt)) + countOffsetInBody;
    ASSERT_EQ(fileBytes[absCountOffset], static_cast<std::uint8_t>(count.value_));
    fileBytes[absCountOffset] = 0x7F; // claim 127 triples; only 3 are present
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(
            reinterpret_cast<const char *>(fileBytes.data()),
            static_cast<std::streamsize>(fileBytes.size())
        );
    }

    m_em.destroyAllEntities();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    // The phantom triple read runs off the chunk body -> the load fails...
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.relationsRestored_, 0u); // Rule #5: no partial counts on failure
    // ...and the real proof of "zero relations applied" is the live world: not
    // one child kept a parent, even though three valid CHILD_OF triples preceded
    // the truncation (the pre-fix loop would have setParent'd all three first).
    for (const EntityId c : children) {
        EXPECT_EQ(parentOf(c), IREntity::kNullEntity)
            << "child " << c << " was partially re-parented before the decode failure";
    }
}

// A relation-free world still writes a well-formed (empty) RELN chunk and
// restores with zero relations.
TEST_F(WorldSnapshotRelationsTest, NoRelationsRoundTrips) {
    for (int i = 0; i < 8; ++i) {
        makeEntity(i);
    }
    IRWorld::SaveRegistry reg = registry();
    const std::string path = tempPath("norelations");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    std::optional<ParsedReln> parsed = parseReln(path);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->names_.size(), 3u); // forward-compat table always present
    EXPECT_TRUE(parsed->triples_.empty());

    m_em.destroyAllEntities();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.relationsRestored_, 0u);
    EXPECT_EQ(result.relationsSkipped_, 0u);
}

} // namespace
