-- `--glitch-test` budget preset: GPU stage timing on, drawn to a 1x output
-- target. A pass that draws the screen target shades viewport x outputScale^2
-- pixels, and its budget is stated at one output pixel per game pixel. On a 2x
-- HiDPI display a window half the 1280x720 game resolution in points is backed
-- by a 1280x720 framebuffer, so the output scale is 1; on a 1x display the
-- window is smaller than the game resolution and the frame is cropped.
config = {
    profiling_enabled = true,
    gpu_stage_timing = true,
    gpu_stage_timing_legacy = false,
    init_window_width = 640,
    init_window_height = 360,
}
