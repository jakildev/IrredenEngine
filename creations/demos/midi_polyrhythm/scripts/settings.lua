-- ============================================================================
-- settings.lua  –  All user-configurable values in one place
-- ============================================================================

local S = {}

S.midi_device = "OP-1"
S.active_palette_name = "chasm"

-- ── Tuning ──────────────────────────────────────────────────────────────────
S.tuning = {
    lane_spacing = 0.0,
    center_offset = vec3.new(0.0, 0.0, 60.0),
    platform_size = ivec3.new(8, 8, 2),
    start_phase = 0.0,
    note_contact_depth = 0.25,
    note_travel_distance = 80.0,
    -- Fraction of the fastest voice's period spent in the air (0.0–1.0).
    -- Gravity is auto-derived so flight_time = airtime_ratio * min_period.
    -- Set to nil to use gravity_override instead.
    airtime_ratio = 0.80,
    gravity_override = nil,
    motion_up_duration_sec = 0.5,
    motion_down_duration_sec = 0.5,
    motion_bottom_rest_level = 1.0,
    burst_spawn_offset_z = 0,
    burst_iso_depth_behind = 0,
    rhythm = {
        preset = RhythmPreset.dense_twelve,
        bpm = nil,  -- set a number to override the preset's default BPM
    },
    layout = {
        mode = "zigzag_path",
        plane = 0,
        depth = 0.0,
        grid_columns = 4,
        zigzag_items_per_row = 4,
        zigzag_path_segment = 1,
        spiral_spacing = 8.0,
        helix_radius = 14.0,
        helix_turns = 1.5,
        helix_height_span = 18.0,
        helix_axis = 2,
    },
}

-- ── Particles ─────────────────────────────────────────────────────────────
S.particle = {
    count = 20,
    lifetime_multiplier = 10,
    initial_speed = 400.0,
    drag_scale = vec3.new(1.0, 1.0, 0.85),

    -- Velocity direction ratios (fraction of initial_speed)
    xy_speed_ratio = 0.15,
    z_speed_ratio = 1.0,
    z_variance_ratio = 0.06,

    -- Per-particle drag / drift behavior
    drag_per_second = 4.5,
    drift_delay_sec = 0.5,
    drift_up_accel = 100.0,
    min_speed = 0.01,

    -- Hover phase (sinusoidal oscillation between ascent and exit drift)
    hover_duration_sec = 1.5,
    hover_osc_speed = 5.0,
    hover_osc_amplitude = 10.0,
    hover_blend_sec = 1.2,
    hover_blend_easing = IREasingFunction.QUADRATIC_EASE_OUT,

    -- Per-particle randomness (0.0 = none, 0.3 = +/-30% of base value)
    hover_start_variance = 0.3,
    hover_duration_variance = 0.3,
    hover_amplitude_variance = 0.3,
    hover_speed_variance = 0.3,

    -- Spawn glow (auto-fires on particle creation)
    glow_enabled = true,
    glow_mix_to_white = 1.0,
    glow_hold_sec = 0.03,
    glow_fade_sec = 0.22,
    glow_easing = IREasingFunction.CUBIC_EASE_OUT,
}

