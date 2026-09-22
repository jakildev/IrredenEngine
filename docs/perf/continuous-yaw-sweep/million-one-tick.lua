-- IRPerfGrid control preset: million-profiling-off.lua with the fixed-step loop
-- clamped to one update a rendered frame (max_update_ticks_per_frame = 1).
-- million.lua's scene with the CPU scope profiler and the
-- GPU stage timers off. --auto-profile still turns on per-system wall timers
-- and full-frame GPU timestamps. Keep the scene identical to million.lua.
config = {
    voxel_pool_edge = 128,
    profiling_enabled = false,
    gpu_stage_timing = false,
    max_update_ticks_per_frame = 1,
}

perf_grid = {
    mode = "voxel_set",
    grid_size = 100,
    spacing = 1.0,
    wave_mode = "per_cell",
    wave_amplitude = 5.0,
    wave_period_seconds = 4.0,
    zoom = 4,
    subdivision_mode = "full",
    base_subdivisions = 1,
}
