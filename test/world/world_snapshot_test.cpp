#include <gtest/gtest.h>

#include <irreden/world/save_registry.hpp>
#include <irreden/world/save_trait.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/ir_entity.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Stand-in "components" for the snapshot round-trip. All trivially
// copyable, so the default SaveSerialize<C> (raw byte image) drives the
// round-trip — this task ships the *mechanism*; real engine components get
// their own SaveSerialize specializations downstream. C_WsTransient is the
// opted-out projection-merge witness. Defined at namespace scope so the
// IR_SAVE_* macros (which specialize IRWorld::SaveTrait) can name them.
namespace WsSnap {
struct C_WsPos {
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t z_ = 0;
};
struct C_WsVel {
    float vx_ = 0.0f;
    float vy_ = 0.0f;
};
struct C_WsTag {
    std::int32_t marker_ = 0;
};
struct C_WsTransient {
    std::int32_t scratch_ = 0;
};
struct C_WsSingletonState {
    std::int32_t counter_ = 0;
    float weight_ = 0.0f;
};
// A component with a *fallible* serializer: `read` rejects a wrong sentinel,
// so a length-valid-but-content-corrupt column fails to *decode* — unlike a
// trivially-copyable raw image, which accepts any byte pattern. This is what
// lets CorruptColumnAbortsWithZeroMutation exercise a decode failure the
// loader's phase-2 gate must catch before it mutates the live world.
struct C_WsChecked {
    std::int32_t payload_ = 0;
};
} // namespace WsSnap

IR_SAVE_OPT_IN(WsSnap::C_WsPos, 1)
IR_SAVE_OPT_IN(WsSnap::C_WsVel, 1)
IR_SAVE_OPT_IN(WsSnap::C_WsTag, 1)
IR_SAVE_OPT_OUT(WsSnap::C_WsTransient)
IR_SAVE_OPT_IN(WsSnap::C_WsSingletonState, 2)
IR_SAVE_OPT_IN(WsSnap::C_WsChecked, 1)

namespace IRWorld {
// Explicit specialization overriding the trivially-copyable raw-image default
// with a sentinel-guarded encoding whose `read` can fail on bad content.
template <> struct SaveSerialize<WsSnap::C_WsChecked> {
    // A distinctive little-endian pattern (EF BE AD DE) the corruption test
    // can locate and flip in the on-disk bytes without colliding with the
    // small varuint headers / low component values elsewhere in the file.
    static constexpr std::uint32_t kSentinel = 0xDEADBEEFu;

    static void write(IRAsset::BinaryWriter &w, const WsSnap::C_WsChecked &value) {
        w.writeU32(kSentinel);
        w.writeU32(static_cast<std::uint32_t>(value.payload_));
    }

    static IRAsset::Result<WsSnap::C_WsChecked> read(IRAsset::BinaryReader &r) {
        IRAsset::Result<std::uint32_t> tag = r.readU32();
        if (!tag.ok()) {
            return IRAsset::Result<WsSnap::C_WsChecked>::error(
                tag.status_.code_,
                tag.status_.message_
            );
        }
        if (tag.value_ != kSentinel) {
            return IRAsset::Result<WsSnap::C_WsChecked>::error(
                IRAsset::BinaryIOError::UnknownTag,
                "C_WsChecked: bad sentinel"
            );
        }
        IRAsset::Result<std::uint32_t> payload = r.readU32();
        if (!payload.ok()) {
            return IRAsset::Result<WsSnap::C_WsChecked>::error(
                payload.status_.code_,
                payload.status_.message_
            );
        }
        return IRAsset::Result<WsSnap::C_WsChecked>::success(
            WsSnap::C_WsChecked{static_cast<std::int32_t>(payload.value_)}
        );
    }
};
} // namespace IRWorld

namespace {

using namespace WsSnap;
using IREntity::EntityId;

// A record of one created entity so the round-trip can assert exact
// restoration (id, values, resolved archetype membership).
struct Expected {
    EntityId id_;
    C_WsPos pos_;
    bool hasVel_ = false;
    C_WsVel vel_{};
    bool hasTag_ = false;
    C_WsTag tag_{};
};

class WorldSnapshotTest : public testing::Test {
  protected:
    WorldSnapshotTest() = default;

