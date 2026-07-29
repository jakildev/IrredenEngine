#include <gtest/gtest.h>

#include <irreden/world/save_registry.hpp>
#include <irreden/world/save_trait.hpp>
#include <irreden/world/world_snapshot.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/ir_entity.hpp>

// The heap-owning half of this file (#2242) registers real engine components,
// so it needs their SaveSerialize<C> specializations in scope — the same
// include contract world_default_registry.cpp follows. The inventory include
// is load-bearing, not incidental: registerComponent<C> is `if constexpr`-gated
// on shouldSave<C>(), and without the IR_SAVE_OPT_IN specializations in scope
// SaveTrait<C> falls back to the "no decision yet" primary (kSave = false), so
// every registerComponent call would silently no-op and the test would save an
// empty world and still pass its ok() assertions.
#include <irreden/world/save_component_inventory.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/common/save_serializers_common.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/save_serializers_render.hpp>
#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/save_serializers_voxel.hpp>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Persist P4 (#2215): the epic's acceptance harness for the whole-world
// snapshot path — round-trip parity (W-7) and deterministic serialization
// (W-8) over a non-trivial, multi-archetype, CHILD_OF-structured world with
// a singleton. In-TU stand-in components; the plumbing under test is
// IRWorld::saveWorld/loadWorld itself (P1-P3), not any one component's
// bytes. Fixtures build CHILD_OF structure only — setRelation asserts on
// PARENT_TO/SIBLING_OF everywhere in the engine today (#2214), so those
// types are out of scope here.
//
// One EntityManager per test (mirrors world_snapshot_test.cpp /
// world_snapshot_relations_test.cpp), not two sequentially-scoped managers:
// component/relation registration mints backing-entity ids from the same
// counter as gameplay entities, so a second fresh manager's registry setup
// would collide with the first manager's saved id range. destroyAllEntities
// frees the live set without recycling ids, which is exactly what the
// restored (identical) ids need to land on.
namespace PersistSnap {
struct C_PA {
    std::int32_t a_ = 0;
};
struct C_PB {
    std::int32_t b_ = 0;
};
struct C_PC {
    std::uint32_t c_ = 0;
};
struct C_PMark {
    std::int32_t marker_ = 0;
};
} // namespace PersistSnap

IR_SAVE_OPT_IN(PersistSnap::C_PA, 1)
IR_SAVE_OPT_IN(PersistSnap::C_PB, 1)
IR_SAVE_OPT_IN(PersistSnap::C_PC, 1)
IR_SAVE_OPT_IN(PersistSnap::C_PMark, 1)

namespace {

using namespace PersistSnap;
using IREntity::EntityId;

constexpr int kPerArchetype = 32; // 4 archetypes * 32 = 128 entities (>100).

// Handles into the built world an assertion needs after load.
struct WorldHandles {
    std::vector<EntityId> archetype1_; // {C_PA}
    std::vector<EntityId> archetype2_; // {C_PA, C_PB}
    std::vector<EntityId> archetype3_; // {C_PA, C_PB, C_PC}
    std::vector<EntityId> archetype4_; // {C_PMark, C_PA}
    EntityId root_ = IREntity::kNullEntity;
    EntityId branch_ = IREntity::kNullEntity;
    EntityId leaf_ = IREntity::kNullEntity;
    EntityId hub_ = IREntity::kNullEntity;
};

class PersistRoundTripFixture : public testing::Test {
  protected:
    IRWorld::SaveRegistry registry() {
        IRWorld::SaveRegistry reg;
        reg.registerComponent<C_PA>();
        reg.registerComponent<C_PB>();
        reg.registerComponent<C_PC>();
        reg.registerComponent<C_PMark>();
        return reg;
    }

    std::string tempPath(const char *name) const {
        return testing::TempDir() + "/ir_persist_rt_" + name + ".irws";
    }

    static std::vector<std::uint8_t> readFileBytes(const std::string &path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        );
    }

