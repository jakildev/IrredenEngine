-- Input latency positive control: sdf-off.lua's timing configuration plus a
-- 20 ms sleep between each frame's last OS-event poll and its RENDER. The
-- reported input_to_present_ms mean must rise by that sleep against
-- sdf-off.lua on the same scene.
config = {
    profiling_enabled = false,
    gpu_stage_timing = false,
    gpu_stage_timing_legacy = false,
    input_latency_test_delay_ms = 20.0,
}
