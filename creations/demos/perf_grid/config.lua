config = {
    init_window_width = 1280,
    init_window_height = 720,
    game_resolution_width = 1280,
    game_resolution_height = 720,
    fit_mode = "stretch",
    fullscreen = false,
    monitor_index = -1,
    monitor_name = "",
    subdivision_mode = "full",
    voxel_render_subdivisions = 1,

    video_capture_output_file = "capture.mp4",
    video_capture_fps = 60,
    video_capture_bitrate = 10000000,
    video_capture_audio_input_enabled = false,
    video_capture_audio_input_device_name = "",
    video_capture_audio_sample_rate = 48000,
    video_capture_audio_channels = 2,
    video_capture_audio_bitrate = 320000,
    video_capture_audio_mux_enabled = true,
    video_capture_audio_wav_enabled = true,
    video_capture_audio_sync_offset_ms = 0.0,
    screenshot_output_dir = "save_files/screenshots",
    start_updates_on_first_key_press = false,
    start_recording_on_first_key_press = false,
    profiling_enabled = true,
    gpu_stage_timing = true,
    gpu_stage_timing_legacy = false,
    gui_scale = 1,
    hovered_trixel_visible = false,
}

perf_grid = {
    mode = "voxel_set",
    grid_size = 64,
    -- 1.0 = contiguous voxels (solid grid_size³ block). Spacing 2.0 turns the
    -- scene into a 1/8-density lattice (see configs/perf/sparse_lattice.lua).
    spacing = 1.0,
    -- rigid = whole block glides screen-right in phase; per_cell = legacy
    -- diagonal wave that shears the lattice open (scatter stress).
    wave_mode = "rigid",
    wave_amplitude = 6.0,
    wave_period_seconds = 4.0,
    wave_offscreen = false,
}