    // Full registry: every opted-in test component. registerComponent is a
    // no-op for the opted-out C_WsTransient.
    IRWorld::SaveRegistry fullRegistry() {
        IRWorld::SaveRegistry reg;
        reg.registerComponent<C_WsPos>();
        reg.registerComponent<C_WsVel>();
        reg.registerComponent<C_WsTag>();
        reg.registerComponent<C_WsTransient>(); // no-op (opted out)
        reg.registerComponent<C_WsSingletonState>();
        return reg;
    }

    std::string tempPath(const char *name) const {
        return testing::TempDir() + "/ir_ws_" + name + ".irws";
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

// Criterion 1: >=100 entities, >=3 saved archetypes (incl. a projection
// merge of two live archetypes that differ only by an opted-out
// component), exact id + value + membership round-trip.
TEST_F(WorldSnapshotTest, RoundTripExactIdsValuesMembership) {
    std::vector<Expected> expected;

    // Archetype A: {C_WsPos} — 40 entities.
    for (int i = 0; i < 40; ++i) {
        C_WsPos pos{i, i * 2, i * 3};
        EntityId id = m_em.createEntity(pos);
        expected.push_back({id, pos});
    }
    // Archetype B: {C_WsPos, C_WsVel} — 30 entities.
    for (int i = 0; i < 30; ++i) {
        C_WsPos pos{100 + i, i, -i};
        C_WsVel vel{static_cast<float>(i) * 0.5f, static_cast<float>(-i)};
        EntityId id = m_em.createEntity(pos, vel);
        expected.push_back({id, pos, true, vel});
    }
    // Archetype C: {C_WsPos, C_WsTag} — 20 entities.
    for (int i = 0; i < 20; ++i) {
        C_WsPos pos{200 + i, 0, i};
        C_WsTag tag{1000 + i};
        EntityId id = m_em.createEntity(pos, tag);
        Expected e{id, pos};
        e.hasTag_ = true;
        e.tag_ = tag;
        expected.push_back(e);
    }
    // Archetype D: {C_WsPos, C_WsTransient} — 15 entities. C_WsTransient is
    // opted out, so D projects onto {C_WsPos} == A: proves projection-merge.
    for (int i = 0; i < 15; ++i) {
        C_WsPos pos{300 + i, i, i};
        C_WsTransient transient{9999};
        EntityId id = m_em.createEntity(pos, transient);
        expected.push_back({id, pos});
    }
    ASSERT_EQ(expected.size(), 105u);

    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string path = tempPath("roundtrip");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    m_em.destroyAllEntities();
    ASSERT_EQ(m_em.getLiveEntityCount(), 0u);

    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 105u);

    for (const Expected &e : expected) {
        ASSERT_TRUE(m_em.entityExists(e.id_)) << "missing id " << e.id_;
        const C_WsPos &pos = m_em.getComponent<C_WsPos>(e.id_);
        EXPECT_EQ(pos.x_, e.pos_.x_);
        EXPECT_EQ(pos.y_, e.pos_.y_);
        EXPECT_EQ(pos.z_, e.pos_.z_);

        auto vel = m_em.getComponentOptional<C_WsVel>(e.id_);
        EXPECT_EQ(vel.has_value(), e.hasVel_);
        if (e.hasVel_) {
            EXPECT_FLOAT_EQ((*vel)->vx_, e.vel_.vx_);
            EXPECT_FLOAT_EQ((*vel)->vy_, e.vel_.vy_);
        }
        auto tag = m_em.getComponentOptional<C_WsTag>(e.id_);
        EXPECT_EQ(tag.has_value(), e.hasTag_);
        if (e.hasTag_) {
            EXPECT_EQ((*tag)->marker_, e.tag_.marker_);
        }
        // Opted-out component never round-trips.
        EXPECT_FALSE(m_em.getComponentOptional<C_WsTransient>(e.id_).has_value());
    }
}

// Criterion 4: a createEntity after load lands above the saved watermark
// (no collision with any restored id).
TEST_F(WorldSnapshotTest, PostLoadAllocationClearsWatermark) {
    EntityId maxId = 0;
    for (int i = 0; i < 10; ++i) {
        const EntityId id = m_em.createEntity(C_WsPos{i, i, i}) & IREntity::IR_ENTITY_ID_BITS;
        if (id > maxId) {
            maxId = id;
        }
    }
    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string path = tempPath("watermark");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());
    m_em.destroyAllEntities();
    ASSERT_TRUE(IRWorld::loadWorld(reg, path).ok());

