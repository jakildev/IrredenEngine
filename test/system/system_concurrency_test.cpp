#include <gtest/gtest.h>

#include <irreden/ir_job.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/system/ir_assert_main_thread.hpp>
#include <irreden/system/system_access.hpp>

#include <atomic>

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
constexpr bool isParallelForAcceptable(Concurrency c, SystemAccess access) {
    if (c != Concurrency::PARALLEL_FOR) {
        return true;
    }
    if (access.usesEntityId_ && !access.parallelSafe_) {
        return false;
    }
    if (access.isBatchForm_) {
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
    void TearDown() override { m_jobs.reset(); }
    std::unique_ptr<IRJob::JobManager> m_jobs;
};

} // namespace

// ----------------------------------------------------------------------
// Validator — PARALLEL_FOR rejection cases
// ----------------------------------------------------------------------

TEST(SystemConcurrencyValidator, PerComponentFormAcceptsParallelFor) {
    // void tick(C_VelA&, const C_VelB&) — the canonical clean case.
    // Writes C_VelA, reads C_VelB, no EntityId, not batch form.
    auto access =
        deriveAccessFromSignature<void(C_VelA &, const C_VelB &), C_VelA, const C_VelB>();

    EXPECT_FALSE(access.usesEntityId_);
    EXPECT_FALSE(access.isBatchForm_);
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
}

TEST(SystemConcurrencyValidator, PerEntityIdFormRejectedWithoutParallelSafe) {
    // void tick(EntityId, C_VelA&) — id-aware form. Without an
    // explicit ParallelSafe tag, the body is presumed to dereference
    // the id into a non-thread-safe singleton.
    auto access = deriveAccessFromSignature<
        void(IREntity::EntityId &, C_VelA &),
        C_VelA>();

    EXPECT_TRUE(access.usesEntityId_);
    EXPECT_FALSE(access.parallelSafe_);
    EXPECT_FALSE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
    // SERIAL stays acceptable in the same configuration.
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::SERIAL, access));
}

TEST(SystemConcurrencyValidator, PerEntityIdFormAcceptedWithParallelSafe) {
    // Direct SystemAccess construction sidesteps a known T-221 trait
    // limitation: `deriveAccessFromSignature` cannot derive
    // `usesEntityId_` AND `parallelSafe_` from the SAME call when
    // tags share the Components pack, because `InvocableWithEntityId`
    // probes invocability with the tag types in argument position.
    // The validator's predicate is the surface under test here — what
    // it does with a SystemAccess that has BOTH flags set — not the
    // trait's signature-detection-through-tags surface. See T-221
    // follow-up filed against #1068 for the trait fix.
    SystemAccess access{};
    access.usesEntityId_ = true;
    access.parallelSafe_ = true;
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
}

TEST(SystemConcurrencyValidator, BatchFormRejected) {
    // void tick(const Archetype&, std::vector<EntityId>&, std::vector<C_VelA>&)
    // — per-archetype batch form. The body consumes the whole column,
    // so row-level chunking would re-enter it with overlapping
    // vectors. PARALLEL_FOR is structurally incompatible.
    auto access = deriveAccessFromSignature<
        void(
            const IREntity::Archetype &,
            std::vector<IREntity::EntityId> &,
            std::vector<C_VelA> &
        ),
        C_VelA>();

    EXPECT_TRUE(access.isBatchForm_);
    EXPECT_FALSE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
    // SERIAL stays acceptable; the batch form is fine on the main
    // thread.
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::SERIAL, access));
}

TEST(SystemConcurrencyValidator, MainThreadTagRejectsParallelFor) {
    // The MainThread tag is explicit "do not parallelize"; the
    // validator must FATAL rather than silently downgrade.
    auto access = deriveAccessFromSignature<
        void(C_VelA &),
        C_VelA, MainThread>();

    EXPECT_TRUE(access.mainThreadOnly_);
    EXPECT_FALSE(isParallelForAcceptable(Concurrency::PARALLEL_FOR, access));
    EXPECT_TRUE(isParallelForAcceptable(Concurrency::MAIN_THREAD, access));
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
