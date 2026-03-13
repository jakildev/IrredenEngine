-- Unit creation for the movement demo

local U = {}

function U.create_units(settings, grid_builder)
    local cfg = settings.units
    local grid = settings.grid
    local nav = settings.nav
    local cell_size = grid.cell_size

    local player_radius_world = nav.voxel_size_world * nav.player_radius_voxels
    local moving_collision_radius =
        player_radius_world * (nav.moving_collision_radius_factor or 0.3)
    local preferred_moving_radius =
        player_radius_world * (nav.preferred_moving_radius_factor or 0.65)
    local r = math.max(1, nav.player_radius_voxels)
    local visual_r = math.max(1, nav.visual_radius_voxels or nav.player_radius_voxels)
    local visual_d = 2 * visual_r
    local unit_voxel_size = ivec3.new(visual_d, visual_d, visual_r)

    local half = vec3.new(0.5, 0.5, 0.5)
    local layer = IRCollisionLayer.DEFAULT

    -- Spawn positions: center-biased, away from walls, and non-overlapping.
    local planning_clearance =
        player_radius_world * (nav.planning_clearance_multiplier or 1.0)
    local min_spacing = player_radius_world * 2.0
    local spawn_positions = grid_builder.get_valid_spawn_positions(
        grid.size_x, grid.size_y, cell_size, cfg.count, min_spacing, planning_clearance
    )
    local actual_count = math.min(cfg.count, #spawn_positions)
    if actual_count < cfg.count then
        print("[Units] WARNING: Only " .. #spawn_positions .. " valid spawn positions, creating " ..
              actual_count .. " units (requested " .. cfg.count .. ")")
    end

    local entities = IREntity.createEntityBatchUnits(
        ivec3.new(actual_count, 1, 1),
        function(params)
            local i = params.index.x
            local p = spawn_positions[i + 1] or spawn_positions[1]
            local z = -math.ceil(visual_r / 2) - 1.0
            return C_Position3D.new(vec3.new(p.x, p.y, z))
        end,
        function() return C_ControllableUnit.new() end,
        function()
            return C_NavAgent.new(
                player_radius_world,
                nav.planning_clearance_multiplier or 1.0
            )
        end,
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
