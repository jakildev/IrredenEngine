#include <gtest/gtest.h>

#include <irreden/world/save_migration.hpp>
#include <irreden/world/save_registry.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_trait.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/ir_entity.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Persist P5 (#2216, epic #667): the `(ComponentId, oldVersion) -> reader`
// component-migration registry. The `ARCH`/`SNGL` chunks stamp each column
// with the `kSaveVersion` it was written at (persist P2's migration seam);
// this suite proves the load-time dispatch on that stamp — current fast path,
// a registered v1->v2 migrator, and the two hard-failure diagnostics
// (`VersionTooNew`, `MigratorMissing`) — over hand-built `IRWS` files, since
// `saveWorld` can only ever emit the *current* version.
//
// A dedicated test-local component (`C_Mig`) carries the worked v1->v2
// migration rather than mutating a real gameplay component with a
// permanently-dead field — the committed fallback in the plan's acceptance
// note, exercised through the identical generic loader path.
namespace MigTest {
// Current build: C_Mig is at v2 = { kept_, added_ }, evolved from the retired
// v1 = { kept_ } by appending a defaulted `added_`.
struct C_Mig {
    std::int32_t kept_ = 0;
    std::int32_t added_ = 0;
};
// At v2 but declares NO migrator: a retired-version column of it must
// hard-fail `MigratorMissing`, never silently read old bytes at the current
// layout. Trivially copyable -> the default raw-image SaveSerialize.
struct C_NoMigrator {
    std::int32_t value_ = 0;
};
// Never registered in the load registry -> its column skips by byte length.
struct C_Unknown {
    std::int32_t junk_ = 0;
};
} // namespace MigTest

IR_SAVE_OPT_IN(MigTest::C_Mig, 2)
IR_SAVE_OPT_IN(MigTest::C_NoMigrator, 2)
IR_SAVE_OPT_IN(MigTest::C_Unknown, 1)

namespace IRWorld {
// Current (v2) serializer: kept_ then added_.
template <> struct SaveSerialize<MigTest::C_Mig> {
    static void write(IRAsset::BinaryWriter &w, const MigTest::C_Mig &value) {
        w.writeI32(value.kept_);
        w.writeI32(value.added_);
    }
    static IRAsset::Result<MigTest::C_Mig> read(IRAsset::BinaryReader &r) {
        IRAsset::Result<std::int32_t> kept = r.readI32();
        if (!kept.ok()) {
            return IRAsset::Result<MigTest::C_Mig>::error(
                kept.status_.code_,
                kept.status_.message_
            );
        }
        IRAsset::Result<std::int32_t> added = r.readI32();
        if (!added.ok()) {
            return IRAsset::Result<MigTest::C_Mig>::error(
                added.status_.code_,
                added.status_.message_
            );
        }
        return IRAsset::Result<MigTest::C_Mig>::success(MigTest::C_Mig{kept.value_, added.value_});
    }
};
// The retired v1 reader: reads only kept_ off the v1 layout, defaults added_.
// Direct per-version reader (Rule #3) — yields a current-build C_Mig.
template <> struct SaveMigration<MigTest::C_Mig> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<MigTest::C_Mig>>> migrators() {
        return {
            {1u, [](IRAsset::BinaryReader &r) -> IRAsset::Result<MigTest::C_Mig> {
                 IRAsset::Result<std::int32_t> kept = r.readI32();
                 if (!kept.ok()) {
                     return IRAsset::Result<MigTest::C_Mig>::error(
                         kept.status_.code_,
                         kept.status_.message_
                     );
                 }
                 return IRAsset::Result<MigTest::C_Mig>::success(
                     MigTest::C_Mig{kept.value_, 0 /* defaulted */}
                 );
             }},
        };
    }
};
} // namespace IRWorld

namespace {

using namespace MigTest;
using IREntity::EntityId;

// One serialized column of an `IRWS` file: the disk save-name, the on-disk
// version stamp, and every row's bytes already laid out in entity order.
struct BuiltColumn {
    std::string saveName_;
    std::uint32_t version_ = 1;
    std::vector<std::uint8_t> bytes_;
};

class MigrationTest : public testing::Test {
  protected:
    std::string tempPath(const char *name) const {
        return testing::TempDir() + "/ir_mig_" + name + ".irws";
    }

