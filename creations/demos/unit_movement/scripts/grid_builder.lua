-- Grid builder: creates level entity and nav cells
-- 128x128 world with perimeter walls, obstacle clusters, and narrow passages

local G = {}

function G.create_level(settings)
    local grid = settings.grid
    local nav = settings.nav
    local level_entity = IREntity.createLevelEntityFromVoxelConfig(
        nav.voxel_size_world,
        nav.voxels_per_nav_cell,
        nav.player_radius_voxels,
        "level"
    )
    IREntity.setNavGridOrigin(level_entity, grid.size_x, grid.size_y, nil, grid.chunk_size)
    print("[GridBuilder] Created level entity (cell=" .. grid.cell_size .. "wu, " ..
          nav.voxels_per_nav_cell .. " voxels/cell, chunk=" .. grid.chunk_size .. ")")
end

local function is_perimeter(x, y, sx, sy)
    return x == 0 or x == sx - 1 or y == 0 or y == sy - 1
end

-- Rectangular obstacle: returns true if (x,y) is inside
local function in_rect(x, y, rx, ry, rw, rh)
    return x >= rx and x < rx + rw and y >= ry and y < ry + rh
end

-- Circular obstacle: returns true if (x,y) is within radius of center
local function in_circle(x, y, cx, cy, r)
    local dx = x - cx
    local dy = y - cy
    return (dx * dx + dy * dy) <= r * r
end

local function build_obstacle_map(sx, sy)
    local blocked = {}
    for x = 0, sx - 1 do
        blocked[x] = {}
        for y = 0, sy - 1 do
            blocked[x][y] = false
        end
    end

    -- Perimeter walls
    for x = 0, sx - 1 do
        for y = 0, sy - 1 do
            if is_perimeter(x, y, sx, sy) then
                blocked[x][y] = true
            end
        end
    end

    -- Central fortress walls (open on 4 sides with wide gates)
    local cx, cy = math.floor(sx / 2), math.floor(sy / 2)
    for x = cx - 12, cx + 12 do
        for y = cy - 12, cy + 12 do
            if x >= 0 and x < sx and y >= 0 and y < sy then
                local edge = (x == cx - 12 or x == cx + 12 or y == cy - 12 or y == cy + 12)
                -- Gates: 9-wide openings so large units can fit through
                local gate_x = (x >= cx - 4 and x <= cx + 4)
                local gate_y = (y >= cy - 4 and y <= cy + 4)
                if edge then
                    if (x == cx - 12 or x == cx + 12) and gate_y then
                        -- east/west gate
                    elseif (y == cy - 12 or y == cy + 12) and gate_x then
                        -- north/south gate
                    else
                        blocked[x][y] = true
                    end
                end
            end
        end
    end

    -- Forest clusters (compact solid obstacles, units must route around)
    local forests = {
        {20, 20, 4},
        {100, 25, 5},
        {25, 100, 4},
        {105, 105, 4},
        {50, 20, 3},
        {20, 50, 3},
        {110, 60, 5},
        {60, 110, 4},
    }
    for _, f in ipairs(forests) do
        local fcx, fcy, fr = f[1], f[2], f[3]
        for x = math.max(1, fcx - fr), math.min(sx - 2, fcx + fr) do
            for y = math.max(1, fcy - fr), math.min(sy - 2, fcy + fr) do
                if in_circle(x, y, fcx, fcy, fr) then
                    blocked[x][y] = true
                end
            end
        end
    end

    -- Horizontal wall segments with wide gaps (units need ~7-cell openings)
    local h_walls = {
        {15, 35, 30},
        {80, 35, 35},
        {15, 90, 30},
        {80, 90, 35},
    }
    for _, w in ipairs(h_walls) do
        local wx, wy, wlen = w[1], w[2], w[3]
        for dx = 0, wlen - 1 do
            local x = wx + dx
            if x >= 1 and x < sx - 1 then
                -- 9-cell gap every 18 cells for passage
                local phase = dx % 18
                if phase < 4 or phase > 12 then
                    blocked[x][wy] = true
                end
            end
        end
    end

    -- Vertical wall segments with wide gaps
    local v_walls = {
        {35, 15, 30},
        {35, 80, 35},
        {90, 15, 30},
        {90, 80, 35},
    }
    for _, w in ipairs(v_walls) do
        local wx, wy, wlen = w[1], w[2], w[3]
        for dy = 0, wlen - 1 do
            local y = wy + dy
            if y >= 1 and y < sy - 1 then
                local phase = dy % 18
                if phase < 4 or phase > 12 then
                    blocked[wx][y] = true
                end
            end
        end
    end

    return blocked
end

function G.create_flat_grid(size_x, size_y, cell_size)
    local ground_color = Color.new(60, 95, 60, 255)
    local wall_color = Color.new(80, 65, 50, 255)
    local forest_color = Color.new(35, 70, 35, 255)
    local voxel_size = ivec3.new(
        math.max(1, math.floor(cell_size)),
        math.max(1, math.floor(cell_size)),
        1
    )

    local blocked = build_obstacle_map(size_x, size_y)

    local entities = IREntity.createEntityBatchNavCells(
        ivec3.new(size_x, size_y, 1),
        function(params)
            local idx = params.index
            local passable = not blocked[idx.x][idx.y]
            return C_NavCell.new(ivec3.new(idx.x, idx.y, 0), passable)
        end,
        function(params)
            local idx = params.index
            local half_x = math.floor(size_x / 2)
            local half_y = math.floor(size_y / 2)
            local x = (idx.x - half_x) * cell_size
            local y = (idx.y - half_y) * cell_size
            return C_Position3D.new(vec3.new(x, y, 0))
        end,
        function(params)
            local idx = params.index
            local is_blocked = blocked[idx.x][idx.y]
            local col
            if is_blocked then
                if is_perimeter(idx.x, idx.y, size_x, size_y) then
                    col = wall_color
                else
                    col = forest_color
                end
            else
                col = ground_color
            end
            return C_VoxelSetNew.new(voxel_size, col)
        end
    )

    print("[GridBuilder] Created " .. #entities .. " nav cells (" ..
          size_x .. "x" .. size_y .. " grid)")
end

return G