    // FNV-1a 64-bit digest (W-8) — a compact stand-in for a byte compare in
    // assertion messages; the tests still assert byte identity directly too.
    static std::uint64_t fnv1a64(const std::vector<std::uint8_t> &bytes) {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        for (std::uint8_t byte : bytes) {
            hash ^= byte;
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

    // Builds the 128-entity / 4-archetype / CHILD_OF-structured / singleton
    // world on m_em. Every field is index-derived — no RNG, no clock.
    // Relation structure: a three-level chain (root -> branch -> leaf, one
    // archetype each) plus a hub entity parenting one child from every
    // archetype, so CHILD_OF edges span archetype boundaries.
    WorldHandles buildNonTrivialWorld() {
        WorldHandles h;
        h.archetype1_.reserve(kPerArchetype);
        h.archetype2_.reserve(kPerArchetype);
        h.archetype3_.reserve(kPerArchetype);
        h.archetype4_.reserve(kPerArchetype);

        for (int i = 0; i < kPerArchetype; ++i) {
            h.archetype1_.push_back(m_em.createEntity(C_PA{i}) & IREntity::IR_ENTITY_ID_BITS);
        }
        for (int i = 0; i < kPerArchetype; ++i) {
            h.archetype2_.push_back(
                m_em.createEntity(C_PA{100 + i}, C_PB{i * 2}) & IREntity::IR_ENTITY_ID_BITS
            );
        }
        for (int i = 0; i < kPerArchetype; ++i) {
            h.archetype3_.push_back(
                m_em.createEntity(C_PA{200 + i}, C_PB{i * 3}, C_PC{static_cast<std::uint32_t>(i)}) &
                IREntity::IR_ENTITY_ID_BITS
            );
        }
        for (int i = 0; i < kPerArchetype; ++i) {
            h.archetype4_.push_back(
                m_em.createEntity(C_PMark{i}, C_PA{300 + i}) & IREntity::IR_ENTITY_ID_BITS
            );
        }

        h.root_ = h.archetype1_[0];
        h.branch_ = h.archetype2_[0];
        h.leaf_ = h.archetype3_[0];
        IREntity::setParent(h.branch_, h.root_);
        IREntity::setParent(h.leaf_, h.branch_);

        h.hub_ = h.archetype4_[0];
        IREntity::setParent(h.archetype1_[1], h.hub_);
        IREntity::setParent(h.archetype2_[1], h.hub_);
        IREntity::setParent(h.archetype3_[1], h.hub_);
        IREntity::setParent(h.archetype4_[1], h.hub_);

        IREntity::singleton<C_PC>().c_ = 0xABCDu;

        return h;
    }

    IREntity::EntityManager m_em; // ctor sets g_entityManager -> this
};

using PersistRoundTrip = PersistRoundTripFixture;
using PersistDeterminism = PersistRoundTripFixture;

// W-7: build the non-trivial world, save it, load it back (after
// destroyAllEntities), and assert structural parity: every restored entity
// keeps its exact id, archetype membership, and field values, and the
// singleton value round-trips (by value — its raw id does not: a singleton
// is recreated via the lazy singletonEntity<C>() path on load, which mints
// a fresh id from the current watermark rather than reusing the original
// one, exactly like a cross-session load, which is why the alias map exists.
// That id churn means this test does not assert byte-for-byte identity
// against the pre-load file (SNGL's raw entity id differs); byte/digest
// determinism is covered separately by PersistDeterminism, which never
// compares across a singleton-churning load boundary.
TEST_F(PersistRoundTrip, ParityAcrossFreshWorld) {
    WorldHandles expected = buildNonTrivialWorld();
    const std::uint32_t expectedSingletonC = IREntity::singleton<C_PC>().c_;

    IRWorld::SaveRegistry reg = registry();
    const std::string pathA = tempPath("parity_a");
    const std::string pathB = tempPath("parity_b");
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathA).ok());

    m_em.destroyAllEntities();
    ASSERT_EQ(m_em.getLiveEntityCount(), 0u);

