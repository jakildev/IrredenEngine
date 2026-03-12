-- ============================================================================
-- particle_launch.lua  –  Layout-correlated particle launch behavior
-- ============================================================================
--
-- Defines per-layout particle launch config (direction, spawn bias, etc.).
-- Call get_config(i, num_voices, platform, scene, voices_mod) and merge
-- the returned overrides into C_ParticleBurst.
--
-- ============================================================================

local P = {}

-- ── Config generators per layout ─────────────────────────────────────────────
-- Each returns a table of C_ParticleBurst overrides, or nil for default.

local function normalize_xy(x, y)
    local len = math.sqrt(x * x + y * y)
    if len < 0.0001 then return 0, 0 end
    return x / len, y / len
end

--- Circle: particles launch toward center. Spawn bias toward outer edge.
function P.circle_toward_center(i, num_voices, platform, scene, voices_mod, voices)
    local layout = platform.layout
    local cfg = layout[LayoutMode.CIRCLE] or {}
    local off = scene.center_offset
    local p = voices_mod.layout_position(i, num_voices, platform)
    local psz = platform.size
    local block_size = (voices and voices[i + 1] and voices[i + 1].size) or 3
    local center_x = off.x
    local center_y = off.y
    local block_x = off.x + p.x + (psz.x - block_size) / 2.0
    local block_y = off.y + p.y + (psz.y - block_size) / 2.0
    local dx = center_x - block_x
    local dy = center_y - block_y
    local ndx, ndy = normalize_xy(dx, dy)
    if ndx == 0 and ndy == 0 then return nil end
    -- Upward component: 0 = horizontal only, 1 = straight up, 0.5 = ~45° diagonal up+center
    local up = cfg.particle_launch_upward or 0.5
    local dx2, dy2, dz2 = ndx * (1 - up), ndy * (1 - up), -up
    local len = math.sqrt(dx2 * dx2 + dy2 * dy2 + dz2 * dz2)
    if len < 0.0001 then dx2, dy2, dz2 = 0, 0, -1 else dx2, dy2, dz2 = dx2 / len, dy2 / len, dz2 / len end

    local out = {
        useDirectionOverride = true,
        directionOverride = vec3.new(dx2, dy2, dz2),
        directionStrength = (cfg.particle_direction_strength or 0.9),
        directionScatter = (cfg.particle_direction_scatter or 0.15),
        useFaceSpawnBias = cfg.particle_spawn_from_outer ~= false,
        faceSpawnBias = vec3.new(-ndx, -ndy, 0),  -- spawn from outer edge (away from center)
    }
    -- Gravity-only (no hover): set in cfg.particle_gravity_only = true
    if cfg.particle_gravity_only then
        out.pHoverDurationSec = 0
        out.pHoverOscAmplitude = 0
    end
    return out
end

--- Grid / default: no overrides (random launch).
function P.default_random(i, num_voices, platform, scene, voices_mod, voices)
    return nil
end

-- ── Registry: layout mode -> config generator ─────────────────────────────────
P.by_layout = {
    [LayoutMode.CIRCLE] = P.circle_toward_center,
    [LayoutMode.GRID] = P.default_random,
    [LayoutMode.ZIGZAG] = P.default_random,
    [LayoutMode.ZIGZAG_PATH] = P.default_random,
    [LayoutMode.SQUARE_SPIRAL] = P.default_random,
    [LayoutMode.HELIX] = P.default_random,
    [LayoutMode.PATH_DOUBLE_C] = P.default_random,
}

--- Get particle launch overrides for block i. Merge these into C_ParticleBurst.
--- Returns table of field overrides, or empty table.
function P.get_config(i, num_voices, platform, scene, voices_mod, voices)
    local mode = (platform.layout or {}).mode or LayoutMode.GRID
    local fn = P.by_layout[mode] or P.default_random
    local overrides = fn(i, num_voices, platform, scene, voices_mod, voices)
    return overrides or {}
end

--- Apply overrides to a C_ParticleBurst instance. Modifies b in place.
function P.apply(burst, overrides)
    if not overrides or not burst then return end
    for k, v in pairs(overrides) do
        burst[k] = v
    end
end

return P
