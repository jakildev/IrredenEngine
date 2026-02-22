-- ============================================================================
-- MIDI Polyrhythm Visualizer
-- ============================================================================
--
-- 7 voices on C minor pentatonic, each with a unique beat period.
-- All voices fire simultaneously on startup, then drift apart and
-- periodically realign as their timing ratios converge.
--
-- Animation: blocks climb linearly, then fall with gravity. On impact
-- at the floor, a particle burst explodes outward and the MIDI note fires.
-- A small rebound bounce follows before the next climb.
--
-- ============================================================================

IRAudio.openMidiOut("OP-1")

local tau = math.pi * 2.0
local fps = 60.0

-- ── Helpers ─────────────────────────────────────────────────────────────────
local function color_from_hsv(h, s, v)
    local r, g, b = IRMath.hsvToRgbBytes(h, s, v)
    return Color.new(r, g, b, 255)
end

-- ── Configuration ───────────────────────────────────────────────────────────

local bpm = 90
local beat_sec = 60.0 / bpm

local visual_options = {
    background_enabled = true,
    -- Drawn as a 2D trixel canvas background via C_TriangleCanvasBackground.
    background_mode = "trixel_canvas_pulse",
    background_color_a = Color.new(18, 10, 30, 255),
    background_color_b = Color.new(44, 24, 70, 255),
    background_pulse_speed = 2.2,
    background_pattern_scale = 8,

    note_hit_glow_enabled = true,
    note_hit_glow_mix_to_white = 1.0,
    note_hit_glow_hold_sec = 0.10,
    note_hit_glow_fade_sec = 0.55,
    note_hit_glow_easing = IREasingFunction.SINE_EASE_OUT
}

-- 7 voices: C minor pentatonic across octaves
-- Beat periods chosen for rich partial alignments:
--   2&3 align every 6 beats (4s)
--   3&4 every 12 beats (8s)
--   4&6 every 12 beats (8s)
--   Full alignment (LCM 2,3,4,5,6,8,12) = 120 beats (80s)
local voices = {
    { note = MidiNote.C2,  beats = 12,  hue = 0.00, sat = 0.92, val = 0.90,
      size = 6, vel = 110, hold = 0.5,  ch = 0,
      burst = 14, bLife = 55, bSpd = 14.0 },

    { note = MidiNote.Eb2, beats = 8,   hue = 0.06, sat = 0.88, val = 0.88,
      size = 5, vel = 100, hold = 0.4,  ch = 0,
      burst = 12, bLife = 48, bSpd = 13.0 },

    { note = MidiNote.F3,  beats = 6,   hue = 0.12, sat = 0.82, val = 0.88,
      size = 5, vel = 94,  hold = 0.30, ch = 0,
      burst = 10, bLife = 42, bSpd = 13.0 },

    { note = MidiNote.G3,  beats = 5,   hue = 0.18, sat = 0.78, val = 0.90,
      size = 4, vel = 90,  hold = 0.24, ch = 0,
      burst = 9,  bLife = 38, bSpd = 14.0 },

    { note = MidiNote.Bb3, beats = 4,   hue = 0.55, sat = 0.72, val = 0.92,
      size = 4, vel = 86,  hold = 0.20, ch = 0,
      burst = 8,  bLife = 35, bSpd = 15.0 },

    { note = MidiNote.C4,  beats = 3,   hue = 0.65, sat = 0.68, val = 0.95,
      size = 3, vel = 80,  hold = 0.15, ch = 0,
      burst = 7,  bLife = 30, bSpd = 16.0 },

    { note = MidiNote.Eb4, beats = 2,   hue = 0.78, sat = 0.60, val = 1.00,
      size = 3, vel = 75,  hold = 0.10, ch = 0,
      burst = 6,  bLife = 25, bSpd = 18.0 },
}

local num_voices = #voices

-- Arrange lanes adjacent along the screen bottom:
-- keep isometric depth stable by moving (+x, -x, 0).
local lane_start_x = -56.0
local lane_spacing = 18.0
local lane_y_offset = -1.0
local platform_z = 150.0
local platform_size_x = 11
local platform_size_y = 7
local platform_size_z = 2
local start_phase = tau - 0.001
local note_contact_depth = 1.25

local function lane_position(i)
    local x = lane_start_x + i * lane_spacing
    local y = x * lane_y_offset
    return x, y
end

