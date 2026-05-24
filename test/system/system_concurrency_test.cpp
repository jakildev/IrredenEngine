#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/system/ir_assert_main_thread.hpp>
#include <irreden/system/system_access.hpp>
#include <irreden/system/system_manager.hpp>

#include <atomic>
#include <optional>
#include <vector>

// T-222 Phase 2 — multithreading epic (#226). Two surfaces under test:
//
//   1. The registration-time validator that rejects bad PARALLEL_FOR
//      combinations (per-entity-id without ParallelSafe, batch form,
//      MainThread tag).
//   2. The IR_ASSERT_MAIN_THREAD macro that catches "lambda body
//      escaped into globals" cases the access trait cannot see.
//
// The validator is reachable only via the namespace-detail entry point
// in ir_system.hpp; we mirror the body here rather than expose it (it
// is a pure function of `SystemAccess` + `Concurrency`).
//
// Worker-side IR_ASSERT_MAIN_THREAD invocation is intentionally NOT
// tested: the assert throws std::runtime_error, and enkiTS' pinned-task
// path does not catch worker exceptions — std::terminate would kill
// the test binary. The robust shape is to test the building block
// (`IRJob::isMainThread()` on a worker) and trust the macro's
// expansion.

namespace {

struct C_VelA {
    int n_ = 0;
};
struct C_VelB {
    int m_ = 0;
};
struct C_Slot {
    int idx_ = 0;
};
struct C_Counter {
    int n_ = 0;
};

using IRSystem::AlsoReads;
using IRSystem::AlsoWrites;
using IRSystem::Concurrency;
using IRSystem::deriveAccessFromSignature;
using IRSystem::MainThread;
using IRSystem::ParallelSafe;
using IRSystem::SystemAccess;

// Local mirror of `IRSystem::detail::validateConcurrencyForAccess`'s
// preconditions. The production helper IR_ASSERTs on failure (throws
// std::runtime_error via engAssert); we re-derive the predicate here
// so the test can run in IR_RELEASE builds too, and so the diagnostics
// stay stable across the validator's internal wording changes.
// Rules are listed most-specific-first, mirroring the production validator
// (T-349).
constexpr bool isParallelForAcceptable(Concurrency c, SystemAccess access) {
    if (c != Concurrency::PARALLEL_FOR) {
        return true;
    }
    if (access.isRelationForm_) {
        return false;
    }
    if (access.isBatchForm_) {
        return false;
    }
    if (access.usesEntityId_ && !access.parallelSafe_) {
        return false;
    }
    if (access.mainThreadOnly_) {
        return false;
    }
    return true;
}

class JobManagerFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        // 2 workers is enough to exercise main-vs-worker thread
        // identity without depending on hardware_concurrency.
        m_jobs = std::make_unique<IRJob::JobManager>(2);
    }
    void TearDown() override {
        m_jobs.reset();
    }
    std::unique_ptr<IRJob::JobManager> m_jobs;
};

} // namespace

// ----------------------------------------------------------------------
// Validator — PARALLEL_FOR rejection cases
// ----------------------------------------------------------------------

TEST(SystemConcurrencyValidator, PerComponentFormAcceptsParallelFor) {
    // void tick(C_VelA&, const C_VelB&) — the canonical clean case.
    // Writes C_VelA, reads C_VelB, no EntityId, not batch form.
    auto access = deriveAccessFromSignature<void(C_VelA &, const C_VelB &), C_VelA, const C_VelB>();

    EXPECT_FALSE(access.usesEntityId_);
    EXPECT_FALSE(access.isBatchForm_);
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
}

TEST(SystemConcurrencyValidator, PerEntityIdFormRejectedWithoutParallelSafe) {
    // void tick(EntityId, C_VelA&) — id-aware form. Without an
    // explicit ParallelSafe tag, the body is presumed to dereference
    // the id into a non-thread-safe singleton.
    auto access = deriveAccessFromSignature<void(IREntity::EntityId &, C_VelA &), C_VelA>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_FALSE(access.parallelSafe_);
    EXPECT_FALSE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
    // SERIAL stays acceptable in the same configuration.
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::SERIAL, access));
}

