-- Unit creation for the movement demo

local U = {}

function U.create_units(settings)
    local cfg = settings.units
    local grid = settings.grid
    local nav = settings.nav
    local cell_size = grid.cell_size
    local half_x = math.floor(grid.size_x / 2)
    local half_y = math.floor(grid.size_y / 2)

    -- Spawn in the center area (inside the fortress)
    local spawn_cell_x = half_x
    local spawn_cell_y = half_y
    local world_cx = (spawn_cell_x - half_x) * cell_size
    local world_cy = (spawn_cell_y - half_y) * cell_size

    local player_radius_world = nav.voxel_size_world * nav.player_radius_voxels
    local moving_collision_radius =
        player_radius_world * (nav.moving_collision_radius_factor or 0.3)
    local preferred_moving_radius =
        player_radius_world * (nav.preferred_moving_radius_factor or 0.65)
    local r = math.max(1, nav.player_radius_voxels)
    local d = 2 * r
    local unit_voxel_size = ivec3.new(d, d, r)

    local half = vec3.new(0.5, 0.5, 0.5)
    local layer = IRCollisionLayer.DEFAULT

    -- Spread units in a grid formation
    local cols = math.ceil(math.sqrt(cfg.count))
    local spacing = player_radius_world * 3.0

    local entities = IREntity.createEntityBatchUnits(
        ivec3.new(cfg.count, 1, 1),
        function(params)
            local i = params.index.x
            local col_idx = i % cols
            local row_idx = math.floor(i / cols)
            local x = world_cx + (col_idx - cols / 2) * spacing
            local y = world_cy + (row_idx - cols / 2) * spacing
            local z = -math.ceil(r / 2) - 1.0
            return C_Position3D.new(vec3.new(x, y, z))
        end,
        function() return C_ControllableUnit.new() end,
        function() return C_NavAgent.new(player_radius_world) end,
        function() return C_ColliderIso3DAABB.new(half, half) end,
        function()
            return C_ColliderCircle.new(
                player_radius_world,
                moving_collision_radius,
                preferred_moving_radius
            )
        end,
        function() return C_Facing2D.new(6.0) end,
        function() return C_SmoothMovement.new() end,
        function()
            return C_CollisionLayer.new(layer, layer, true)
        end,
        function(params)
            local i = params.index.x
            local col = (i == 0) and cfg.player_color or cfg.unit_color
            return C_VoxelSetNew.new(unit_voxel_size, col, true)
        end
    )

    print("[Units] Created " .. #entities .. " controllable units in grid formation")
end

return U