    IRWorld::LoadResult result = IRWorld::loadWorld(reg, pathA);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, static_cast<std::uint64_t>(4 * kPerArchetype));
    // root -> branch -> leaf, hub -> 4 children.
    EXPECT_EQ(result.relationsRestored_, 6u);
    EXPECT_EQ(result.relationsSkipped_, 0u);
    EXPECT_EQ(result.columnsSkipped_, 0u);
    EXPECT_EQ(result.singletonsSkipped_, 0u);

    // Structural parity: per-archetype counts (each include set below
    // uniquely identifies one of the four archetypes). Not asserting the
    // total live-entity count: it mixes in framework bookkeeping entities
    // (component-backing entities, CHILD_OF relation entities, the
    // singleton) whose count legitimately differs across a destroy+load
    // cycle — e.g. the bookkeeping entities from the first-time
    // registerComponent<C> calls are gone after destroyAllEntities and are
    // not recreated on load (the SaveRegistry's type->id mapping already
    // resolves without them), while the singleton IS recreated fresh.
    EXPECT_EQ(
        IREntity::collectEntitiesSimple(IREntity::getArchetype<C_PA, C_PB, C_PC>()).size(),
        static_cast<std::size_t>(kPerArchetype)
    );
    EXPECT_EQ(
        IREntity::collectEntitiesSimple(IREntity::getArchetype<C_PMark, C_PA>()).size(),
        static_cast<std::size_t>(kPerArchetype)
    );

    // Sampled field values across every archetype.
    for (int i = 0; i < kPerArchetype; ++i) {
        ASSERT_TRUE(m_em.entityExists(expected.archetype1_[i]));
        EXPECT_EQ(m_em.getComponent<C_PA>(expected.archetype1_[i]).a_, i);

        ASSERT_TRUE(m_em.entityExists(expected.archetype2_[i]));
        EXPECT_EQ(m_em.getComponent<C_PA>(expected.archetype2_[i]).a_, 100 + i);
        EXPECT_EQ(m_em.getComponent<C_PB>(expected.archetype2_[i]).b_, i * 2);

        ASSERT_TRUE(m_em.entityExists(expected.archetype3_[i]));
        EXPECT_EQ(m_em.getComponent<C_PA>(expected.archetype3_[i]).a_, 200 + i);
        EXPECT_EQ(m_em.getComponent<C_PB>(expected.archetype3_[i]).b_, i * 3);
        EXPECT_EQ(
            m_em.getComponent<C_PC>(expected.archetype3_[i]).c_,
            static_cast<std::uint32_t>(i)
        );

        ASSERT_TRUE(m_em.entityExists(expected.archetype4_[i]));
        EXPECT_EQ(m_em.getComponent<C_PMark>(expected.archetype4_[i]).marker_, i);
        EXPECT_EQ(m_em.getComponent<C_PA>(expected.archetype4_[i]).a_, 300 + i);
    }

    // Singleton value round-trips; its restored id is a fresh alias, not
    // the original raw id (see the class comment above).
    EXPECT_EQ(IREntity::singleton<C_PC>().c_, expectedSingletonC);
    ASSERT_EQ(result.singletonAliases_.size(), 1u);

    // Re-serializing the freshly loaded world at least succeeds and is
    // itself reproducible (strict digest/byte stability of a loaded world
    // is asserted by PersistDeterminism.SaveLoadSaveDigestStable).
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathB).ok());
}

// W-7 acceptance clause 2: every CHILD_OF edge (chain + hub's multi-child
// spread) is individually recovered after load.
TEST_F(PersistRoundTrip, ChildOfEdgesSurviveLoad) {
    WorldHandles expected = buildNonTrivialWorld();
    IRWorld::SaveRegistry reg = registry();
    const std::string path = tempPath("edges");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    m_em.destroyAllEntities();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.relationsRestored_, 6u);

    EXPECT_EQ(
        m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(expected.branch_)),
        expected.root_
    );
    EXPECT_EQ(
        m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(expected.leaf_)),
        expected.branch_
    );
    EXPECT_EQ(
        m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(expected.root_)),
        IREntity::kNullEntity
    );

    EXPECT_EQ(
        m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(expected.archetype1_[1])),
        expected.hub_
    );
    EXPECT_EQ(
        m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(expected.archetype2_[1])),
        expected.hub_
    );
    EXPECT_EQ(
        m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(expected.archetype3_[1])),
        expected.hub_
    );
    EXPECT_EQ(
        m_em.getParentEntityFromArchetype(m_em.getEntityArchetype(expected.archetype4_[1])),
        expected.hub_
    );
}