    EntityId fresh = m_em.createEntity(C_WsPos{0, 0, 0}) & IREntity::IR_ENTITY_ID_BITS;
    EXPECT_GT(fresh, maxId);
    EXPECT_FALSE(m_em.entityExists(0)); // sanity: null id never live
}

// Criterion 2: singleton value round-trips by value; alias map records the
// saved-id -> live-id mapping.
TEST_F(WorldSnapshotTest, SingletonRoundTripByValue) {
    EntityId savedSingletonId = m_em.getOrCreateSingleton<C_WsSingletonState>();
    m_em.getComponent<C_WsSingletonState>(savedSingletonId) = C_WsSingletonState{42, 3.5f};
    // Also a couple of gameplay entities so the SNGL path coexists with ARCH.
    m_em.createEntity(C_WsPos{1, 2, 3});

    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string path = tempPath("singleton");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    m_em.destroyAllEntities(); // clears the singleton cache

    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.singletonsRestored_, 1u);

    const C_WsSingletonState &restored = IREntity::singleton<C_WsSingletonState>();
    EXPECT_EQ(restored.counter_, 42);
    EXPECT_FLOAT_EQ(restored.weight_, 3.5f);

    // Alias map: saved id -> the (freshly lazy-created) live singleton id.
    ASSERT_EQ(result.singletonAliases_.size(), 1u);
    auto it = result.singletonAliases_.find(savedSingletonId & IREntity::IR_ENTITY_ID_BITS);
    ASSERT_NE(it, result.singletonAliases_.end());
    EXPECT_EQ(it->second, IREntity::singletonEntityOrNull<C_WsSingletonState>());
}

// Criterion 3: an opted-out component's save-name never appears in the file
// (CMPN table), while an opted-in one does.
TEST_F(WorldSnapshotTest, OptOutComponentAbsentFromFile) {
    m_em.createEntity(C_WsPos{1, 2, 3}, C_WsTransient{7});

    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string path = tempPath("optout");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    std::vector<std::uint8_t> bytes = readFileBytes(path);
    std::string blob(bytes.begin(), bytes.end());
    EXPECT_NE(blob.find("WsSnap::C_WsPos"), std::string::npos);
    EXPECT_EQ(blob.find("WsSnap::C_WsTransient"), std::string::npos);
}

// Criterion 6: two consecutive saves of the same world are byte-identical.
TEST_F(WorldSnapshotTest, DoubleSaveIsByteIdentical) {
    for (int i = 0; i < 25; ++i) {
        m_em.createEntity(C_WsPos{i, i, i}, C_WsVel{static_cast<float>(i), 0.0f});
    }
    for (int i = 0; i < 25; ++i) {
        m_em.createEntity(C_WsPos{i, 0, 0}, C_WsTag{i});
    }
    m_em.getComponent<C_WsSingletonState>(m_em.getOrCreateSingleton<C_WsSingletonState>()) =
        C_WsSingletonState{7, 1.0f};

    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string pathA = tempPath("det_a");
    const std::string pathB = tempPath("det_b");
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathA).ok());
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathB).ok());

    EXPECT_EQ(readFileBytes(pathA), readFileBytes(pathB));
}

// Criterion 5a: bad magic is a recoverable error, world untouched.
TEST_F(WorldSnapshotTest, BadMagicRecoverable) {
    const std::string path = tempPath("badmagic");
    {
        IRAsset::FileBinaryWriter w(path);
        ASSERT_TRUE(w.ok());
        std::vector<IRAsset::ChunkPayload> none;
        ASSERT_TRUE(IRAsset::writeChunked(w, IRAsset::makeTag("XXXX"), 1, none).ok());
    }
    // Build the registry (which registers components -> backing entities)
    // BEFORE the baseline, so the count reflects only the loader's effect.
    IRWorld::SaveRegistry reg = fullRegistry();
    m_em.createEntity(C_WsPos{1, 1, 1});
    const EntityId before = m_em.getLiveEntityCount();

    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status_.code_, IRAsset::BinaryIOError::BadMagic);
    EXPECT_EQ(m_em.getLiveEntityCount(), before); // zero mutation
}

