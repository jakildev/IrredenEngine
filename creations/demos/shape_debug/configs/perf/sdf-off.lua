-- Auto-profile retains frame/system timers, full-frame GPU timestamps and the
-- Debug CPU histogram. This disables easy_profiler and GPU stage timestamps.
config = {
    profiling_enabled = false,
    gpu_stage_timing = false,
    gpu_stage_timing_legacy = false,
}
