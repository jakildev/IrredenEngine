-- A 2x macOS framebuffer produces the fixed 1280x960 output-scale-1 text
-- fixture: each gui_scale-1 texel covers 2x1 output pixels.
config = {
    init_window_width = 640,
    init_window_height = 480,
    game_resolution_width = 1280,
    game_resolution_height = 960,
    gui_scale = 1,
}