// Criterion 5b: version-too-new is a recoverable error.
TEST_F(WorldSnapshotTest, VersionTooNewRecoverable) {
    const std::string path = tempPath("vtoonew");
    {
        IRAsset::FileBinaryWriter w(path);
        ASSERT_TRUE(w.ok());
        std::vector<IRAsset::ChunkPayload> none;
        ASSERT_TRUE(
            IRAsset::writeChunked(
                w,
                IRWorld::kWorldSnapshotMagic,
                IRWorld::kWorldSnapshotVersion + 99,
                none
            )
                .ok()
        );
    }
    IRWorld::SaveRegistry reg = fullRegistry();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status_.code_, IRAsset::BinaryIOError::VersionTooNew);
}

// Criterion 5c: truncation is a recoverable error.
TEST_F(WorldSnapshotTest, TruncatedRecoverable) {
    for (int i = 0; i < 20; ++i) {
        m_em.createEntity(C_WsPos{i, i, i}, C_WsVel{1.0f, 2.0f});
    }
    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string path = tempPath("trunc");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    std::vector<std::uint8_t> bytes = readFileBytes(path);
    ASSERT_GT(bytes.size(), 16u);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), 16); // header only
    }
    m_em.destroyAllEntities();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(m_em.getLiveEntityCount(), 0u); // zero mutation
}

// Criterion 5d: a restored id colliding with a live entity aborts with zero
// mutation (loading without a reset first).
TEST_F(WorldSnapshotTest, LiveIdCollisionRecoverable) {
    for (int i = 0; i < 10; ++i) {
        m_em.createEntity(C_WsPos{i, i, i});
    }
    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string path = tempPath("collision");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    const EntityId before = m_em.getLiveEntityCount();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path); // no reset -> collides
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(m_em.getLiveEntityCount(), before); // zero mutation
}

// Criterion 5e: a component whose save-name does not resolve in the load
// registry is skipped (by byte length), not fatal — its entities restore
// with only the resolvable columns.
TEST_F(WorldSnapshotTest, UnknownComponentNameSkipped) {
    std::vector<EntityId> ids;
    for (int i = 0; i < 12; ++i) {
        ids.push_back(m_em.createEntity(C_WsPos{i, i, i}, C_WsVel{static_cast<float>(i), 0.0f}));
    }
    IRWorld::SaveRegistry saveReg = fullRegistry(); // has C_WsVel
    const std::string path = tempPath("skip");
    ASSERT_TRUE(IRWorld::saveWorld(saveReg, path).ok());
    m_em.destroyAllEntities();

    IRWorld::SaveRegistry loadReg; // deliberately missing C_WsVel
    loadReg.registerComponent<C_WsPos>();
    IRWorld::LoadResult result = IRWorld::loadWorld(loadReg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 12u);
    EXPECT_GT(result.columnsSkipped_, 0u);

    for (EntityId id : ids) {
        ASSERT_TRUE(m_em.entityExists(id));
        EXPECT_TRUE(m_em.getComponentOptional<C_WsPos>(id).has_value());
        EXPECT_FALSE(m_em.getComponentOptional<C_WsVel>(id).has_value()); // skipped
    }
}

// An empty world round-trips cleanly (no archetypes, no singletons).
TEST_F(WorldSnapshotTest, EmptyWorldRoundTrips) {
    IRWorld::SaveRegistry reg = fullRegistry();
    const std::string path = tempPath("empty");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.entitiesRestored_, 0u);
    EXPECT_EQ(result.singletonsRestored_, 0u);
}

// A length-valid-but-content-corrupt column must be caught by the load's
// phase-2 decode gate and abort with ZERO world mutation (Rule #5) — not
// surface mid-apply, after restoreEntitiesBatch has already spliced entities
// into the live graph. Uses the fallible C_WsChecked serializer so a corrupt
// row genuinely fails to decode (a trivially-copyable raw image accepts any
// bytes and can't reproduce this). See #2213.
TEST_F(WorldSnapshotTest, CorruptColumnAbortsWithZeroMutation) {
    IRWorld::SaveRegistry reg;
    reg.registerComponent<C_WsPos>();
    reg.registerComponent<C_WsChecked>();
    // A pure-{C_WsPos} archetype plus a {C_WsPos, C_WsChecked} one, so a
    // decode failure in the checked column would (pre-fix) land after at least
    // one archetype is already spliced — the partial mutation under test.
    for (int i = 0; i < 5; ++i) {
        m_em.createEntity(C_WsPos{i, i, i});
    }
    for (int i = 0; i < 5; ++i) {
        m_em.createEntity(C_WsPos{i, i, i}, C_WsChecked{100 + i});
    }
    const std::string path = tempPath("corruptcol");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    // Corrupt one C_WsChecked sentinel in place: same file length, valid
    // per-column byteLength headers, but the row no longer decodes.
    std::vector<std::uint8_t> bytes = readFileBytes(path);
    const std::array<std::uint8_t, 4> needle{0xEF, 0xBE, 0xAD, 0xDE}; // 0xDEADBEEF LE
    auto at = std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
    ASSERT_NE(at, bytes.end()) << "C_WsChecked sentinel not found in file";
    *at ^= 0xFFu; // flip the low byte -> sentinel mismatch on decode
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(
            reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }

    m_em.destroyAllEntities();
    const EntityId before = m_em.getLiveEntityCount();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    EXPECT_FALSE(result.ok());                    // corrupt column rejected
    EXPECT_EQ(result.entitiesRestored_, 0u);      // Rule #5: no partial counts
    EXPECT_EQ(m_em.getLiveEntityCount(), before); // zero world mutation
}

} // namespace