-- ============================================================================
-- Collision-driven platforms
-- ============================================================================
IREntity.createEntityBatchNotePlatforms(
    ivec3.new(num_voices, 1, 1),

    -- Position: adjacent lanes at shared floor depth.
    function(params)
        local i = params.index.x
        local x, y = lane_position(i)
        return C_Position3D.new(vec3.new(x, y, platform_z))
    end,

    -- Platform voxel
    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        return C_VoxelSetNew.new(
            ivec3.new(platform_size_x, platform_size_y, platform_size_z),
            color_from_hsv(v.hue, 0.40, 0.20)
        )
    end,

    function(params)
        return C_ColliderIso3DAABB.new(
            vec3.new(platform_size_x * 0.5, platform_size_y * 0.5, platform_size_z * 0.5),
            vec3.new(platform_size_x * 0.5, platform_size_y * 0.5, platform_size_z * 0.5)
        )
    end,

    function(params)
        return C_CollisionLayer.new(
            IRCollisionLayer.NOTE_PLATFORM,
            IRCollisionLayer.NOTE_BLOCK,
            true
        )
    end,

    function(params)
        return C_ContactEvent.new()
    end,

    function(params)
        return C_Velocity3D.new(0.0, 0.0, 0.0)
    end,

    function(params)
        -- Generic hit response: impulse down, then damped rebound return to origin.
        return C_ReactiveReturn3D.new(
            vec3.new(0.0, 0.0, 100.0), -- collision impulse (downward in +Z)
            95.0,                     -- return spring strength
            12.0,                     -- damping per second
            6,                        -- rebounds before settle (increased to make effect obvious)
            0.015,                    -- settle distance
            0.02,                     -- settle speed
            true                      -- trigger on contact enter
        )
    end
)

-- ============================================================================
-- Collision-driven note blocks
-- ============================================================================
IREntity.createEntityBatchNoteBlocks(
    ivec3.new(num_voices, 1, 1),

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local x, y = lane_position(i)
        local travel = 26.0 + i * 2.5
        -- Add slight penetration at cycle bottom so collision-enter is robust.
        local base_z = platform_z - v.size - travel + note_contact_depth
        return C_Position3D.new(vec3.new(x, y, base_z))
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        return C_VoxelSetNew.new(ivec3.new(v.size, v.size, v.size), color_from_hsv(v.hue, v.sat, v.val))
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local period = beat_sec * v.beats
        local travel = 26.0 + i * 2.5
        local idle = C_PeriodicIdle.new(vec3.new(0.0, 0.0, travel), period, start_phase)
        idle:addStageDurationSeconds(0.0, period * 0.32, 0.0, 1.0, IREasingFunction.QUADRATIC_EASE_IN)
        idle:addStageDurationSeconds(period * 0.32, period * 0.68, 1.0, 0.0, IREasingFunction.LINEAR_INTERPOLATION)
        return idle
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local hs = v.size * 0.5
        return C_ColliderIso3DAABB.new(vec3.new(hs, hs, hs), vec3.new(hs, hs, hs))
    end,

    function(params)
        return C_CollisionLayer.new(
            IRCollisionLayer.NOTE_BLOCK,
            IRCollisionLayer.NOTE_PLATFORM,
            true
        )
    end,

    function(params)
        return C_ContactEvent.new()
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        return C_MidiNote.new(v.note, v.vel, v.ch, v.hold)
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        return C_ParticleBurst.new(v.burst, v.bLife, v.bSpd)
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local base_color = color_from_hsv(v.hue, v.sat, v.val)
        local glow_color = IRMath.lerpColor(base_color, Color.new(255, 255, 255, 255),
            visual_options.note_hit_glow_mix_to_white)

        if not visual_options.note_hit_glow_enabled then
            glow_color = base_color
        end

        return C_TriggerGlow.new(
            glow_color,
            visual_options.note_hit_glow_hold_sec,
            visual_options.note_hit_glow_fade_sec,
            visual_options.note_hit_glow_easing,
            visual_options.note_hit_glow_enabled
        )
    end
)

-- ============================================================================
-- Ambient background options
-- ============================================================================
if visual_options.background_enabled and visual_options.background_mode == "trixel_canvas_pulse" then
    IRVisual.setMainCanvasPulseBackground(
        visual_options.background_color_a,
        visual_options.background_color_b,
        visual_options.background_pulse_speed,
        visual_options.background_pattern_scale
    )
else
    IRVisual.clearMainCanvasBackground()
end
