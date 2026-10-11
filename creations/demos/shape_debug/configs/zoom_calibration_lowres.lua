-- IRShapeDebug continuous-zoom preset: a game resolution half the window's in
-- each dimension, so one framebuffer pixel is at least two screen pixels and
-- the framebuffer-to-screen residual has sub-pixel steps to take. The
-- calibration sweeps log the output scale each capture ran at.
config = {
    init_window_width = 1280,
    init_window_height = 720,
    game_resolution_width = 640,
    game_resolution_height = 360,
}
