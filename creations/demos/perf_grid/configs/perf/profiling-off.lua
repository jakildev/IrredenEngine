-- IRPerfGrid preset: config.lua's own scene with the CPU scope profiler and the
-- GPU stage timers off. --auto-profile still turns on per-system wall timers and
-- full-frame GPU timestamps. Carries no perf_grid table, so the scene and its
-- per-cell wave are config.lua's.
config = {
    profiling_enabled = false,
    gpu_stage_timing = false,
}