TEST(SystemConcurrencyValidator, PerEntityIdFormAcceptedWithParallelSafe) {
    // Combining EntityId + ParallelSafe in the Components pack must
    // derive BOTH flags — the trait filters tag types out of the
    // signature probes (T-328 sub-task D), so the pack mixing a real
    // tick signature with markers like `ParallelSafe` no longer
    // suppresses `usesEntityId_`.
    auto access =
        deriveAccessFromSignature<void(IREntity::EntityId &, C_VelA &), C_VelA, ParallelSafe>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_TRUE(access.parallelSafe_);
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
}

TEST(SystemAccessTagFilter, EntityIdAndMainThreadComposeWithSignatureProbe) {
    // The signature probes used to fail when tag types appeared in the
    // Components pack alongside a real `EntityId`-aware tick signature.
    // Sub-task D filters tags before probing; this asserts the combined
    // derivation now returns the expected flags from a single call.
    auto access = deriveAccessFromSignature<
        void(IREntity::EntityId &, C_VelA &),
        C_VelA,
        ParallelSafe,
        MainThread>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_FALSE(access.isBatchForm_);
    EXPECT_TRUE(access.parallelSafe_);
    EXPECT_TRUE(access.mainThreadOnly_);
}

TEST(SystemConcurrencyValidator, BatchFormRejected) {
    // void tick(const Archetype&, std::vector<EntityId>&, std::vector<C_VelA>&)
    // — per-archetype batch form. The body consumes the whole column,
    // so row-level chunking would re-enter it with overlapping
    // vectors. PARALLEL_FOR is structurally incompatible.
    auto access = deriveAccessFromSignature<
        void(const IREntity::Archetype &, std::vector<IREntity::EntityId> &, std::vector<C_VelA> &),
        C_VelA>();

    EXPECT_TRUE(access.isBatchForm_);
    EXPECT_FALSE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
    // SERIAL stays acceptable; the batch form is fine on the main
    // thread.
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::SERIAL, access));
}

TEST(SystemConcurrencyValidator, RelationFormRejected) {
    // T-334: a tick with `(Components&..., std::optional<RelComps*>...)`
    // is the relation form. `rangedFn`'s relation branch in
    // system_manager.hpp calls `getRelatedEntityFromArchetype` +
    // `getComponentOptional` on `EntityManager` inside the per-row
    // loop, which would race on the manager from worker threads.
    //
    // The trait CANNOT see the second pack (`RelationComponents...`)
    // because the two packs collide in a free-function template form
    // (see the TODO at `InvocableWithOptionalRelations` in
    // ir_system_types.hpp). createSystem folds the bit in via a
    // constexpr lambda where both packs are in scope, so we exercise
    // the validator's contract here by constructing the descriptor
    // directly — same shape as PerEntityIdFormAcceptedWithParallelSafe.
    SystemAccess access{};
    access.isRelationForm_ = true;
    EXPECT_FALSE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
    // SERIAL / MAIN_THREAD stay acceptable; the relation form is fine
    // on the main thread.
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::SERIAL, access));
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::MAIN_THREAD, access));
}

TEST(SystemConcurrencyValidator, MainThreadTagRejectsParallelFor) {
    // The MainThread tag is explicit "do not parallelize"; the
    // validator must FATAL rather than silently downgrade.
    auto access = deriveAccessFromSignature<void(C_VelA &), C_VelA, MainThread>();

    EXPECT_TRUE(access.mainThreadOnly_);
    EXPECT_FALSE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::MAIN_THREAD, access));
}

// ----------------------------------------------------------------------
// createSystem integration — full path through the constexpr-lambda
// relation-form detector + the registration-time validator.
// ----------------------------------------------------------------------