-- ── Visual options ──────────────────────────────────────────────────────────
S.visual = {
    -- "palette" = least-saturated palette half; "desaturate" = derive from block color
    platform_color_mode = "desaturate",
    platform_desat_factor = 0.45,
    platform_darken_factor = 0.30,

    platform_mute_color = Color.new(20, 20, 25, 255),
    platform_mute_amount = 0.60,

    background_enabled = false,
    background_mode = "trixel_canvas_pulse",
    background_color_a = Color.new(15, 15, 15, 255),
    background_color_b = Color.new(2, 2, 2, 255),
    background_pulse_speed = 0.5,
    background_pattern_scale = 4,
    background_start_zoom_multiplier = 4.0,
    background_wave_dir = vec2.new(1.0, 1.0),
    background_wave_phase_scale = 10.0,
    background_wave_speed_multiplier = 1.0,
    background_wave_start_offset = 0.0,
    background_wave_direction_motion_enabled = false,
    background_wave_direction_motion_start = vec2.new(1.0, 1.0),
    background_wave_direction_motion_end = vec2.new(-1.0, 1.0),
    background_wave_direction_motion_period_sec = 100.0,
    background_wave_direction_motion_ease_forward = IREasingFunction.SINE_EASE_IN_OUT,
    background_wave_direction_motion_ease_backward = IREasingFunction.SINE_EASE_IN_OUT,
    background_wave2_dir = vec2.new(-1.0, 1.0),
    background_wave2_phase_scale = 7.0,
    background_wave2_speed_multiplier = 1.25,
    background_wave2_start_offset = 1.2,
    background_wave2_direction_motion_enabled = true,
    background_wave2_direction_motion_start = vec2.new(-1.2, 1.0),
    background_wave2_direction_motion_end = vec2.new(-0.8, 1.0),
    background_wave2_direction_motion_period_sec = 41.0,
    background_wave2_direction_motion_ease_forward = IREasingFunction.SINE_EASE_IN_OUT,
    background_wave2_direction_motion_ease_backward = IREasingFunction.SINE_EASE_IN_OUT,
    background_wave_interference_mix = 0.2,

    camera_start_zoom = 4.0,

    note_hit_glow_enabled = true,
    note_hit_glow_mix_to_white = 1.0,
    note_hit_glow_hold_sec = 0.10,
    note_hit_glow_fade_sec = 0.55,
    note_hit_glow_easing = IREasingFunction.SINE_EASE_OUT,

    platform_hit_glow_enabled = true,
    platform_hit_glow_mix_to_white = 0.50,
    platform_hit_glow_hold_sec = 0.2,
    platform_hit_glow_fade_sec = 1.00,
    platform_hit_glow_easing = IREasingFunction.SINE_EASE_OUT,
}

-- ── Platform animation ─────────────────────────────────────────────────────
S.tuning.platform_anim = {
    enabled = true,
    direction = vec3.new(0.0, 0.0, 1.0),
    start_displacement = 0.0,

    launch = {
        { dur = 0.15, from = 3.0,  to = 5.0,  ease = "QUADRATIC_EASE_IN" },
        { dur = 0.08, from = 5.0,  to = -1.5, ease = "QUADRATIC_EASE_OUT" },
        { dur = 0.12, from = -1.5, to = 0.0,  ease = "SINE_EASE_OUT" },
        action_phase = 1,
    },
    land = {
        { dur = 0.06, from = 0.0,  to = 4.0,  ease = "QUADRATIC_EASE_OUT" },
        { dur = 0.15, from = 4.0,  to = 3.0,  ease = "SINE_EASE_OUT" },
    },

    color_enabled = true,
    color_mode = "HSV_OFFSET",
    -- HSV offset applied when platform is in the "down" (landed) position.
    -- launch idle = no offset (original color), land idle = darkened.
    idle_value_offset = -0.3,
    motion_shift = {
        enabled = false,
    },
}

-- ── Scale ───────────────────────────────────────────────────────────────────
-- Scale intervals and root notes are now defined in the engine (IRAudio).
--
-- Available modes (ScaleMode.*):
--   Diatonic:   MAJOR/IONIAN, DORIAN, PHRYGIAN, LYDIAN, MIXOLYDIAN,
--               MINOR/AEOLIAN, LOCRIAN
--   Variants:   HARMONIC_MINOR, MELODIC_MINOR, HUNGARIAN_MINOR,
--               DOUBLE_HARMONIC, NEAPOLITAN_MINOR, NEAPOLITAN_MAJOR,
--               ENIGMATIC, PERSIAN
--   Pentatonic: PENTATONIC_MAJOR, PENTATONIC_MINOR, HIRAJOSHI, IN_SEN,
--               IWATO, PELOG
--   Hexatonic:  WHOLE_TONE, BLUES, AUGMENTED, PROMETHEUS, TRITONE
--   Octatonic:  DIMINISHED_WHOLE_HALF, DIMINISHED_HALF_WHOLE,
--               BEBOP_DOMINANT, BEBOP_MAJOR
--   Other:      CHROMATIC
--
-- Available root notes (NoteName.*):
--   C, Cs/Db, D, Ds/Eb, E, F, Fs/Gb, G, Gs/Ab, A, As/Bb, B
--
-- Usage:
--   intervals = IRAudio.getScaleIntervals(ScaleMode.DORIAN)
--   root      = IRAudio.rootNote(NoteName.D, 3)    -- D3 MIDI note number

S.scale = {
    intervals    = IRAudio.getScaleIntervals(ScaleMode.PENTATONIC_MINOR),
    root         = IRAudio.rootNote(NoteName.Gs, 3),
    start_offset = 0,
}

return S
