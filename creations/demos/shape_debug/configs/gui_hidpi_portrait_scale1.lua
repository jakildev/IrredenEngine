-- IRShapeDebug --gui-test preset: a portrait game resolution twice the window's
-- size in points, which a 2x display backs with a framebuffer of exactly the
-- game resolution. On a 1x display the window is smaller than the game
-- resolution and the frame is cropped, so the on-frame assertions only hold on
-- a HiDPI host.
config = {
    init_window_width = 540,
    init_window_height = 960,
    game_resolution_width = 1080,
    game_resolution_height = 1920,
    gui_scale = 1,
}
