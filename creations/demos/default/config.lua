
-- TODO: Break up settings into groups
local config_2kfullscreen = {
    -- WINDOW AND RENDER SETTINGS
    init_window_width = 2560,
    init_window_height = 1440,
    game_resolution_width = 2560,
    game_resolution_height = 1440,
    fit_mode = "stretch", -- WIP
    fullscreen = true,
    monitor_index = -1, -- -1 means "auto/default monitor"
    monitor_name = "", -- exact monitor name match takes priority over index
    voxel_render_mode = "snapped", -- "snapped" or "smooth"
    voxel_render_subdivisions = 1, -- effective subdivisions = base * zoom

    -- VIDEO CAPTURE SETTINGS
    video_capture_output_file = "capture.mp4",
    video_capture_fps = 60,
    video_capture_bitrate = 10000000,
    video_capture_audio_input_enabled = false,
    video_capture_audio_input_device_name = "",
    video_capture_audio_sample_rate = 48000,
    video_capture_audio_channels = 2,
    screenshot_output_dir = "save_files/screenshots",
    start_updates_on_first_key_press = false,
    start_recording_on_first_key_press = false,
    profiling_enabled = true
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
    monitor_index = -1, -- -1 means "auto/default monitor"
    monitor_name = "", -- exact monitor name match takes priority over index
    voxel_render_mode = "smooth", -- "snapped" or "smooth"
    voxel_render_subdivisions = 1, -- effective subdivisions = base * zoom

    -- VIDEO CAPTURE SETTINGS
    video_capture_output_file = "capture.mp4",
    video_capture_fps = 60,
    video_capture_bitrate = 10000000,
    video_capture_audio_input_enabled = false,
    video_capture_audio_input_device_name = "",
    video_capture_audio_sample_rate = 48000,
    video_capture_audio_channels = 2,
    screenshot_output_dir = "save_files/screenshots",
    start_updates_on_first_key_press = true,
    start_recording_on_first_key_press = true,
    profiling_enabled = true
    -- END

}

local config_1080_windowed_vertical = {
    -- WINDOW AND RENDER SETTINGS
    init_window_width = 1080,
    init_window_height = 1920,
    game_resolution_width = 1080,
    game_resolution_height = 1920,
    fit_mode = "stretch", -- WIP
    fullscreen = false,
    monitor_index = -1, -- -1 means "auto/default monitor"
    monitor_name = "", -- exact monitor name match takes priority over index
    voxel_render_mode = "smooth", -- "snapped" or "smooth"
    voxel_render_subdivisions = 1, -- effective subdivisions = base * zoom

    -- VIDEO CAPTURE SETTINGS
    video_capture_output_file = "capture.mp4",
    video_capture_fps = 60,
    video_capture_bitrate = 10000000,
    video_capture_audio_input_enabled = false,
    video_capture_audio_input_device_name = "",
    video_capture_audio_sample_rate = 48000,
    video_capture_audio_channels = 2,
    screenshot_output_dir = "save_files/screenshots",
    start_updates_on_first_key_press = true,
    start_recording_on_first_key_press = true,
    profiling_enabled = false
    -- END

}

config = config_1080_windowed_vertical