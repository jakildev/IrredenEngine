-- Unit Movement Demo settings
-- Large-world showcase: 128x128 nav grid with obstacles, many units

local S = {}

S.nav = {
    voxel_size_world = 1.0,
    voxels_per_nav_cell = 3,
    player_radius_voxels = 7,
    chunk_size = 32,
    moving_collision_radius_factor = 0.3,
    preferred_moving_radius_factor = 0.65,
}

local cell_size = S.nav.voxel_size_world * S.nav.voxels_per_nav_cell

S.grid = {
    size_x = 128,
    size_y = 128,
    cell_size = cell_size,
    chunk_size = S.nav.chunk_size,
}

S.units = {
    count = 12,
    unit_color = Color.new(100, 180, 255, 255),
    player_color = Color.new(255, 200, 100, 255),
    selected_color = Color.new(255, 255, 150, 255),
}

return S
