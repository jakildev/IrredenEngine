-- IRPerfGrid preset: 100³ single-voxel entities in a 128³ pool, FULL
-- subdivision at base 1, per-cell wave, zoom 4, with the CPU scope profiler
-- and the GPU stage timers on. config.voxel_pool_edge is consumed by the
-- engine's pre-init pass, before the render manager exists. Window size,
-- resolution and wave_offscreen come from config.lua. million-profiling-off.lua
-- is this file with the two profiling keys false; keep the scenes identical.
config = {
    voxel_pool_edge = 128,
    profiling_enabled = true,
    gpu_stage_timing = true,
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
