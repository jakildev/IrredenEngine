local grid_size_x = 128
local grid_size_y = 128
local grid_span_x = grid_size_x - 1
local grid_span_y = grid_size_y - 1
local loop_seconds = 10.0 * 60.0
local world_spacing = 1.0
local base_wave_amplitude = 110.0
local base_period_seconds = 6.0
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
    ivec3.new(grid_size_x, grid_size_y, 1),
    function(params)
        local index = params.index
        local cx = (grid_size_x - 1) * 0.5
        local cy = (grid_size_y - 1) * 0.5

        -- Centered grid with integer world coordinates and one empty voxel gap.
        local x = (index.x - cx) * world_spacing
        local y = (index.y - cy) * world_spacing
        return C_Position3D.new(vec3.new(x, y, 0.0))
    end,
    function(params)
        local index = params.index
        local nx = index.x / grid_span_x
        local ny = index.y / grid_span_y

        -- True continuous 2D rainbow field (no row/column quantization).
        local hue = fract(nx * 0.68 + ny * 0.32)
        local sat = 0.9
        local val = 0.88
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
        local x_norm_centered = ((index.x / grid_span_x) - 0.5) * 2.0
        local y_norm_centered = ((index.y / grid_span_y) - 0.5) * 2.0
        local radial_xy = math.sqrt(x_norm_centered * x_norm_centered + y_norm_centered * y_norm_centered)

        -- Start all voxels from a flat baseline (z = 0) and then animate outward.
        local phase = 0.0

        -- Keep this fully continuous for smoother spatial evolution (no integer-cycle banding).
        local raw_period =
            base_period_seconds
            + math.sin(x_norm_centered * tau * 0.22 + y_norm_centered * tau * 0.27) * 0.22
        local period = math.max(0.001, raw_period)

        local center_boost = (1.0 - clamp01(radial_xy * 0.72)) * 18.0
        local amplitude = base_wave_amplitude + center_boost
        local amplitude_vec = vec3.new(0.0, 0.0, amplitude)

        local idle_component = C_PeriodicIdle.new(amplitude_vec, period, phase)
        idle_component:addStageDurationSeconds(
            0.0,
            period * 0.5,
            -1.0,
            1.0,
            IREasingFunction.SINE_EASE_IN_OUT
        )
        idle_component:addStageDurationSeconds(
            period * 0.5,
            period * 0.5,
            1.0,
            -1.0,
            IREasingFunction.SINE_EASE_IN_OUT
        )
        return idle_component
    end
)