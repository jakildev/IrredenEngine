
-- TODO: Break up settings into groups
local config_2kfullscreen = {
    -- WINDOW AND RENDER SETTINGS
    init_window_width = 2560,
    init_window_height = 1440,
    game_resolution_width = 2560,
    game_resolution_height = 1440,
    fit_mode = "stretch", -- WIP
    fullscreen = true
    -- END

}
local config_1080_windowed = {
    -- WINDOW AND RENDER SETTINGS
    init_window_width = 1920,
    init_window_height = 1080,
    game_resolution_width = 1920,
    game_resolution_height = 1080,
    fit_mode = "stretch", -- WIP
    fullscreen = false
    -- END

}

config = config_1080_windowed