// W-8: two consecutive saves of the same unmutated world are byte-identical
// and share the same digest.
TEST_F(PersistDeterminism, SameWorldSameBytes) {
    buildNonTrivialWorld();
    IRWorld::SaveRegistry reg = registry();
    const std::string pathA = tempPath("det_a");
    const std::string pathB = tempPath("det_b");
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathA).ok());
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathB).ok());

    const std::vector<std::uint8_t> bytesA = readFileBytes(pathA);
    const std::vector<std::uint8_t> bytesB = readFileBytes(pathB);
    ASSERT_FALSE(bytesA.empty());
    EXPECT_EQ(bytesA, bytesB);
    EXPECT_EQ(fnv1a64(bytesA), fnv1a64(bytesB));
}

// W-8: save -> load (after destroyAllEntities) -> save twice in a row with
// no mutation in between; the two post-load saves are byte-identical. This
// proves the *loaded* world serializes deterministically — it deliberately
// does not compare against the pre-load file, because the singleton is
// recreated via the lazy singletonEntity<C>() path on load (a fresh id from
// the current watermark, not the original raw id — see
// PersistRoundTrip.ParityAcrossFreshWorld), so the pre- and post-load SNGL
// bytes differ by design even though every other chunk round-trips exactly.
TEST_F(PersistDeterminism, SaveLoadSaveDigestStable) {
    buildNonTrivialWorld();
    IRWorld::SaveRegistry reg = registry();
    const std::string pathSource = tempPath("cycle_source");
    const std::string pathB = tempPath("cycle_b");
    const std::string pathC = tempPath("cycle_c");
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathSource).ok());

    m_em.destroyAllEntities();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, pathSource);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathB).ok());
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathC).ok());

    const std::vector<std::uint8_t> bytesB = readFileBytes(pathB);
    const std::vector<std::uint8_t> bytesC = readFileBytes(pathC);
    ASSERT_FALSE(bytesB.empty());
    EXPECT_EQ(fnv1a64(bytesB), fnv1a64(bytesC));
    EXPECT_EQ(bytesB, bytesC);
}

// #2242: W-7 and W-8 over *heap-owning* engine components. Every test above
// uses in-TU trivially-copyable stand-ins, so they exercise the walker but
// never a real SaveSerialize<C> specialization. This one drives the direct
// C++ saveWorld(registry, path) surface (the Lua binding's coverage lives in
// test/script/lua_world_snapshot_test.cpp) with a world whose components own
// a string, two independent vectors, and a hash map — the three shapes where
// a serializer can silently drop content or emit non-deterministic bytes.
class PersistHeapOwningFixture : public PersistRoundTripFixture {
  protected:
    IRWorld::SaveRegistry heapOwningRegistry() {
        IRWorld::SaveRegistry reg;
        reg.registerComponent<IRComponents::C_Name>();
        reg.registerComponent<IRComponents::C_Skeleton>();
        reg.registerComponent<IRComponents::C_WidgetLabel>();
        reg.registerComponent<IRComponents::C_BindPoints>();
        // Vacuity guard: registerComponent no-ops for a type whose
        // IR_SAVE_OPT_IN is not in scope, which would make every assertion
        // below pass against an empty save.
        EXPECT_EQ(reg.size(), 4u) << "registry is not holding all four components";
        return reg;
    }

    // Deliberately inserts the bind points in non-sorted order: the map's
    // iteration order is not the write order, so a serializer that skipped
    // the sort would produce different bytes here than for the same content
    // inserted the other way round.
    void buildHeapOwningWorld() {
        m_named =
            m_em.createEntity(IRComponents::C_Name{"the player"}) & IREntity::IR_ENTITY_ID_BITS;
        m_emptyNamed = m_em.createEntity(IRComponents::C_Name{""}) & IREntity::IR_ENTITY_ID_BITS;

        IRComponents::C_WidgetLabel label{};
        label.text_ = "Frames/sec";
        label.colorOverride_ = IRMath::Color{9, 8, 7, 240};
        m_widget = m_em.createEntity(label) & IREntity::IR_ENTITY_ID_BITS;

        IRComponents::C_Skeleton skeleton{};
        skeleton.joints_ = {101, IREntity::kNullEntity, 103};
        skeleton.bindPose_.resize(3);
        skeleton.bindPose_[1].translation_ = IRMath::vec3(1.5f, -2.5f, 3.5f);

        IRComponents::C_BindPoints points{};
        points.setPoint(
            "hand.R",
            IRComponents::BindPointRuntime{
                2,
                IRMath::vec3(1.0f, 0.0f, 0.0f),
                IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
            }
        );
        points.setPoint(
            "hand.L",
            IRComponents::BindPointRuntime{
                1,
                IRMath::vec3(-1.0f, 0.0f, 0.0f),
                IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
            }
        );
        points.setPoint(
            "head",
            IRComponents::BindPointRuntime{
                0,
                IRMath::vec3(0.0f, 0.0f, 2.0f),
                IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)
            }
        );
        m_rig = m_em.createEntity(skeleton, points) & IREntity::IR_ENTITY_ID_BITS;
    }

    IREntity::EntityId m_named = IREntity::kNullEntity;
    IREntity::EntityId m_emptyNamed = IREntity::kNullEntity;
    IREntity::EntityId m_widget = IREntity::kNullEntity;
    IREntity::EntityId m_rig = IREntity::kNullEntity;
};

