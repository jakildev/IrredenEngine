-- ============================================================================
-- colors.lua  –  Palette loading, sorting, and color helpers
-- ============================================================================

local C = {}

-- ── Global enums for autocomplete ───────────────────────────────────────────

NoteColorMode = {
    PER_NOTE  = "per_note",
    PER_VOICE = "per_voice",
}

PlatformColorMode = {
    PALETTE     = "palette",
    DESATURATE  = "desaturate",
    MATCH_BLOCK = "match_block",
}

Palette = {
    SWEETIE           = "sweetie",
    ISLAND_JOY        = "island_joy",
    CHASM             = "chasm",
    PEAR36            = "pear36",
    MULFOK32          = "mulfok32",
    MIDNIGHT_ABLAZE   = "midnight_ablaze",
    OIL_6             = "oil_6",
    TWILIGHT_5        = "twilight_5",
    TWO_BIT_DEMICHROME = "two_bit_demichrome",
    AMMO_8            = "ammo_8",
    BERRY_NEBULA      = "berry_nebula",
    BLESSING          = "blessing",
    BORKFEST          = "borkfest",
    CL8UDS            = "cl8uds",
    CRYPTIC_OCEAN     = "cryptic_ocean",
    CURIOSITIES       = "curiosities",
    DREAMSCAPE8       = "dreamscape8",
    EPHEMERA          = "ephemera",
    EULBINK           = "eulbink",
    FAIRYDUST_8       = "fairydust_8",
    HOLLOW            = "hollow",
    HOPE_DIAMOND      = "hope_diamond",
    ICE_CREAM_GB      = "ice_cream_gb",
    INK               = "ink",
    INKPINK           = "inkpink",
    LATE_NIGHT_BATH   = "late_night_bath",
    LAVA_GB           = "lava_gb",
    NYX8              = "nyx8",
    PAPER_8           = "paper_8",
    PASTEL_QT         = "pastel_qt",
    PICO_8            = "pico_8",
    POLLEN8           = "pollen8",
    RUST_GOLD_8       = "rust_gold_8",
    SLSO8             = "slso8",
}

PaletteSortMode = {
    HUE        = "hue",
    SATURATION = "saturation",
    VALUE      = "value",
    LUMINANCE  = "luminance",
    ORIGINAL   = "original",
}

ColorPickMode = {
    SPLIT_HALF     = "split_half",
    EVENLY_SPACED  = "evenly_spaced",
    EVERY_OTHER    = "every_other",
    FIRST_N        = "first_n",
    RANDOM         = "random",
    MANUAL         = "manual",
}

-- ── Palette assets ──────────────────────────────────────────────────────────