    // Emit a minimal single-archetype `IRWS` file matching world_snapshot.cpp's
    // chunk layout (CMPN + ARCH + META; SNGL/RELN absent are a clean no-op).
    // `cmpnNames` is the local-index key space; `columns` are one-per
    // `archLocalIndices`, in the same order.
    void writeIrws(
        const std::string &path,
        const std::vector<std::string> &cmpnNames,
        const std::vector<std::uint32_t> &archLocalIndices,
        const std::vector<EntityId> &entityIds,
        const std::vector<BuiltColumn> &columns,
        EntityId watermark
    ) const {
        IRAsset::MemoryBinaryWriter cmpn;
        cmpn.writeVarUInt(cmpnNames.size());
        for (const std::string &name : cmpnNames) {
            cmpn.writeString(name);
        }

        IRAsset::MemoryBinaryWriter arch;
        arch.writeVarUInt(1); // one archetype group
        arch.writeVarUInt(archLocalIndices.size());
        for (const std::uint32_t li : archLocalIndices) {
            arch.writeVarUInt(li);
        }
        arch.writeVarUInt(entityIds.size());
        for (const EntityId id : entityIds) {
            arch.writeVarUInt(id);
        }
        for (const BuiltColumn &col : columns) {
            arch.writeU32(col.version_);
            arch.writeVarUInt(col.bytes_.size());
            arch.writeBytes(col.bytes_.data(), col.bytes_.size());
        }

        IRAsset::MemoryBinaryWriter meta;
        meta.writeVarUInt(watermark);
        meta.writeVarUInt(entityIds.size());

        std::vector<IRAsset::ChunkPayload> chunks;
        chunks.push_back({IRAsset::makeTag("CMPN"), cmpn.takeBuffer()});
        chunks.push_back({IRAsset::makeTag("ARCH"), arch.takeBuffer()});
        chunks.push_back({IRAsset::makeTag("META"), meta.takeBuffer()});

        IRAsset::FileBinaryWriter w(path);
        ASSERT_TRUE(w.ok());
        ASSERT_TRUE(
            IRAsset::writeChunked(
                w,
                IRWorld::kWorldSnapshotMagic,
                IRWorld::kWorldSnapshotVersion,
                chunks
            )
                .ok()
        );
    }

    // Restore ids well above IR_RESERVED_ENTITIES (0xFF) + the handful of
    // backing entities registration mints, so they are guaranteed free.
    IREntity::EntityManager m_em; // ctor sets g_entityManager -> this
};

// Acceptance #1: worked v1->v2 migration — the retired reader preserves the v1
// field and defaults the field appended in v2, end-to-end through the loader.
TEST_F(MigrationTest, WorkedV1ToV2Migration) {
    const std::vector<EntityId> ids{1000, 1001, 1002};
    const std::vector<std::int32_t> kept{11, 22, 33};
    IRAsset::MemoryBinaryWriter col;
    for (const std::int32_t k : kept) {
        col.writeI32(k); // v1 layout: kept_ only
    }
    const std::string path = tempPath("v1_to_v2");
    ASSERT_NO_FATAL_FAILURE(writeIrws(
        path,
        {IRWorld::saveName<C_Mig>()},
        {0},
        ids,
        {BuiltColumn{IRWorld::saveName<C_Mig>(), 1u, col.takeBuffer()}},
        2000
    ));

    IRWorld::SaveRegistry reg;
    reg.registerComponent<C_Mig>();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 3u);

    for (std::size_t i = 0; i < ids.size(); ++i) {
        ASSERT_TRUE(m_em.entityExists(ids[i])) << "missing id " << ids[i];
        const C_Mig &m = m_em.getComponent<C_Mig>(ids[i]);
        EXPECT_EQ(m.kept_, kept[i]); // v1 field preserved exactly
        EXPECT_EQ(m.added_, 0);      // v2's appended field defaulted
    }
}

// Acceptance #5: a current-version column dispatches straight to the current
// reader (never the migrator) — the fast path regresses nothing. `added_`
// comes back as the real saved value, proving the migrator did NOT run.
TEST_F(MigrationTest, CurrentVersionFastPath) {
    const std::vector<EntityId> ids{1000, 1001};
    const std::vector<std::pair<std::int32_t, std::int32_t>> vals{{5, 6}, {7, 8}};
    IRAsset::MemoryBinaryWriter col;
    for (const auto &v : vals) {
        col.writeI32(v.first);  // v2 layout: kept_
        col.writeI32(v.second); //            added_
    }
    const std::string path = tempPath("current");
    ASSERT_NO_FATAL_FAILURE(writeIrws(
        path,
        {IRWorld::saveName<C_Mig>()},
        {0},
        ids,
        {BuiltColumn{IRWorld::saveName<C_Mig>(), 2u, col.takeBuffer()}},
        2000
    ));

    IRWorld::SaveRegistry reg;
    reg.registerComponent<C_Mig>();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 2u);

    for (std::size_t i = 0; i < ids.size(); ++i) {
        const C_Mig &m = m_em.getComponent<C_Mig>(ids[i]);
        EXPECT_EQ(m.kept_, vals[i].first);
        EXPECT_EQ(m.added_, vals[i].second); // real saved value, not defaulted
    }
}