namespace {

using namespace WsSnap;
using IREntity::EntityId;

// The allocator watermark must advance past every restored id BEFORE the
// singleton loop, or a fresh-session singleton lazy-create draws a
// just-restored gameplay id and cross-wires onto it. A same-manager reload
// can't reproduce this (after a save the watermark already sits past the
// saved ids), so this drives a genuine second session with a fresh
// EntityManager whose watermark is back at the reserved base. See #2213.
TEST(WorldSnapshotFreshSession, WatermarkAdvancesBeforeSingletonLazyCreate) {
    const std::string path = testing::TempDir() + "/ir_ws_fresh_watermark.irws";
    std::vector<EntityId> gameplayIds;
    const C_WsSingletonState savedSingleton{1234, 5.5f};
    {
        IREntity::EntityManager em1; // ctor sets g_entityManager -> em1
        IRWorld::SaveRegistry reg;
        reg.registerComponent<C_WsPos>();
        reg.registerComponent<C_WsSingletonState>();
        // Gameplay entities first, so the lowest post-registration id belongs
        // to a gameplay entity — exactly the id a fresh-session singleton
        // lazy-create would otherwise draw.
        for (int i = 0; i < 8; ++i) {
            gameplayIds.push_back(
                em1.createEntity(C_WsPos{i, i + 1, i + 2}) & IREntity::IR_ENTITY_ID_BITS
            );
        }
        em1.getComponent<C_WsSingletonState>(em1.getOrCreateSingleton<C_WsSingletonState>()) =
            savedSingleton;
        ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());
    } // em1 destructs, clearing the global

    // A brand-new session: watermark back at the reserved base, the same two
    // components registered in the same order (same backing-entity count), so
    // without the pre-mutation advance the singleton lazy-create would draw
    // exactly gameplayIds[0].
    IREntity::EntityManager em2; // ctor sets g_entityManager -> em2
    IRWorld::SaveRegistry reg;
    reg.registerComponent<C_WsPos>();
    reg.registerComponent<C_WsSingletonState>();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;

    // The singleton restored by value...
    const C_WsSingletonState &restored = IREntity::singleton<C_WsSingletonState>();
    EXPECT_EQ(restored.counter_, savedSingleton.counter_);
    EXPECT_FLOAT_EQ(restored.weight_, savedSingleton.weight_);

    // ...onto a fresh id strictly above every restored gameplay id. This is
    // the fix's invariant: without the pre-mutation watermark advance the
    // lazy-create draws a low, already-restored id and this fails.
    const EntityId liveSingleton =
        IREntity::singletonEntityOrNull<C_WsSingletonState>() & IREntity::IR_ENTITY_ID_BITS;
    const EntityId maxGameplay = *std::max_element(gameplayIds.begin(), gameplayIds.end());
    EXPECT_GT(liveSingleton, maxGameplay);

    // Every restored gameplay entity keeps its exact C_WsPos — the singleton
    // value never cross-wired onto a gameplay record.
    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(em2.entityExists(gameplayIds[i])) << "missing id " << gameplayIds[i];
        auto pos = em2.getComponentOptional<C_WsPos>(gameplayIds[i]);
        ASSERT_TRUE(pos.has_value());
        EXPECT_EQ((*pos)->x_, i);
        EXPECT_EQ((*pos)->y_, i + 1);
        EXPECT_EQ((*pos)->z_, i + 2);
    }
}

} // namespace
