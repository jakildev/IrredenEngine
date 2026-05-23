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

    screenshot_output_dir = "save_files/screenshots",
    start_updates_on_first_key_press = false,
    profiling_enabled = false,
    gpu_stage_timing = false,
    gui_scale = 2,
    hovered_trixel_visible = false,
}

-- canvas_stress exercises the detached-canvas voxel path: many entities,
-- each owning its own per-entity canvas + voxel pool, composited over a
-- main-canvas GRID grid. See creations/demos/canvas_stress/main.cpp.
canvas_stress = {
    main_grid_size = 5,
    detached_count = 9,
    initial_zoom = 0.4,
    auto_rotate = false,
}
