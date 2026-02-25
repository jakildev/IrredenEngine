-- ============================================================================
-- colors.lua  –  Palette loading, sorting, and color helpers
-- ============================================================================

local C = {}

-- ── Palette assets ──────────────────────────────────────────────────────────

C.palettes = {
    sweetie    = IRMath.loadPalette("data/palettes/sweetie-16-1x.png"),
    island_joy = IRMath.loadPalette("data/palettes/island-joy-16-1x.png"),
    chasm      = IRMath.loadPalette("data/palettes/chasm-1x.png"),
    pear36     = IRMath.loadPalette("data/palettes/pear36-1x.png"),
    mulfok32   = IRMath.loadPalette("data/palettes/mulfok32-1x.png"),
}

-- ── Helpers ─────────────────────────────────────────────────────────────────

function C.complementary(c)
    return Color.new(255 - c.r, 255 - c.g, 255 - c.b, c.a)
end

function C.desaturate(c, desat, darken)
    local hsv = IRMath.colorToHSV(c)
    return IRMath.hsvToColor(ColorHSV(
        hsv.h,
        hsv.s * (1.0 - desat),
        hsv.v * (1.0 - darken),
        hsv.a
    ))
end

local function pick_evenly(pool, count)
    local result = {}
    local n = #pool
    for i = 1, count do
        local idx = math.floor((i - 1) * n / count) + 1
        result[i] = pool[idx]
    end
    return result
end

-- ── Build palette config ────────────────────────────────────────────────────
-- Sorts the raw palette by saturation; most-saturated half → note colors,
-- least-saturated half → palette-mode platform colors.

function C.build_palette_config(raw_palette, pitch_classes)
    local sorted = IRMath.sortBySaturation(raw_palette)
    local n = #sorted
    local mid = math.floor(n / 2)

    local note_pool = {}
    for i = mid + 1, n do
        note_pool[#note_pool + 1] = sorted[i]
    end

    local platform_pool = {}
    for i = 1, mid do
        platform_pool[#platform_pool + 1] = sorted[i]
    end

    return {
        note     = pick_evenly(note_pool, pitch_classes),
        platform = pick_evenly(platform_pool, pitch_classes),
    }
end

-- ── High-level build ────────────────────────────────────────────────────────
-- Returns { note_colors, platform_colors, pitch_classes, platform_color_for }

function C.build(settings)
    local pitch_classes = #settings.scale.intervals
    local raw = C.palettes[settings.active_palette_name] or C.palettes.mulfok32
    local cfg = C.build_palette_config(raw, pitch_classes)
    local vis = settings.visual

    local function platform_color_for(note_color, pc)
        if vis.platform_color_mode == "desaturate" then
            return C.desaturate(note_color, vis.platform_desat_factor, vis.platform_darken_factor)
        end
        if cfg.platform and cfg.platform[pc] then
            return cfg.platform[pc]
        end
        return C.complementary(note_color)
    end

    return {
        note_colors     = cfg.note,
        platform_colors = cfg.platform,
        pitch_classes   = pitch_classes,
        platform_color_for = platform_color_for,
    }
end

return C