// The validator tests above mirror its predicate locally so they can
// pin the contract without standing up an ECS. The detection logic that
// SETS `isRelationForm_` lives in the `constexpr` lambda inside
// `createSystem` (ir_system.hpp) and is exercised only by an actual
// `createSystem<...>(...)` invocation — a silent regression in the
// `std::is_invocable_v` probe (typo, wrong reference category, missing
// pack expansion) would not be caught by any of the
// `deriveAccessFromSignature`-based tests above. Standing up
// EntityManager + SystemManager once in a fixture lets us drive the
// full path: probe sets the bit, validator reads it, IR_ASSERT throws.
class CreateSystemValidatorTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(CreateSystemValidatorTest, RelationFormParallelForFatalsAtRegistration) {
    // T-334 nit: covers the `constexpr` lambda probe in `createSystem`
    // that sets `isRelationForm_`. A tick taking
    // `(Components&..., std::optional<RelComps*>...)` is the relation
    // form. With Concurrency::PARALLEL_FOR the registration-time
    // validator must FATAL — the relation branch resolves the related
    // entity and its components via EntityManager lookups inside the
    // per-row loop, which is not thread-safe.
    auto relationTickBody = [](C_VelA &a, std::optional<C_VelB *> b) {
        (void)a;
        (void)b;
    };
    // IR_ASSERT throws std::runtime_error in debug builds (test binary
    // is built debug — see test/ecs/modifier_quat_runtime_test.cpp:427).
    EXPECT_THROW(
        (IRSystem::createSystem<C_VelA>(
            "TestRelationFormParallelForRejected",
            relationTickBody,
            nullptr,
            nullptr,
            IRSystem::RelationParams<C_VelB>{},
            nullptr,
            Concurrency::PARALLEL_FOR
        )),
        std::runtime_error
    );
}

TEST_F(CreateSystemValidatorTest, RelationFormSerialAcceptedAtRegistration) {
    // Companion to the FATAL case above: the same relation-form body
    // must register cleanly under Concurrency::SERIAL — the relation
    // form is fine on the main thread. This pins the probe's
    // disposition: it sets `isRelationForm_`, but only the
    // PARALLEL_FOR validator rule fires on it.
    auto relationTickBody = [](C_VelA &a, std::optional<C_VelB *> b) {
        (void)a;
        (void)b;
    };
    EXPECT_NO_THROW({
        IRSystem::createSystem<C_VelA>(
            "TestRelationFormSerialAccepted",
            relationTickBody,
            nullptr,
            nullptr,
            IRSystem::RelationParams<C_VelB>{},
            nullptr,
            Concurrency::SERIAL
        );
    });
}

TEST_F(CreateSystemValidatorTest, CatchAllWithRelationParamsFatalsAtRegistration) {
    // T-349: a variadic catch-all tick simultaneously satisfies every
    // signature probe (entity-id, batch-form, relation-form). The
    // validator rules are ordered most-specific-first (relation →
    // batch → entity-id) so the relation-form assertion fires first;
    // the log output names the condition (!access.isRelationForm_).
    // This test pins that PARALLEL_FOR + catch-all + RelationParams is
    // always rejected — a different shape than the explicit relation-
    // form case in RelationFormParallelForFatalsAtRegistration.
    auto catchAllTick = [](auto &&...) {};
    EXPECT_THROW(
        (IRSystem::createSystem<C_VelA>(
            "TestCatchAllRelationParallelFor",
            catchAllTick,
            nullptr,
            nullptr,
            IRSystem::RelationParams<C_VelB>{},
            nullptr,
            Concurrency::PARALLEL_FOR
        )),
        std::runtime_error
    );
}

// ----------------------------------------------------------------------
// IR_ASSERT_MAIN_THREAD — main-thread side
// ----------------------------------------------------------------------

