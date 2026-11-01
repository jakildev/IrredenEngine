-- IRPerfGrid preset: the small_dense_yaw scene on a spacing-2 lattice with the
-- wave off — a static exactness fixture. Crowded sun tiles reach the overflow
-- hierarchy at oblique yaw (0.9 rad), so a byte comparison of two builds at
-- `--yaw <y> --capture-frame 60 --exit-after-frames 75 --no-overlay` covers it.
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
    spacing = 2.0,
    wave_mode = "per_cell",
    wave_amplitude = 0.0,
    zoom = 2,
}
