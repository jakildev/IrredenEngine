-- ============================================================================
-- settings.lua  –  All user-configurable values in one place
-- ============================================================================

local S = {}

S.palette_selection_enabled = false
S.start_paused = true

S.midi_device = "OP-1"
S.active_palette = Palette.SWEETIE
S.note_color_mode = NoteColorMode.PER_VOICE
S.palette_sort = PaletteSortMode.ORIGINAL
S.color_pick = ColorPickMode.MIDNIGHT_ABLAZE
-- S.manual_note_indices = {5, 6, 3, 4, 6, 1, 2, 7, 3, 2, 1, 8, 4, 7, 8, 5}
S.manual_platform_indices = nil
S.rhythm_preset = RhythmPreset.wave_1m_fast
S.rhythm_bpm = nil --Overrides rhythm bpm
S.stop_after_cycle = true

-- ── Social/export text ───────────────────────────────────────────────────────
S.description = {
    hashtags = {
        "#gamedev",
        "#polyrhythm",
        "#satisfying",
        "#asmr",
    },
}

-- ── Voice defaults ───────────────────────────────────────────────────────────
S.voice = {
    -- Note-block voxel side length (cube).
    block_size = 6,

    -- MIDI note output defaults per voice.
    midi_velocity_start = 112,
    midi_velocity_step = 3,
    midi_velocity_min = 64,
    midi_hold_sec = 0.12,
    midi_channel = 0,
}

-- ── Scene placement ──────────────────────────────────────────────────────────
S.scene = {
    center_offset = vec3.new(0.0, 0.0, 60.0),
}

-- ── Platform geometry/layout/spring ─────────────────────────────────────────
S.platform = {
    lane_spacing = 4.0,
    size = ivec3.new(8, 8, 2),
    layout = {
        mode = LayoutMode.GRID,
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

-- ── Note-block motion/placement ─────────────────────────────────────────────
S.note_block = {
    contact_depth = 0,
    travel_distance = 70.0,
}

-- ── Physics timing inputs ────────────────────────────────────────────────────
S.physics = {
    -- Fraction of the fastest voice's period spent in the air (0.0–1.0).
    -- Gravity is auto-derived so flight_time = airtime_ratio * min_period.
    -- Set to nil to use gravity_override instead.
    airtime_ratio = 0.70,
    gravity_override = nil,
}

-- ── Particles ─────────────────────────────────────────────────────────────
S.particle = {
    count = 0,
    lifetime_multiplier = 10,
    initial_speed = 300.0,
    drag_scale = vec3.new(0.5, 0.5, 0.35),
    burst_spawn_offset_z = 0,
    burst_iso_depth_behind = 0,

    -- Velocity direction ratios (fraction of initial_speed)
    xy_speed_ratio = 0.15,
    z_speed_ratio = 1.0,
    z_variance_ratio = 0.02,

    -- Per-particle drag / drift behavior
    drag_per_second = 4.5,
    drift_delay_sec = 0.8, --Extends pur-launch phase before hover kicks in
    drift_up_accel = 100.0,
    min_speed = 0.01,

    -- Hover phase (sinusoidal oscillation between ascent and exit drift)
    hover_duration_sec = 1.5,
    hover_osc_speed = 5.0,
    hover_osc_amplitude = 10.0,
    hover_blend_sec = 2.0,
    hover_blend_easing = IREasingFunction.CUBIC_EASE_OUT,

    -- Per-particle randomness (0.0 = none, 0.3 = +/-30% of base value)
    hover_start_variance = 0.2,
    hover_duration_variance = 0.3,
    hover_amplitude_variance = 0.3,
    hover_speed_variance = 0.3,

    -- Spawn glow (auto-fires on particle creation)
    glow_enabled = true,
    glow_mix_to_white = 1.0,
    glow_hold_sec = 0.10,
    glow_fade_sec = 0.20,
    glow_easing = IREasingFunction.CUBIC_EASE_OUT,
}

-- ── Visual options ──────────────────────────────────────────────────────────
S.visual = {
    platform_color_mode = PlatformColorMode.MATCH_BLOCK,
    -- platform_desat_factor = 0.45,
    -- platform_darken_factor = 0.30,

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

    platform_hit_glow_enabled = false,
    platform_hit_glow_mix_to_white = 0.50,
    platform_hit_glow_hold_sec = 0.2,
    platform_hit_glow_fade_sec = 1.00,
    platform_hit_glow_easing = IREasingFunction.SINE_EASE_OUT,
}

-- ── Platform spring physics ────────────────────────────────────────────────
-- Replaces the old animation-driven platform_anim with velocity-based spring
-- physics.  Stiffness is auto-derived in main.lua from gravity + travel
-- distance so the spring launch speed matches the block's impulse speed.
S.platform.spring = {
    length = 8.0,
    lock_ratio = 0.7,
    overshoot_ratio = 0.3,
    absorption_ratio = 1.0,
    damping = 10.0,
    max_launch_oscillations = 2,
    max_catch_oscillations = 2,
    settle_speed = 0.5,
    load_lead_sec = 0.15,
    direction = vec3.new(0.0, 0.0, 1.0),
    -- Lock path color shift (+/- hue, saturation, value, alpha).
    -- Used for rest -> locked as spring.colorProgress goes 0 -> 1.
    color_shift_hsv = ColorHSV(0.0, 0.00, 0.00, 0.0),
    -- Release path color shift (+/- hue, saturation, value, alpha).
    -- Used for locked/launch -> rest as spring.colorProgress goes 1 -> 0.
    -- Set this equal to color_shift_hsv to mirror the lock path exactly.
    release_color_shift_hsv = ColorHSV(0.0, -0.35, -0.50, 0.0),
    -- Minimum HSV value/saturation after shift (0.0–1.0). Prevents dark
    -- base colors from becoming indistinguishable from black when shifted.
    color_min_value = 0.08,
    color_min_saturation = 0.0,
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
    mode         = ScaleMode.PENTATONIC_MINOR,
    root_note    = NoteName.Gs,
    root_octave  = 2,
    start_offset = 0,
}
S.scale.intervals = IRAudio.getScaleIntervals(S.scale.mode)
S.scale.root      = IRAudio.rootNote(S.scale.root_note, S.scale.root_octave)

return S
