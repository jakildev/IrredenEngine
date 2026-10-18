-- The render fixture corrects this starting window to a fixed 1280x960
-- framebuffer: each gui_scale-1 texel covers 2x1 output pixels.
config = {
    init_window_width = 640,
    init_window_height = 480,
    game_resolution_width = 1280,
    game_resolution_height = 960,
    gui_scale = 1,
}
