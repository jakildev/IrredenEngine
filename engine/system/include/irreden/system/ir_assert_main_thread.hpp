#ifndef IR_ASSERT_MAIN_THREAD_H
#define IR_ASSERT_MAIN_THREAD_H

#include <irreden/ir_job.hpp>
#include <irreden/ir_profile.hpp>

/// Asserts that the calling code is running on the main thread, i.e. on
/// the thread that constructed the active `IRJob::JobManager`. Used to
/// guard manager-entry APIs that mutate non-thread-safe singletons
/// (`g_entityManager`, `g_systemManager`, render/audio managers, sol2
/// bindings) when called from inside a `PARALLEL_FOR` system body.
///
/// T-222 Phase 2 of the multithreading epic (#226). The assertion catches
/// the "lambda body escapes into globals" case the `SystemAccess` trait
/// cannot see from the tick signature alone — a body that closes over
/// `g_entityManager` looks pure to the compile-time validator but blows
/// up at runtime when a worker calls in.
///
/// Behavior:
/// - Debug builds: calls `IRJob::isMainThread()`; on `false`, FATALs
///   via `IR_ASSERT` with the caller's file/line.
/// - Release builds (`IR_RELEASE`): no-op, same as `IR_ASSERT`.
/// - When `g_jobManager == nullptr` (unit tests, startup before
///   `World`): `isMainThread()` returns `true` as a safe default, so
///   the macro never spuriously FATALs in pre-init code.
#define IR_ASSERT_MAIN_THREAD()                                                                    \
    IR_ASSERT(                                                                                     \
        ::IRJob::isMainThread(),                                                                   \
        "IR_ASSERT_MAIN_THREAD: called from worker thread (workerId={}); this API mutates a "      \
        "non-thread-safe singleton and must run on the main thread.",                              \
        ::IRJob::workerId()                                                                        \
    )

#endif /* IR_ASSERT_MAIN_THREAD_H */
