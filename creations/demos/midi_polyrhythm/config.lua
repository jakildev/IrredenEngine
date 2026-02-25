config = {
    init_window_width = 1080,
    init_window_height = 1920,
    game_resolution_width = 1080,
    game_resolution_height = 1920,
    fit_mode = "stretch",
    fullscreen = false,
    monitor_index = 1, -- -1 means "auto/default monitor"
    monitor_name = "", -- exact monitor name match takes priority over index
    voxel_render_mode = "smooth",
    voxel_render_subdivisions = 1,

    video_capture_output_file = "polyrhythm_capture.mp4",
    video_capture_fps = 60,
    video_capture_bitrate = 10000000,
    video_capture_audio_input_enabled = true,
    video_capture_audio_input_device_name = "IN 01-02 (BEHRINGER UMC 1820)",
    video_capture_audio_sample_rate = 48000,
    video_capture_audio_channels = 2,
    screenshot_output_dir = "save_files/screenshots",
    start_updates_on_first_key_press = false,
    start_recording_on_first_key_press = false,
    profiling_enabled = false
}
