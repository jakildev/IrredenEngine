
-- TODO: Break up settings into groups
local config_2kfullscreen = {
    -- WINDOW AND RENDER SETTINGS
    init_window_width = 2560,
    init_window_height = 1440,
    game_resolution_width = 2560,
    game_resolution_height = 1440,
    fit_mode = "stretch", -- WIP
    fullscreen = true,
    voxel_render_mode = "snapped", -- "snapped" or "smooth"
    voxel_render_subdivisions = 1, -- effective subdivisions = base * zoom

    -- VIDEO CAPTURE SETTINGS
    video_capture_output_file = "capture.mp4",
    video_capture_fps = 60,
    video_capture_bitrate = 10000000,
    screenshot_output_dir = "save_files/screenshots"
    -- END

}
local config_1080_windowed = {
    -- WINDOW AND RENDER SETTINGS
    init_window_width = 1920,
    init_window_height = 1080,
    game_resolution_width = 1920,
    game_resolution_height = 1080,
    fit_mode = "stretch", -- WIP
    fullscreen = false,
    voxel_render_mode = "smooth", -- "snapped" or "smooth"
    voxel_render_subdivisions = 1, -- effective subdivisions = base * zoom

    -- VIDEO CAPTURE SETTINGS
    video_capture_output_file = "capture.mp4",
    video_capture_fps = 60,
    video_capture_bitrate = 10000000,
    screenshot_output_dir = "save_files/screenshots"
    -- END

}

config = config_1080_windowed