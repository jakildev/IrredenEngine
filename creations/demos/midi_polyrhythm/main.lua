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
local rhythm   = load_module("rhythm")   -- first: exports RhythmPreset global
local settings = load_module("settings")
local colors   = load_module("colors")
local voices   = load_module("voices")
local entities = load_module("entities")

-- ── MIDI output ─────────────────────────────────────────────────────────────
IRAudio.openMidiOut(settings.midi_device)

-- ── Rhythm preset ───────────────────────────────────────────────────────────
rhythm.print_all(settings.tuning.rhythm.preset)
local preset = rhythm.select(settings.tuning.rhythm.preset, settings.tuning.rhythm.bpm)

-- ── Palette & colors ────────────────────────────────────────────────────────
local palette = colors.build(settings)

-- ── Voice creation ──────────────────────────────────────────────────────────
local voice_list, num_voices = voices.build(settings, preset, palette)

-- ── Physics ─────────────────────────────────────────────────────────────────
local GRAVITY_MAGNITUDE
local tuning = settings.tuning

if tuning.gravity_override then
    GRAVITY_MAGNITUDE = tuning.gravity_override
else
    local min_period = math.huge
    for _, v in ipairs(voice_list) do
        if v.period_sec < min_period then min_period = v.period_sec end
    end
    local h = tuning.note_travel_distance
    local r = tuning.airtime_ratio or 0.80

    local pa = tuning.platform_anim
    if pa and pa.enabled then
        local land_dur = 0.0
        if pa.land then
            for _, phase in ipairs(pa.land) do land_dur = land_dur + phase.dur end
        end
        local launch_to_action = 0.0
        if pa.launch then
            for i, phase in ipairs(pa.launch) do
                launch_to_action = launch_to_action + phase.dur
                if pa.launch.action_phase and i > pa.launch.action_phase then break end
            end
        end
        local min_ground_needed = land_dur + launch_to_action
        local max_ratio = 1.0 - min_ground_needed / min_period
        if r > max_ratio then
            print(string.format(
                "[Physics] airtime_ratio %.2f clamped to %.2f (animations need %.3fs ground, min period %.2fs)",
                r, max_ratio, min_ground_needed, min_period))
            r = max_ratio
        end
    end

    local target_flight = r * min_period
    GRAVITY_MAGNITUDE = 8.0 * h / (target_flight * target_flight)
    local flight_sec = 2.0 * math.sqrt(2.0 * h / GRAVITY_MAGNITUDE)
    print(string.format(
        "[Physics] auto-gravity: g=%.2f  flight=%.2fs (%.0f%% of min period %.2fs)",
        GRAVITY_MAGNITUDE, flight_sec, r * 100, min_period))
end
IRPhysics.setGravityMagnitude(GRAVITY_MAGNITUDE)

-- ── Entity creation ─────────────────────────────────────────────────────────
entities.init(voices)
entities.create_platforms(settings, voice_list, num_voices)
entities.create_note_blocks(settings, voice_list, num_voices, GRAVITY_MAGNITUDE)
entities.setup_background(settings)

-- ── Camera ──────────────────────────────────────────────────────────────────
IRRender.setCameraZoom(settings.visual.camera_start_zoom)
