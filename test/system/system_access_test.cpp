#include <gtest/gtest.h>

#include <irreden/system/ir_system_types.hpp>
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
using IRSystem::deriveAccessFromSignature;
using IRSystem::Destroys;
using IRSystem::Exclude;
using IRSystem::MainThread;
using IRSystem::ParallelSafe;
using IRSystem::Spawns;
using IRSystem::SystemAccess;
using IRSystem::typeKey;

} // namespace

// ----------------------------------------------------------------------
// Per-component signature form
// ----------------------------------------------------------------------

TEST(SystemAccessTest, PerComponentInfersWritesByDefault) {
    auto access = deriveAccessFromSignature<void(C_AccessA &, C_AccessB &), C_AccessA, C_AccessB>();

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
        const C_AccessA,
        C_AccessB>();

    EXPECT_EQ(access.readCount_, 1u);
    EXPECT_EQ(access.writeCount_, 1u);
    EXPECT_TRUE(access.readsType<C_AccessA>());
    EXPECT_TRUE(access.writesType<C_AccessB>());
    EXPECT_FALSE(access.writesType<C_AccessA>());
}

TEST(SystemAccessTest, NoFalseAccessForUnreferencedTypes) {
    auto access = deriveAccessFromSignature<void(C_AccessA &), C_AccessA>();

    EXPECT_FALSE(access.readsType<C_AccessC>());
    EXPECT_FALSE(access.writesType<C_AccessC>());
}

// ----------------------------------------------------------------------
// Per-entity-id signature form
// ----------------------------------------------------------------------

TEST(SystemAccessTest, EntityIdParamFlipsUsesEntityId) {
    auto access = deriveAccessFromSignature<void(IREntity::EntityId &, C_AccessA &), C_AccessA>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_FALSE(access.isBatchForm_);
    EXPECT_TRUE(access.writesType<C_AccessA>());
}

TEST(SystemAccessTest, EntityIdParamConstReadIsHonored) {
    auto access =
        deriveAccessFromSignature<void(IREntity::EntityId &, const C_AccessA &), const C_AccessA>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_TRUE(access.readsType<C_AccessA>());
    EXPECT_FALSE(access.writesType<C_AccessA>());
}

// ----------------------------------------------------------------------
// Batch / per-archetype signature form
// ----------------------------------------------------------------------

TEST(SystemAccessTest, BatchSignatureFlipsIsBatchForm) {
    auto access = deriveAccessFromSignature<
        void(
            const IREntity::Archetype &,
            std::vector<IREntity::EntityId> &,
            std::vector<C_AccessA> &
        ),
        C_AccessA>();

    EXPECT_FALSE(access.usesEntityId_);
    EXPECT_TRUE(access.isBatchForm_);
    EXPECT_TRUE(access.writesType<C_AccessA>());
}

// ----------------------------------------------------------------------
// Tags
// ----------------------------------------------------------------------

TEST(SystemAccessTest, SpawnsTagFlipsBothSpawnsAndArchetypeGraph) {
    auto access = deriveAccessFromSignature<void(C_AccessA &), C_AccessA, Spawns>();

    EXPECT_TRUE(access.spawns_);
    EXPECT_TRUE(access.mutatesArchetypeGraph_);
    EXPECT_FALSE(access.destroys_);
}

TEST(SystemAccessTest, DestroysTagFlipsBothDestroysAndArchetypeGraph) {
    auto access = deriveAccessFromSignature<void(C_AccessA &), C_AccessA, Destroys>();

    EXPECT_TRUE(access.destroys_);
    EXPECT_TRUE(access.mutatesArchetypeGraph_);
    EXPECT_FALSE(access.spawns_);
}

TEST(SystemAccessTest, MainThreadAndParallelSafeTagsCompose) {
    auto access =
        deriveAccessFromSignature<void(C_AccessA &), C_AccessA, MainThread, ParallelSafe>();

    EXPECT_TRUE(access.mainThreadOnly_);
    EXPECT_TRUE(access.parallelSafe_);
}

TEST(SystemAccessTest, AlsoReadsAddsForeignReadEntry) {
    auto access = deriveAccessFromSignature<void(C_AccessA &), C_AccessA, AlsoReads<C_ForeignR>>();

    EXPECT_TRUE(access.writesType<C_AccessA>());
    EXPECT_TRUE(access.readsType<C_ForeignR>());
    EXPECT_FALSE(access.writesType<C_ForeignR>());
}

