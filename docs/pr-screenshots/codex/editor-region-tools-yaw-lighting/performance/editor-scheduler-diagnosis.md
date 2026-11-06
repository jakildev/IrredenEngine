# Editor profiling comparator hang diagnosis

The 24-run Release/offscreen matrix stopped at run 7: sampled voxel lighting, yaw 67 degrees, zoom 8, GPU stage timestamps disabled, default auto worker pool. The wrapper terminated it after 90 seconds with `ALIVE-TIMEOUT`; the driver rejected the run despite the wrapper's zero exit status. The outer quiet verdict was `GUARDED`, but the matrix is **incomplete and unqualified**. The first six measurements are retained only as partial diagnostics, not an editor throughput/adoption claim.

A standalone retry of the same control timed out after 45 seconds. One native one-second stack sample shows the main thread blocked in `PROPAGATE_TRANSFORM::beginTick` → `IRJob::parallelChunks` → `parallelFor` → enki task-completion semaphore. All sampled Enki workers are waiting for new tasks. This locates the sampled stall in the current frame’s transform phase, in CPU scheduling rather than the shadow shader or final profile closure. It does not imply earlier frames had not rendered. It does not identify the underlying race or prove the problem existed in a prior binary.

During the retry, a filtered process read found exactly one IRVoxelEditor instance (PID 87358); no previous timed-out editor remained. The fleet wrapper owns termination. No scheduler or transform source was changed during this diagnosis. The current slice does not modify those files; their last shared change is commit 094ee4e0d. Timing-dependent activation by the new scene/profile is possible, so “inherited bug” remains unproven.

## Controls

All controls use Release, offscreen, yaw67/zoom8, 360 requested frames and a 45-second watchdog. The default pool is auto/P-core-capped. These bounded controls are diagnostic and were not run under a quiet benchmark lease.

| Case | Wrapper result | Wrapper exit |
|---|---|---|
| exact-timing-off | CLEAN | 0 |
| sampled-timing-off-workers0 | CLEAN | 0 |
| sampled-timing-off-workers1 | CLEAN | 0 |
| sampled-timing-on | CLEAN | 0 |

`--worker-threads 0` is the existing inline-serial mode; `1` still uses an Enki worker and main-thread task pumping. Both completing, along with sampled/timing-on and exact/timing-off completing at the same pose, supports a scheduler/timing interaction. It does not justify changing the production worker count or attributing a performance delta to lighting.

Commands and exact exits are retained in each `editor-scheduler-controls/*/result.json`; logs are adjacent. The failed standalone command/result/log and full native sample are in `editor-timeout-retry`. The original matrix contains `executed-driver.py.txt`, `cases.json`, per-run artifacts, `quiet.txt`, `outer.log` and `qualification.json`.

## Sample excerpts

```text
Call graph:
    85 Thread_11646288   DispatchQueue_1: com.apple.main-thread  (serial)
    + 85 start  (in dyld) + 6992  [0x18c12be00]
    +   85 main  (in IRVoxelEditor) + 4912  [0x102509d70]
    +     85 IREngine::gameLoop()  (in IRVoxelEditor) + 96  [0x102549210]
    +       85 IREngine::World::gameLoop(unsigned int)  (in IRVoxelEditor) + 404  [0x1028f7020]
    +         85 IREngine::runMainLoopFrame<IREngine::World::FrameStages>(IREngine::World::FrameStages&)  (in IRVoxelEditor) + 132  [0x1028f749c]
    +           85 IRSystem::SystemManager::executePipeline(IRTime::Events)  (in IRVoxelEditor) + 452  [0x102c525e0]
    +             85 IRSystem::SystemManager::executeSystem(unsigned long long)  (in IRVoxelEditor) + 112  [0x102c528c4]
    +               85 IRSystem::System<(IRSystem::SystemName)30>::beginTick()  (in IRVoxelEditor) + 2052  [0x1026eb640]
    +                 85 IRJob::parallelChunks(std::span<int const, 18446744073709551615ul>, std::vector<IRJob::RowChunk>&, std::function<void (int, int, int)> const&, IRJob::ParallelTuning const&)  (in IRVoxelEditor) + 952  [0x102c68fb4]
    +                   85 IRJob::parallelFor(int, int, int, std::function<void (int, int)> const&)  (in IRVoxelEditor) + 192  [0x102c689b8]
    +                     85 enki::TaskScheduler::WaitforTask(enki::ICompletable const*, enki::TaskPriority)  (in IRVoxelEditor) + 252  [0x102c6ce20]
    +                       85 enki::TaskScheduler::WaitForTaskCompletion(enki::ICompletable const*, unsigned int)  (in IRVoxelEditor) + 432  [0x102c6c890]
    +                         85 _dispatch_semaphore_wait_slow  (in libdispatch.dylib) + 132  [0x18c330b20]
    +                           85 _dispatch_sema4_wait  (in libdispatch.dylib) + 28  [0x18c330490]
    +                             85 semaphore_wait_trap  (in libsystem_kernel.dylib) + 8  [0x18c4a5bb0]
    85 Thread_11646304
    + 85 thread_start  (in libsystem_pthread.dylib) + 8  [0x18c4e4c1c]
    +   85 _pthread_start  (in libsystem_pthread.dylib) + 136  [0x18c4e9c58]
    +     85 std::__thread_proxy[abi:nqe210106]<std::tuple<std::unique_ptr<std::__thread_struct>, void (*)(enki::ThreadArgs const&), enki::ThreadArgs>>(void*)  (in IRVoxelEditor) + 56  [0x102c6d920]
    +       85 enki::TaskScheduler::TaskingThreadFunction(enki::ThreadArgs const&)  (in IRVoxelEditor) + 228  [0x102c6acf8]
    +         85 enki::TaskScheduler::WaitForNewTasks(unsigned int)  (in IRVoxelEditor) + 376  [0x102c6af40]
    +           85 _dispatch_semaphore_wait_slow  (in libdispatch.dylib) + 132  [0x18c330b20]
    +             85 _dispatch_sema4_wait  (in libdispatch.dylib) + 28  [0x18c330490]
    +               85 semaphore_wait_trap  (in libsystem_kernel.dylib) + 8  [0x18c4a5bb0]
```

## Disposition

No speculative engine fix or worker-count workaround was applied. Keep the CPU scheduler timeout separate from the qualified shared shadow optimization measurements. A follow-up should reproduce with a reduced scheduler workload and a same-source parent binary/control, then inspect completion notification and waiting races before changing dispatch semantics. Default exact lighting and CLI smoke results are recorded separately in `editor-cli-controls/results.json`; those are correctness controls, not performance qualification.

## Subsequent default smoke failure

The no-environment-override default exact-lighting smoke (yaw30/zoom4, stage timing ON, 120 requested frames) also hit a 60-second ALIVE-TIMEOUT. Therefore the hang is **not isolated to sampled lighting or disabled stage timing**. Its log is `editor-cli-controls/default-exact.log`; no stack was captured for this later run. The three CLI rejection controls passed with process exit 2. Two further no-env exact/stage-off controls are recorded in `editor-default-off-controls`.

## Final default controls

Both no-env default exact/stage-timing-OFF controls completed CLEAN with 360 recorded frames, 360 camera samples, zero yaw travel, constant requested zoom and no GPU stage samples: yaw30/zoom4 and yaw67/zoom8. Their exact commands, results and profile reports are under `editor-default-off-controls`. These successful runs do not erase the earlier intermittent default exact/stage-timing-ON timeout. All native runs have ended; no source or production worker configuration was changed.
