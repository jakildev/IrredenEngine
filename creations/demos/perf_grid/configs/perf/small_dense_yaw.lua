-- IRPerfGrid preset: small dense scene under continuous yaw — a 24³ solid
-- block (13,824 voxels) on a 540×960 portrait canvas at zoom 2 with the
-- per-cell wave. Small enough that the rotating frame's cost is the lighting
-- of per-axis and overflow faces (finite source-face sun query, overflow
-- relight), not raster fan-out. Yaw comes from the CLI:
--   rotating: --yaw 0.0017453293 --yaw-step 0.0017453293
--   cardinal: --yaw 0
-- both with --auto-profile 900 --no-overlay.
config = {
    init_window_width = 540,
    init_window_height = 960,
    game_resolution_width = 540,
    game_resolution_height = 960,
    profiling_enabled = true,
    gpu_stage_timing = true,
}

perf_grid = {
    mode = "voxel_set",
    grid_size = 24,
    spacing = 1.0,
    wave_mode = "per_cell",
    wave_amplitude = 5.0,
    zoom = 2,
}
