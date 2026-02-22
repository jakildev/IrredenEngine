local grid_size = 32
local grid_span = grid_size - 1
local total_voxels = grid_size * grid_size * grid_size
local base_period_seconds = 200.0
local tau = math.pi * 2.0

local function fract(x)
    return x - math.floor(x)
end

local function clamp01(x)
    return math.max(0.0, math.min(1.0, x))
end

local function hsv_to_rgb(h, s, v)
    local hh = fract(h) * 6.0
    local c = v * s
    local x = c * (1.0 - math.abs((hh % 2.0) - 1.0))
    local m = v - c

    local r = 0.0
    local g = 0.0
    local b = 0.0

    if hh < 1.0 then
        r = c
        g = x
    elseif hh < 2.0 then
        r = x
        g = c
    elseif hh < 3.0 then
        g = c
        b = x
    elseif hh < 4.0 then
        g = x
        b = c
    elseif hh < 5.0 then
        r = x
        b = c
    else
        r = c
        b = x
    end

    return r + m, g + m, b + m
end

IREntity.createEntityBatchVoxelPeriodicIdle(
    ivec3.new(grid_size, grid_size, grid_size),
    function(params)
        local index = params.index
        local base = vec3.new(index.x, index.y, index.z) - params.center

        local nx = base.x / grid_span
        local ny = base.y / grid_span
        local nz = base.z / grid_span

        local radius_xy = math.sqrt(nx * nx + ny * ny)
        local wave_xy = math.sin((nx + ny) * tau * 2.5 + nz * tau * 1.2)
        local wave_xz = math.sin((nx + nz) * tau * 2.1 - ny * tau * 1.3)
        local wave_yz = math.sin((ny - nz) * tau * 2.1 + nx * tau * 1.3)

        -- Broad, mostly uniform volume with subtle warping so shape stays readable.
        local x = nx * 100.0 + wave_yz * 18.0
        local y = ny * 100.0 + wave_xz * 18.0
        local z = nz * 100.0 + wave_xy * 30.0 - radius_xy * 20.0

        return C_Position3D.new(vec3.new(x, y, z))
    end,
    function(params)
        local index = params.index
        local nx = index.x / grid_span
        local ny = index.y / grid_span
        local nz = index.z / grid_span

        local linear_index = index.x + index.y * grid_size + index.z * grid_size * grid_size
        local ordered_t = linear_index / (total_voxels - 1)

        local hue = ordered_t
        local sat = clamp01(0.8 + nz * 0.18)
        local val = clamp01(0.65 + ny * 0.22 + (1.0 - math.abs(nx - 0.5) * 2.0) * 0.13)
        local r, g, b = hsv_to_rgb(hue, sat, val)

        return C_VoxelSetNew.new(
            ivec3.new(1, 1, 1),
            Color.new(
                math.floor(r * 255.0),
                math.floor(g * 255.0),
                math.floor(b * 255.0),
                255
            )
        )
    end,
    function(params)
        local index = params.index
        local nx = index.x / grid_span
        local ny = index.y / grid_span
        local nz = index.z / grid_span

        local cx = nx - 0.5
        local cy = ny - 0.5
        local cz = nz - 0.5
        local radial_xyz = math.sqrt(cx * cx + cy * cy + cz * cz)

        -- Structured groups: coherent waves that drift in/out of sync.
        local axis_selector = (math.floor(index.x / 4) + math.floor(index.y / 4) + math.floor(index.z / 4)) % 3
        local sync_group = (math.floor(index.x / 8) + math.floor(index.y / 8) + math.floor(index.z / 8)) % 4
        local group_period_offset = (sync_group - 1.5) * 0.35

        local amplitude = 240.0 + (1.0 - clamp01(radial_xyz * 1.4)) * 45.0
        local period = base_period_seconds + group_period_offset
        local phase =
            (math.floor(index.x / 6) * 0.18 + math.floor(index.y / 6) * 0.23 + math.floor(index.z / 6) * 0.29) * tau
        local axis_amplitude = amplitude
        local amplitude_vec = vec3.new(0.0, 0.0, 0.0)
        if axis_selector == 0 then
            amplitude_vec = vec3.new(axis_amplitude, 0.0, 0.0)
        elseif axis_selector == 1 then
            amplitude_vec = vec3.new(0.0, axis_amplitude, 0.0)
        else
            amplitude_vec = vec3.new(0.0, 0.0, axis_amplitude)
        end

        local idle_component = C_PeriodicIdle.new(amplitude_vec, period, phase)
        idle_component:addStageDurationSeconds(
            0.0,
            period * 0.25,
            -1.0,
            0.15,
            IREasingFunction.SINE_EASE_IN_OUT
        )
        idle_component:addStageDurationSeconds(
            period * 0.25,
            period * 0.25,
            0.15,
            1.0,
            IREasingFunction.SINE_EASE_IN_OUT
        )
        idle_component:addStageDurationSeconds(
            period * 0.5,
            period * 0.25,
            1.0,
            -0.2,
            IREasingFunction.SINE_EASE_IN_OUT
        )
        idle_component:addStageDurationSeconds(
            period * 0.75,
            period * 0.25,
            -0.2,
            -1.0,
            IREasingFunction.SINE_EASE_IN_OUT
        )
        return idle_component
    end
)