C.palettes = {
    sweetie            = IRMath.loadPalette("data/palettes/sweetie-16-1x.png"),
    island_joy         = IRMath.loadPalette("data/palettes/island-joy-16-1x.png"),
    chasm              = IRMath.loadPalette("data/palettes/chasm-1x.png"),
    pear36             = IRMath.loadPalette("data/palettes/pear36-1x.png"),
    mulfok32           = IRMath.loadPalette("data/palettes/mulfok32-1x.png"),
    midnight_ablaze    = IRMath.loadPalette("data/palettes/midnight-ablaze-1x.png"),
    oil_6              = IRMath.loadPalette("data/palettes/oil-6-1x.png"),
    twilight_5         = IRMath.loadPalette("data/palettes/twilight-5-1x.png"),
    two_bit_demichrome = IRMath.loadPalette("data/palettes/2bit-demichrome-1x.png"),
    ammo_8             = IRMath.loadPalette("data/palettes/ammo-8-1x.png"),
    berry_nebula       = IRMath.loadPalette("data/palettes/berry-nebula-1x.png"),
    blessing           = IRMath.loadPalette("data/palettes/blessing-1x.png"),
    borkfest           = IRMath.loadPalette("data/palettes/borkfest-1x.png"),
    cl8uds             = IRMath.loadPalette("data/palettes/cl8uds-1x.png"),
    cryptic_ocean      = IRMath.loadPalette("data/palettes/cryptic-ocean-1x.png"),
    curiosities        = IRMath.loadPalette("data/palettes/curiosities-1x.png"),
    dreamscape8        = IRMath.loadPalette("data/palettes/dreamscape8-1x.png"),
    ephemera           = IRMath.loadPalette("data/palettes/ephemera-1x.png"),
    eulbink            = IRMath.loadPalette("data/palettes/eulbink-1x.png"),
    fairydust_8        = IRMath.loadPalette("data/palettes/fairydust-8-1x.png"),
    hollow             = IRMath.loadPalette("data/palettes/hollow-1x.png"),
    hope_diamond       = IRMath.loadPalette("data/palettes/hope-diamond-1x.png"),
    ice_cream_gb       = IRMath.loadPalette("data/palettes/ice-cream-gb-1x.png"),
    ink                = IRMath.loadPalette("data/palettes/ink-1x.png"),
    inkpink            = IRMath.loadPalette("data/palettes/inkpink-1x.png"),
    late_night_bath    = IRMath.loadPalette("data/palettes/late-night-bath-1x.png"),
    lava_gb            = IRMath.loadPalette("data/palettes/lava-gb-1x.png"),
    nyx8               = IRMath.loadPalette("data/palettes/nyx8-1x.png"),
    paper_8            = IRMath.loadPalette("data/palettes/paper-8-1x.png"),
    pastel_qt          = IRMath.loadPalette("data/palettes/pastel-qt-1x.png"),
    pico_8             = IRMath.loadPalette("data/palettes/pico-8-1x.png"),
    pollen8            = IRMath.loadPalette("data/palettes/pollen8-1x.png"),
    rust_gold_8        = IRMath.loadPalette("data/palettes/rust-gold-8-1x.png"),
    slso8              = IRMath.loadPalette("data/palettes/slso8-1x.png"),
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

-- ── Sort ────────────────────────────────────────────────────────────────────

local sort_functions = {
    [PaletteSortMode.HUE]        = IRMath.sortByHue,
    [PaletteSortMode.SATURATION] = IRMath.sortBySaturation,
    [PaletteSortMode.VALUE]      = IRMath.sortByValue,
    [PaletteSortMode.LUMINANCE]  = IRMath.sortByLuminance,
}

local function sort_palette(raw, mode)
    if mode == PaletteSortMode.ORIGINAL then
        local copy = {}
        for i = 1, #raw do copy[i] = raw[i] end
        return copy
    end
    local fn = sort_functions[mode] or IRMath.sortBySaturation
    return fn(raw)
end

-- ── Pick helpers ────────────────────────────────────────────────────────────

local function pick_evenly(pool, count)
    local result = {}
    local n = #pool
    if n == 0 then return result end
    for i = 1, count do
        local idx = math.floor((i - 1) * n / count) + 1
        result[i] = pool[idx]
    end
    return result
end

local function pick_every_other(pool, count, offset)
    local result = {}
    local n = #pool
    if n == 0 then return result end
    local j = 0
    for i = offset, n, 2 do
        j = j + 1
        result[j] = pool[i]
        if j >= count then break end
    end
    while j < count do
        j = j + 1
        result[j] = pool[((j - 1) % n) + 1]
    end
    return result
end

local function pick_first_n(pool, count)
    local result = {}
    local n = #pool
    for i = 1, count do
        result[i] = pool[((i - 1) % n) + 1]
    end
    return result
end

local function pick_random(pool, count, seed)
    local rng_state = seed or 42
    local function next_rng()
        rng_state = (rng_state * 1103515245 + 12345) % 2147483648
        return rng_state
    end
    local shuffled = {}
    for i = 1, #pool do shuffled[i] = pool[i] end
    for i = #shuffled, 2, -1 do
        local j = (next_rng() % i) + 1
        shuffled[i], shuffled[j] = shuffled[j], shuffled[i]
    end
    return pick_evenly(shuffled, count)
end

local function pick_manual(pool, indices, count)
    local result = {}
    local n = #pool
    if not indices then return pick_evenly(pool, count) end
    for i = 1, count do
        local idx = indices[i]
        if idx and idx >= 1 and idx <= n then
            result[i] = pool[idx]
        else
            result[i] = pool[((i - 1) % n) + 1]
        end
    end
    return result
end

-- ── Build palette config ────────────────────────────────────────────────────

function C.build_palette_config(sorted, color_count, pick_mode, manual_note_idx, manual_plat_idx, random_seed_note, random_seed_platform)
    local n = #sorted

    if pick_mode == ColorPickMode.SPLIT_HALF then
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
            note     = pick_evenly(note_pool, color_count),
            platform = pick_evenly(platform_pool, color_count),
        }

    elseif pick_mode == ColorPickMode.EVERY_OTHER then
        return {
            note     = pick_every_other(sorted, color_count, 1),
            platform = pick_every_other(sorted, color_count, 2),
        }

    elseif pick_mode == ColorPickMode.FIRST_N then
        return {
            note     = pick_first_n(sorted, color_count),
            platform = nil,
        }

    elseif pick_mode == ColorPickMode.RANDOM then
        return {
            note     = pick_random(sorted, color_count, random_seed_note or 42),
            platform = pick_random(sorted, color_count, random_seed_platform or 137),
        }

    elseif pick_mode == ColorPickMode.MANUAL then
        return {
            note     = pick_manual(sorted, manual_note_idx, color_count),
            platform = pick_manual(sorted, manual_plat_idx, color_count),
        }

    else -- EVENLY_SPACED (default)
        return {
            note     = pick_evenly(sorted, color_count),
            platform = nil,
        }
    end
