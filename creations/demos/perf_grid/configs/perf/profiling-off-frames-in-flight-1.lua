-- IRPerfGrid preset: profiling-off.lua with the OpenGL queue bounded to 1
-- presented frame in flight (the shipped default), one arm of the
-- frame-overlap measurement (docs/perf/input-latency/). Carries no perf_grid
-- table, so the scene is config.lua's.
config = {
    profiling_enabled = false,
    gpu_stage_timing = false,
    max_frames_in_flight = 1,
}