TEST_F(JobManagerFixture, AssertMainThreadIsNoOpOnMainThread) {
    // The macro must not throw on the constructing thread.
    EXPECT_NO_THROW({ IR_ASSERT_MAIN_THREAD(); });
}

TEST(IRAssertMainThread, IsNoOpWithoutJobManager) {
    // `IRJob::isMainThread()` returns true when g_jobManager is
    // nullptr (pre-`World` startup, unit tests without the fixture);
    // the macro must therefore be a no-op in that state.
    ASSERT_EQ(g_jobManager, nullptr);
    EXPECT_NO_THROW({ IR_ASSERT_MAIN_THREAD(); });
}

TEST_F(JobManagerFixture, IsMainThreadReturnsFalseFromWorker) {
    // Building-block check for the IR_ASSERT_MAIN_THREAD macro: when
    // pinned to a worker, `IRJob::isMainThread()` must return false.
    // We don't trigger the macro itself from the worker because the
    // assertion throws and enkiTS' pinned-task path doesn't catch
    // worker exceptions.
    std::atomic<bool> sawMainFromWorker{true};
    IRJob::pinTo(1, [&]() { sawMainFromWorker.store(IRJob::isMainThread()); });
    EXPECT_FALSE(sawMainFromWorker.load());
}

// ----------------------------------------------------------------------
// PARALLEL_FOR dispatch integration — T-335
// ----------------------------------------------------------------------
//
// Verifies that `Concurrency::PARALLEL_FOR` actually distributes work
// across workers and visits every entity exactly once. The entity count
// (4096) is ≥ 8 × kDefaultGrainSize (512), forcing multiple chunks and
// real parallel execution with the 2-worker pool.
//
// Two complementary assertions:
//   1. A shared atomic total == kEntityCount → no entity skipped.
//   2. Per-entity atomics all == 1 → no entity double-visited.
//      This shape is TSAN-friendly: each entity's slot is written by
//      exactly one worker at a time, so TSAN cannot alias the accesses.

class ParallelDispatchFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_jobs = std::make_unique<IRJob::JobManager>(2);
    }
    void TearDown() override {
        m_jobs.reset();
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    std::unique_ptr<IRJob::JobManager> m_jobs;
};

TEST_F(ParallelDispatchFixture, AllEntitiesDispatchedParallelFor) {
    constexpr int kEntityCount = 4096;
    for (int i = 0; i < kEntityCount; ++i) {
        IREntity::createEntity(C_Counter{});
    }

    std::atomic<int> total{0};
    auto sysId = IRSystem::createSystem<C_Counter>(
        "ParallelDispatchTotal",
        [&total](C_Counter &) { total.fetch_add(1, std::memory_order_relaxed); },
        nullptr,
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::PARALLEL_FOR
    );

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(total.load(), kEntityCount);
}

TEST_F(ParallelDispatchFixture, EachEntityVisitedExactlyOnce) {
    // TSAN-friendly: each entity owns one slot in `visits`; workers
    // never write the same slot.
    constexpr int kEntityCount = 4096;
    for (int i = 0; i < kEntityCount; ++i) {
        IREntity::createEntity(C_Slot{i});
    }

    std::vector<std::atomic<int>> visits(kEntityCount);
    for (auto &v : visits) {
        v.store(0, std::memory_order_relaxed);
    }

    auto *visitsPtr = visits.data();
    auto sysId = IRSystem::createSystem<C_Slot>(
        "ParallelDispatchPerSlot",
        [visitsPtr](C_Slot &slot) { visitsPtr[slot.idx_].fetch_add(1, std::memory_order_relaxed); },
        nullptr,
        nullptr,
        {},
        nullptr,
        IRSystem::Concurrency::PARALLEL_FOR
    );

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    int misses = 0;
    for (int i = 0; i < kEntityCount; ++i) {
        if (visits[i].load(std::memory_order_relaxed) != 1) {
            ++misses;
        }
    }
    EXPECT_EQ(misses, 0) << misses << " slot(s) not visited exactly once";
}