// Acceptance #2: a column newer than this build fails with VersionTooNew,
// naming the component, and leaves the world untouched.
TEST_F(MigrationTest, NewerThanBuildRejectedVersionTooNew) {
    const std::vector<EntityId> ids{1000};
    IRAsset::MemoryBinaryWriter col;
    col.writeI32(1); // bytes never read — the version check fires first
    col.writeI32(2);
    const std::string path = tempPath("too_new");
    ASSERT_NO_FATAL_FAILURE(writeIrws(
        path,
        {IRWorld::saveName<C_Mig>()},
        {0},
        ids,
        {BuiltColumn{IRWorld::saveName<C_Mig>(), 3u, col.takeBuffer()}}, // v3 > build v2
        2000
    ));

    IRWorld::SaveRegistry reg;
    reg.registerComponent<C_Mig>();
    const EntityId before = m_em.getLiveEntityCount();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status_.code_, IRAsset::BinaryIOError::VersionTooNew);
    EXPECT_NE(result.status_.message_.find("C_Mig"), std::string::npos);
    EXPECT_EQ(result.entitiesRestored_, 0u);
    EXPECT_EQ(m_em.getLiveEntityCount(), before); // zero world mutation
}

// Acceptance #3: a known component at an older version with no registered
// migrator is the one non-recoverable case — MigratorMissing, world untouched
// (never a silent read at the current layout).
TEST_F(MigrationTest, MissingMigratorHardFails) {
    const std::vector<EntityId> ids{1000, 1001};
    IRAsset::MemoryBinaryWriter col;
    col.writeI32(7); // a "v1" row for a component whose build is at v2
    col.writeI32(8); // with no v1 migrator declared
    const std::string path = tempPath("no_migrator");
    ASSERT_NO_FATAL_FAILURE(writeIrws(
        path,
        {IRWorld::saveName<C_NoMigrator>()},
        {0},
        ids,
        {BuiltColumn{IRWorld::saveName<C_NoMigrator>(), 1u, col.takeBuffer()}},
        2000
    ));

    IRWorld::SaveRegistry reg;
    reg.registerComponent<C_NoMigrator>();
    const EntityId before = m_em.getLiveEntityCount();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status_.code_, IRAsset::BinaryIOError::MigratorMissing);
    EXPECT_NE(result.status_.message_.find("C_NoMigrator"), std::string::npos);
    EXPECT_EQ(result.entitiesRestored_, 0u);      // out-component never materialized
    EXPECT_EQ(m_em.getLiveEntityCount(), before); // zero world mutation
}

// Acceptance #4 (compose): a migrated column and an unresolvable column in the
// same archetype — the unknown one skips by byte length while the migrated one
// restores. Proves migration and the forward-compat skip coexist per entity.
TEST_F(MigrationTest, MigrationComposesWithUnknownSkip) {
    const std::vector<EntityId> ids{1000, 1001};
    IRAsset::MemoryBinaryWriter migCol;
    migCol.writeI32(41); // C_Mig v1 rows (kept_ only)
    migCol.writeI32(42);
    IRAsset::MemoryBinaryWriter unkCol;
    unkCol.writeI32(999); // C_Unknown rows — never resolved, skipped by length
    unkCol.writeI32(999);

    const std::string path = tempPath("compose_skip");
    // CMPN order fixes local indices: C_Mig=0, C_Unknown=1.
    ASSERT_NO_FATAL_FAILURE(writeIrws(
        path,
        {IRWorld::saveName<C_Mig>(), IRWorld::saveName<C_Unknown>()},
        {0, 1},
        ids,
        {BuiltColumn{IRWorld::saveName<C_Mig>(), 1u, migCol.takeBuffer()},
         BuiltColumn{IRWorld::saveName<C_Unknown>(), 1u, unkCol.takeBuffer()}},
        2000
    ));

    IRWorld::SaveRegistry reg;
    reg.registerComponent<C_Mig>(); // C_Unknown deliberately NOT registered
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 2u);
    EXPECT_GT(result.columnsSkipped_, 0u); // the unknown column was skipped

    for (std::size_t i = 0; i < ids.size(); ++i) {
        ASSERT_TRUE(m_em.entityExists(ids[i])) << "missing id " << ids[i];
        const C_Mig &m = m_em.getComponent<C_Mig>(ids[i]);
        EXPECT_EQ(m.kept_, 41 + static_cast<std::int32_t>(i)); // migrated
        EXPECT_EQ(m.added_, 0);
    }
}

} // namespace
