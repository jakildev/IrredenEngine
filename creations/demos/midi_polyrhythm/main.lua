-- ============================================================================
-- MIDI Polyrhythm Visualizer  –  main entry point
-- ============================================================================
--
-- All configurable values live in settings.lua.
-- Logic is split across: rhythm.lua, colors.lua, voices.lua, entities.lua
--
-- ============================================================================

local SCRIPT_DIR = "IRMidiPolyrhythm/scripts/"
local function load_module(name)
    return dofile(SCRIPT_DIR .. name .. ".lua")
end

-- ── Load modules ────────────────────────────────────────────────────────────
local rhythm   = load_module("rhythm")   -- exports RhythmPreset global
local colors   = load_module("colors")   -- exports NoteColorMode, PlatformColorMode globals
local voices   = load_module("voices")   -- exports LayoutMode global
local settings = load_module("settings")
local entities = load_module("entities")

-- ── Main initialization (called after palette is resolved) ──────────────────

local function init_polyrhythm(selected_palette)
    print("[Init] init_polyrhythm START, preset=" .. tostring(settings.rhythm_preset))
    if selected_palette then
        settings.active_palette = selected_palette
    end

    -- ── MIDI output ─────────────────────────────────────────────────────────
    IRAudio.openMidiOut(settings.midi_device)

    -- ── Rhythm preset ───────────────────────────────────────────────────────
    print("[Init] rhythm preset: " .. tostring(settings.rhythm_preset))
    rhythm.print_all(settings.rhythm_preset)
    local preset = rhythm.select(settings.rhythm_preset, settings.rhythm_bpm)
    print("[Init] preset loaded, num_voices=" .. tostring(preset.num_voices))

    -- ── Palette & colors ────────────────────────────────────────────────────
    local palette = colors.build(settings, preset.num_voices)
    print("[Init] palette built")

    -- ── Voice creation ──────────────────────────────────────────────────────
    local voice_list, num_voices = voices.build(settings, preset, palette)
    print("[Init] voices built: " .. tostring(num_voices))

    -- ── Physics ─────────────────────────────────────────────────────────────
    local GRAVITY_MAGNITUDE
    local physics = settings.physics
    local note_block = settings.note_block
    local ps = settings.platform.spring

    if physics.gravity_override then
        GRAVITY_MAGNITUDE = physics.gravity_override
    else
        local min_period = math.huge
        for _, v in ipairs(voice_list) do
            if v.period_sec < min_period then min_period = v.period_sec end
        end
        local h = note_block.travel_distance
        local r = physics.airtime_ratio or 0.80

        local target_flight = r * min_period
        GRAVITY_MAGNITUDE = 8.0 * h / (target_flight * target_flight)
        local flight_sec = 2.0 * math.sqrt(2.0 * h / GRAVITY_MAGNITUDE)
        print(string.format(
            "[Physics] auto-gravity: g=%.2f  flight=%.2fs (%.0f%% of min period %.2fs)",
            GRAVITY_MAGNITUDE, flight_sec, r * 100, min_period))
    end
    IRPhysics.setGravityMagnitude(GRAVITY_MAGNITUDE)

    -- ── Auto-derive spring stiffness ────────────────────────────────────────
    if ps then
        local impulse_speed = IRPhysics.impulseForHeight(GRAVITY_MAGNITUDE, note_block.travel_distance)
        local auto_stiffness = (impulse_speed * impulse_speed) / (ps.length * ps.length)
        ps.auto_stiffness = auto_stiffness
        print(string.format(
            "[Spring] auto-stiffness: k=%.2f  impulse=%.2f  length=%.2f",
            auto_stiffness, impulse_speed, ps.length))
    end

    -- ── Entity creation ─────────────────────────────────────────────────────
    print("[Init] creating platforms...")
    entities.init(voices)
    entities.create_platforms(settings, voice_list, num_voices)
    print("[Init] creating note blocks...")
    entities.create_note_blocks(settings, voice_list, num_voices, GRAVITY_MAGNITUDE)
    print("[Init] setup background...")
    entities.setup_background(settings)
    print("[Init] entity creation done")

    -- ── Camera ──────────────────────────────────────────────────────────────
    IRRender.setCameraPosition2DIso(0, 0)
    IRRender.setCameraZoom(settings.visual.camera_start_zoom)

    return preset, voice_list, num_voices
end

-- ── Intro overlay ─────────────────────────────────────────────────────

local preset, voice_list, num_voices

local NOTE_NAMES = {
    [0] = "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B",
}

local SCALE_NAMES = {
    [ScaleMode.MAJOR]                  = "Major",
    [ScaleMode.DORIAN]                 = "Dorian",
    [ScaleMode.PHRYGIAN]               = "Phrygian",
    [ScaleMode.LYDIAN]                 = "Lydian",
    [ScaleMode.MIXOLYDIAN]             = "Mixolydian",
    [ScaleMode.MINOR]                  = "Minor",
    [ScaleMode.LOCRIAN]                = "Locrian",
    [ScaleMode.HARMONIC_MINOR]         = "Harmonic Minor",
    [ScaleMode.MELODIC_MINOR]          = "Melodic Minor",
    [ScaleMode.HUNGARIAN_MINOR]        = "Hungarian Minor",
    [ScaleMode.DOUBLE_HARMONIC]        = "Double Harmonic",
    [ScaleMode.NEAPOLITAN_MINOR]       = "Neapolitan Minor",
    [ScaleMode.NEAPOLITAN_MAJOR]       = "Neapolitan Major",
    [ScaleMode.ENIGMATIC]              = "Enigmatic",
    [ScaleMode.PERSIAN]                = "Persian",
    [ScaleMode.PENTATONIC_MAJOR]       = "Pentatonic Major",
    [ScaleMode.PENTATONIC_MINOR]       = "Pentatonic Minor",
    [ScaleMode.HIRAJOSHI]              = "Hirajoshi",
    [ScaleMode.IN_SEN]                 = "In Sen",
    [ScaleMode.IWATO]                  = "Iwato",
    [ScaleMode.PELOG]                  = "Pelog",
    [ScaleMode.WHOLE_TONE]             = "Whole Tone",
    [ScaleMode.BLUES]                  = "Blues",
    [ScaleMode.AUGMENTED]              = "Augmented",
    [ScaleMode.PROMETHEUS]             = "Prometheus",
    [ScaleMode.TRITONE]                = "Tritone",
    [ScaleMode.DIMINISHED_WHOLE_HALF]  = "Dim Whole-Half",
    [ScaleMode.DIMINISHED_HALF_WHOLE]  = "Dim Half-Whole",
    [ScaleMode.BEBOP_DOMINANT]         = "Bebop Dominant",
    [ScaleMode.BEBOP_MAJOR]            = "Bebop Major",
    [ScaleMode.CHROMATIC]              = "Chromatic",
}