end

-- ── High-level build ────────────────────────────────────────────────────────

function C.build(settings, num_voices)
    local pal = settings.palette or {}
    local raw = C.palettes[pal.active] or C.palettes.mulfok32
    local vis = settings.visual
    local note_mode = pal.note_color_mode or NoteColorMode.PER_NOTE
    local sort_mode = pal.sort_mode or PaletteSortMode.SATURATION
    local pick_cfg = pal.pick or {}
    local pick_mode = pick_cfg.mode or ColorPickMode.SPLIT_HALF
    local manual = pick_cfg[ColorPickMode.MANUAL] or {}
    local random_cfg = pick_cfg[ColorPickMode.RANDOM] or {}
    local manual_note = manual.note_indices
    local manual_platform = manual.platform_indices
    local seed_note = random_cfg.seed_note
    local seed_platform = random_cfg.seed_platform

    local color_count = (note_mode == NoteColorMode.PER_VOICE) and num_voices or pitch_classes
    local sorted = sort_palette(raw, sort_mode)
    local cfg = C.build_palette_config(
        sorted, color_count, pick_mode,
        manual_note,
        manual_platform,
        seed_note,
        seed_platform
    )

    local pc = vis.platform_color or {}
    local pc_mode = pc.mode or PlatformColorMode.MATCH_BLOCK
    local desat_cfg = pc[PlatformColorMode.DESATURATE] or {}
    local desat_factor = desat_cfg.desat_factor or 0.45
    local darken_factor = desat_cfg.darken_factor or 0.30

    local function platform_color_for(note_color, idx)
        if pc_mode == PlatformColorMode.MATCH_BLOCK then
            return note_color
        end
        if pc_mode == PlatformColorMode.DESATURATE then
            return C.desaturate(note_color, desat_factor, darken_factor)
        end
        if cfg.platform and cfg.platform[idx] then
            return cfg.platform[idx]
        end
        return C.complementary(note_color)
    end

    return {
        note_colors        = cfg.note,
        platform_colors    = cfg.platform,
        pitch_classes      = pitch_classes,
        note_color_mode    = note_mode,
        platform_color_for = platform_color_for,
    }
end

return C
