#include <gtest/gtest.h>

#include <irreden/system/system_access.hpp>

namespace {

struct C_AccessA {
    int n_ = 0;
};
struct C_AccessB {
    int m_ = 0;
};
struct C_AccessC {
    float f_ = 0.0f;
};
struct C_ForeignR {
    int r_ = 0;
};
struct C_ForeignW {
    int w_ = 0;
};

using IRSystem::AlsoReads;
using IRSystem::AlsoWrites;
using IRSystem::Destroys;
using IRSystem::Exclude;
using IRSystem::MainThread;
using IRSystem::ParallelSafe;
using IRSystem::Spawns;
using IRSystem::SystemAccess;
using IRSystem::deriveAccessFromSignature;
using IRSystem::typeKey;

} // namespace

// ----------------------------------------------------------------------
// Per-component signature form
// ----------------------------------------------------------------------

TEST(SystemAccessTest, PerComponentInfersWritesByDefault) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &, C_AccessB &),
        C_AccessA, C_AccessB>();

    EXPECT_FALSE(access.usesEntityId_);
    EXPECT_FALSE(access.isBatchForm_);
    EXPECT_EQ(access.writeCount_, 2u);
    EXPECT_EQ(access.readCount_, 0u);
    EXPECT_TRUE(access.writesType<C_AccessA>());
    EXPECT_TRUE(access.writesType<C_AccessB>());
    EXPECT_FALSE(access.readsType<C_AccessA>());
}

TEST(SystemAccessTest, ConstInPackInfersRead) {
    auto access = deriveAccessFromSignature<
        void(const C_AccessA &, C_AccessB &),
        const C_AccessA, C_AccessB>();

    EXPECT_EQ(access.readCount_, 1u);
    EXPECT_EQ(access.writeCount_, 1u);
    EXPECT_TRUE(access.readsType<C_AccessA>());
    EXPECT_TRUE(access.writesType<C_AccessB>());
    EXPECT_FALSE(access.writesType<C_AccessA>());
}

TEST(SystemAccessTest, NoFalseAccessForUnreferencedTypes) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA>();

    EXPECT_FALSE(access.readsType<C_AccessC>());
    EXPECT_FALSE(access.writesType<C_AccessC>());
}

// ----------------------------------------------------------------------
// Per-entity-id signature form
// ----------------------------------------------------------------------

TEST(SystemAccessTest, EntityIdParamFlipsUsesEntityId) {
    auto access = deriveAccessFromSignature<
        void(IREntity::EntityId &, C_AccessA &),
        C_AccessA>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_FALSE(access.isBatchForm_);
    EXPECT_TRUE(access.writesType<C_AccessA>());
}

TEST(SystemAccessTest, EntityIdParamConstReadIsHonored) {
    auto access = deriveAccessFromSignature<
        void(IREntity::EntityId &, const C_AccessA &),
        const C_AccessA>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_TRUE(access.readsType<C_AccessA>());
    EXPECT_FALSE(access.writesType<C_AccessA>());
}

// ----------------------------------------------------------------------
// Batch / per-archetype signature form
// ----------------------------------------------------------------------

TEST(SystemAccessTest, BatchSignatureFlipsIsBatchForm) {
    auto access = deriveAccessFromSignature<
        void(const IREntity::Archetype &, std::vector<IREntity::EntityId> &,
             std::vector<C_AccessA> &),
        C_AccessA>();

    EXPECT_FALSE(access.usesEntityId_);
    EXPECT_TRUE(access.isBatchForm_);
    EXPECT_TRUE(access.writesType<C_AccessA>());
}

// ----------------------------------------------------------------------
// Tags
// ----------------------------------------------------------------------

TEST(SystemAccessTest, SpawnsTagFlipsBothSpawnsAndArchetypeGraph) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA, Spawns>();

    EXPECT_TRUE(access.spawns_);
    EXPECT_TRUE(access.mutatesArchetypeGraph_);
    EXPECT_FALSE(access.destroys_);
}

TEST(SystemAccessTest, DestroysTagFlipsBothDestroysAndArchetypeGraph) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA, Destroys>();

    EXPECT_TRUE(access.destroys_);
    EXPECT_TRUE(access.mutatesArchetypeGraph_);
    EXPECT_FALSE(access.spawns_);
}

TEST(SystemAccessTest, MainThreadAndParallelSafeTagsCompose) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA, MainThread, ParallelSafe>();

    EXPECT_TRUE(access.mainThreadOnly_);
    EXPECT_TRUE(access.parallelSafe_);
}

TEST(SystemAccessTest, AlsoReadsAddsForeignReadEntry) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA, AlsoReads<C_ForeignR>>();

    EXPECT_TRUE(access.writesType<C_AccessA>());
    EXPECT_TRUE(access.readsType<C_ForeignR>());
    EXPECT_FALSE(access.writesType<C_ForeignR>());
}

TEST(SystemAccessTest, AlsoWritesAddsForeignWriteEntry) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA, AlsoWrites<C_ForeignW>>();

    EXPECT_TRUE(access.writesType<C_AccessA>());
    EXPECT_TRUE(access.writesType<C_ForeignW>());
    EXPECT_FALSE(access.readsType<C_ForeignW>());
}

TEST(SystemAccessTest, AlsoReadsAndAlsoWritesAcceptMultipleTypes) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA,
        AlsoReads<C_ForeignR, C_AccessB>,
        AlsoWrites<C_ForeignW, C_AccessC>>();

    EXPECT_TRUE(access.readsType<C_ForeignR>());
    EXPECT_TRUE(access.readsType<C_AccessB>());
    EXPECT_TRUE(access.writesType<C_ForeignW>());
    EXPECT_TRUE(access.writesType<C_AccessC>());
    EXPECT_TRUE(access.writesType<C_AccessA>());
}

TEST(SystemAccessTest, ExcludeContributesNoAccess) {
    auto access = deriveAccessFromSignature<
        void(C_AccessA &),
        C_AccessA, Exclude<C_AccessB>>();

    EXPECT_TRUE(access.writesType<C_AccessA>());
    EXPECT_FALSE(access.readsType<C_AccessB>());
    EXPECT_FALSE(access.writesType<C_AccessB>());
}

// ----------------------------------------------------------------------
// Type keys are stable across instantiations
// ----------------------------------------------------------------------

TEST(SystemAccessTest, TypeKeyIsStableAndUnique) {
    EXPECT_EQ(typeKey<C_AccessA>, typeKey<C_AccessA>);
    EXPECT_EQ(typeKey<C_AccessA>, typeKey<const C_AccessA>);
    EXPECT_NE(typeKey<C_AccessA>, typeKey<C_AccessB>);
}

// ----------------------------------------------------------------------
// constexpr verification: deriveAccessFromSignature is fully constexpr
// ----------------------------------------------------------------------

TEST(SystemAccessTest, DeriveAccessFromSignatureIsConstexpr) {
    constexpr SystemAccess access = deriveAccessFromSignature<
        void(C_AccessA &, const C_AccessB &),
        C_AccessA, const C_AccessB, Spawns, ParallelSafe>();

    static_assert(access.writeCount_ == 1u, "C_AccessA should be the only write");
    static_assert(access.readCount_ == 1u, "C_AccessB should be the only read");
    static_assert(access.spawns_, "Spawns tag should flip the flag");
    static_assert(access.parallelSafe_, "ParallelSafe tag should flip the flag");
    static_assert(access.mutatesArchetypeGraph_, "Spawns implies mutatesArchetypeGraph");

    EXPECT_TRUE(access.writesType<C_AccessA>());
    EXPECT_TRUE(access.readsType<C_AccessB>());
}