local function format_key(s)
    return s:gsub("_", " "):lower():gsub("(%a)([%w]*)",
        function(f, r) return f:upper() .. r end)
end

local function build_overlay_text()
    local root = NOTE_NAMES[settings.scale.root_note] or "?"
    local oct  = settings.scale.root_octave
    local mode = SCALE_NAMES[settings.scale.mode] or "?"
    local scale_line = root .. oct .. " " .. mode

    local timing_parts = {}
    if preset.bpm then
        timing_parts[#timing_parts + 1] = preset.bpm .. " BPM"
    end
    if preset.align_sec >= 60 then
        local mins = preset.align_sec / 60
        if mins == math.floor(mins) then
            timing_parts[#timing_parts + 1] = string.format("%dm cycle", mins)
        else
            timing_parts[#timing_parts + 1] = string.format("%.1fm cycle", mins)
        end
    else
        timing_parts[#timing_parts + 1] = string.format("%.0fs cycle", preset.align_sec)
    end

    local pattern_line
    if preset.cycles then
        local parts = {}
        for _, c in ipairs(preset.cycles) do parts[#parts + 1] = tostring(c) end
        pattern_line = table.concat(parts, ":")
    else
        local lo = preset.notes_per_align[1]
        local hi = preset.notes_per_align[#preset.notes_per_align]
        pattern_line = lo .. "-" .. hi .. " notes/cycle"
    end

    local lines = {
        scale_line,
        preset.name .. "  |  " .. num_voices .. " voices",
        table.concat(timing_parts, "  |  "),
        pattern_line,
        format_key(settings.active_palette),
    }
    return table.concat(lines, "\n")
end

local function build_description_text()
    local root = NOTE_NAMES[settings.scale.root_note] or "?"
    local oct  = settings.scale.root_octave or "?"
    local mode = SCALE_NAMES[settings.scale.mode] or "?"
    local scale_line = root .. oct .. " " .. mode

    local cycle_text
    if preset.align_sec >= 60 then
        cycle_text = string.format("%.1fm cycle", preset.align_sec / 60.0)
    else
        cycle_text = string.format("%.0fs cycle", preset.align_sec)
    end

    local bpm_text = preset.bpm and (tostring(preset.bpm) .. " BPM") or "Variable BPM"

    local pattern_text
    if preset.cycles then
        local parts = {}
        for _, c in ipairs(preset.cycles) do parts[#parts + 1] = tostring(c) end
        pattern_text = table.concat(parts, ":")
    else
        local lo = preset.notes_per_align[1]
        local hi = preset.notes_per_align[#preset.notes_per_align]
        pattern_text = lo .. "-" .. hi .. " notes/cycle"
    end

    local desc = string.format(
        "SYSTEM LOG // IRMidiPolyrhythm\n" ..
        "MODE: %s\n" ..
        "PRESET: %s | %d voices\n" ..
        "TIMING: %s | %s\n" ..
        "PATTERN: %s\n" ..
        "PALETTE: %s",
        scale_line,
        preset.name,
        num_voices,
        bpm_text,
        cycle_text,
        pattern_text,
        format_key(settings.active_palette)
    )

    local hashtags = ""
    if settings.description and settings.description.hashtags and #settings.description.hashtags > 0 then
        hashtags = table.concat(settings.description.hashtags, " ")
    end

    return desc, hashtags
end

-- ── Post-init: overlay text, pause, etc. ─────────────────────────────────────

local function post_init()
    if settings.start_paused then
        -- Use the proper pause mechanism that works with P key toggle
        IREntity.forEachComponent(
            function(idle) 
                idle.paused_ = true 
            end,
            C_PeriodicIdle
        )
    end

    IRText.create(build_overlay_text(), 20, 90, {
        color = {255, 255, 255, 255},
        wrapWidth = -1,
        lifetime = 480,
    })

    local description_text, hashtags_text = build_description_text()
    print("----- COPY DESCRIPTION START -----")
    print(description_text)
    if hashtags_text ~= "" then
        print("")
        print(hashtags_text)
    end
    print("----- COPY DESCRIPTION END -----")
end

-- ── Palette selection or direct init ────────────────────────────────────────

if settings.palette_selection_enabled then
    local palette_selector = load_module("palette_selector")
    palette_selector.show(colors.palettes, function(selected_key)
        print("[Palette] Selected: " .. selected_key)
        preset, voice_list, num_voices = init_polyrhythm(selected_key)
        post_init()
    end)
else
    preset, voice_list, num_voices = init_polyrhythm(nil)
    post_init()
end

