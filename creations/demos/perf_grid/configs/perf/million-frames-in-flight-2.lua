-- IRPerfGrid preset: million-profiling-off.lua's scene with the OpenGL queue
-- bounded to 2 presented frames in flight, one arm of the frame-overlap
-- measurement (docs/perf/input-latency/). Keep the scene identical to
-- million-profiling-off.lua.
config = {
    voxel_pool_edge = 128,
    profiling_enabled = false,
    gpu_stage_timing = false,
    max_frames_in_flight = 2,
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