using PersistHeapOwning = PersistHeapOwningFixture;

TEST_F(PersistHeapOwning, ValuesSurviveRoundTrip) {
    buildHeapOwningWorld();
    IRWorld::SaveRegistry reg = heapOwningRegistry();
    const std::string path = tempPath("heap_owning");
    ASSERT_TRUE(IRWorld::saveWorld(reg, path).ok());

    m_em.destroyAllEntities();
    IRWorld::LoadResult result = IRWorld::loadWorld(reg, path);
    ASSERT_TRUE(result.ok()) << result.status_.message_;
    EXPECT_EQ(result.entitiesRestored_, 4u);
    EXPECT_EQ(result.columnsSkipped_, 0u);

    ASSERT_TRUE(m_em.entityExists(m_named));
    EXPECT_EQ(m_em.getComponent<IRComponents::C_Name>(m_named).name_, "the player");
    ASSERT_TRUE(m_em.entityExists(m_emptyNamed));
    EXPECT_EQ(m_em.getComponent<IRComponents::C_Name>(m_emptyNamed).name_, "");

    ASSERT_TRUE(m_em.entityExists(m_widget));
    const IRComponents::C_WidgetLabel &label =
        m_em.getComponent<IRComponents::C_WidgetLabel>(m_widget);
    EXPECT_EQ(label.text_, "Frames/sec");
    EXPECT_EQ(label.colorOverride_.red_, 9);
    EXPECT_EQ(label.colorOverride_.alpha_, 240);

    ASSERT_TRUE(m_em.entityExists(m_rig));
    const IRComponents::C_Skeleton &skeleton = m_em.getComponent<IRComponents::C_Skeleton>(m_rig);
    ASSERT_EQ(skeleton.joints_.size(), 3u);
    EXPECT_EQ(skeleton.joints_[1], IREntity::kNullEntity)
        << "a severance hole must not be compacted away — slot order is the bone-id space";
    EXPECT_EQ(skeleton.joints_[2], 103u);
    ASSERT_EQ(skeleton.bindPose_.size(), 3u);
    EXPECT_FLOAT_EQ(skeleton.bindPose_[1].translation_.y, -2.5f);

    const IRComponents::C_BindPoints &points = m_em.getComponent<IRComponents::C_BindPoints>(m_rig);
    ASSERT_EQ(points.points_.size(), 3u);
    EXPECT_EQ(points.points_.at("hand.L").boneId_, 1u);
    EXPECT_FLOAT_EQ(points.points_.at("head").offset_.z, 2.0f);
}

TEST_F(PersistHeapOwning, DoubleSaveIsByteIdentical) {
    buildHeapOwningWorld();
    IRWorld::SaveRegistry reg = heapOwningRegistry();
    const std::string pathA = tempPath("heap_det_a");
    const std::string pathB = tempPath("heap_det_b");
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathA).ok());
    ASSERT_TRUE(IRWorld::saveWorld(reg, pathB).ok());

    const std::vector<std::uint8_t> bytesA = readFileBytes(pathA);
    const std::vector<std::uint8_t> bytesB = readFileBytes(pathB);
    ASSERT_FALSE(bytesA.empty());
    EXPECT_EQ(bytesA, bytesB);
    EXPECT_EQ(fnv1a64(bytesA), fnv1a64(bytesB));
}

} // namespace