TEST(SystemAccessTest, AlsoWritesAddsForeignWriteEntry) {
    auto access = deriveAccessFromSignature<void(C_AccessA &), C_AccessA, AlsoWrites<C_ForeignW>>();

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
    auto access = deriveAccessFromSignature<void(C_AccessA &), C_AccessA, Exclude<C_AccessB>>();

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
        C_AccessA,
        const C_AccessB,
        Spawns,
        ParallelSafe>();

    static_assert(access.writeCount_ == 1u, "C_AccessA should be the only write");
    static_assert(access.readCount_ == 1u, "C_AccessB should be the only read");
    static_assert(access.spawns_, "Spawns tag should flip the flag");
    static_assert(access.parallelSafe_, "ParallelSafe tag should flip the flag");
    static_assert(access.mutatesArchetypeGraph_, "Spawns implies mutatesArchetypeGraph");

    EXPECT_TRUE(access.writesType<C_AccessA>());
    EXPECT_TRUE(access.readsType<C_AccessB>());
}

// ----------------------------------------------------------------------
// FilterTags_t no-op proof (#1803 blocker 1a)
//
// `createSystem` / `registerSystem` now feed the archetype matcher + the
// dispatch/member-tick binder a `detail::FilterTags_t<Pack...>` list in
// place of `detail::PartitionExcludes<Pack...>::Included`. The swap is a
// provable no-op for every system that exists in engine + creations today:
// none passes a tag type (`ParallelSafe` / `Spawns` / `Destroys` /
// `MainThread` / `AlsoReads` / `AlsoWrites`) in a registration pack, and for
// any tag-free pack the two lists are byte-identical — both strip
// `Exclude<...>` placeholders the same way, and `FilterTags_t` has nothing
// else to drop. The lists diverge ONLY when the pack carries a tag, which is
// the new capability: a tagged system (e.g. `ParallelSafe` for an audited
// PARALLEL_FOR body) gets the tag kept out of its archetype + tick signature
// instead of corrupting them. These static_asserts gate that invariant at
// compile time so a regression in either template fails the build.
// ----------------------------------------------------------------------

namespace {
template <typename... Pack>
using Included = typename IRSystem::detail::PartitionExcludes<Pack...>::Included;
template <typename... Pack> using Filtered = IRSystem::detail::FilterTags_t<Pack...>;
} // namespace

// Tag-free pack (the shape of every current system): identical lists.
static_assert(
    std::is_same_v<Filtered<C_AccessA, C_AccessB>, Included<C_AccessA, C_AccessB>>,
    "FilterTags_t must equal PartitionExcludes::Included for a plain pack"
);
// Exclude<...> placeholders are stripped identically by both.
static_assert(
    std::is_same_v<
        Filtered<C_AccessA, Exclude<C_AccessC>, C_AccessB>,
        Included<C_AccessA, Exclude<C_AccessC>, C_AccessB>>,
    "FilterTags_t must equal PartitionExcludes::Included for an Exclude-bearing pack"
);
// const-ness in the pack (reads) is preserved identically by both.
static_assert(
    std::is_same_v<Filtered<const C_AccessA, C_AccessB>, Included<const C_AccessA, C_AccessB>>,
    "FilterTags_t must preserve const exactly like PartitionExcludes::Included"
);
// Divergence is intentional and ONLY for a tagged pack: FilterTags drops the
// tag where PartitionExcludes::Included would leak it into the archetype —
// the exact bug blocker 1a fixes.
static_assert(
    !std::is_same_v<
        Filtered<C_AccessA, ParallelSafe, C_AccessB>,
        Included<C_AccessA, ParallelSafe, C_AccessB>>,
    "FilterTags_t must strip ParallelSafe where PartitionExcludes::Included leaks it"
);
static_assert(
    std::is_same_v<
        Filtered<C_AccessA, ParallelSafe, C_AccessB>,
        IRSystem::detail::TypeList<C_AccessA, C_AccessB>>,
    "FilterTags_t<A, ParallelSafe, B> must resolve to exactly TypeList<A, B>"
);
// Combined Exclude<...> + ParallelSafe: both kinds of tag stripped together.
static_assert(
    std::is_same_v<
        Filtered<C_AccessA, Exclude<C_AccessC>, ParallelSafe, C_AccessB>,
        IRSystem::detail::TypeList<C_AccessA, C_AccessB>>,
    "FilterTags_t must strip both Exclude and tag types in a combined pack"
);

TEST(SystemAccessTest, FilterTagsIsNoOpForTagFreePacks) {
    // Runtime mirror of the compile-time proof above so a CI run records a
    // PASS line for the 1a no-op invariant.
    EXPECT_TRUE((std::is_same_v<Filtered<C_AccessA, C_AccessB>, Included<C_AccessA, C_AccessB>>));
    EXPECT_TRUE((std::is_same_v<
                 Filtered<C_AccessA, Exclude<C_AccessC>, C_AccessB>,
                 Included<C_AccessA, Exclude<C_AccessC>, C_AccessB>>));
    EXPECT_FALSE((std::is_same_v<
                  Filtered<C_AccessA, ParallelSafe, C_AccessB>,
                  Included<C_AccessA, ParallelSafe, C_AccessB>>));
}
