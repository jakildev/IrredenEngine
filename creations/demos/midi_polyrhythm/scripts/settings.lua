-- ============================================================================
-- settings.lua  –  All user-configurable values in one place
-- ============================================================================

local S = {}

S.palette_selection_enabled = true
S.start_paused = false

S.midi_device = "OP-1"
-- Palette/color selection (mode + configs per mode)
S.palette = {
    active = Palette.BERRY_NEBULA,
    note_color_mode = NoteColorMode.PER_VOICE,
    sort_mode = PaletteSortMode.ORIGINAL,
    pick = {
        mode = ColorPickMode.FIRST_N,
        [ColorPickMode.MANUAL] = {
            note_indices = {1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6, 4, 5, 6, 7},
            platform_indices = nil,
        },
        [ColorPickMode.RANDOM] = { seed_note = 42, seed_platform = 137 },
    },
}
S.rhythm_preset = RhythmPreset.wave_3m_slow
S.rhythm_bpm = 60 --Overrides rhythm bpm
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
    block_size = 8,

    -- MIDI note output defaults per voice.
    midi_velocity_start = 112,
    midi_velocity_step = 3,
    midi_velocity_min = 64,
    midi_hold_sec = 0.12,
    midi_channel = 0,
}

-- ── Scene placement ──────────────────────────────────────────────────────────
S.scene = {
    center_offset = vec3.new(0.0, 0.0, 10.0),
}

-- ── Platform geometry/layout/spring ─────────────────────────────────────────
S.platform = {
    lane_spacing = 8.0,
    size = ivec3.new(8, 8, 2),
    layout = {
        mode = LayoutMode.PATH_DOUBLE_C,
        plane = 0,
        depth = 0.0,

        [LayoutMode.GRID] = {
            columns = 4,
        },
        [LayoutMode.ZIGZAG] = {
            items_per_row = 4,
        },
        [LayoutMode.ZIGZAG_PATH] = {
            segment = 3,
        },
        [LayoutMode.SQUARE_SPIRAL] = {
            spacing = 8.0,
        },
        [LayoutMode.HELIX] = {
            radius = 18.0,
            turns = 1.25,
            height_span = 110.0,
            axis = CoordinateAxis.ZAxis,
        },
        [LayoutMode.PATH_DOUBLE_C] = {
            radius = 28.0,
            blocks_per_arc = 8,
            z_step = -2.0,
            axis = CoordinateAxis.ZAxis,
            start_angle = math.pi / 4,  -- 45°: head-on for iso (XY split plane)
            invert = false,
        },
    },
}

-- ── Note-block motion/placement ─────────────────────────────────────────────
S.note_block = {
    contact_depth = 0,
    travel_distance = 40.0,

    -- Squash-and-stretch: velocity→stretch (momentum), acceleration→squash (force)
    squash = {
        enabled = false,
        stretch_strength = 0.8,     -- elongation along velocity at max; 0 = none
        squash_strength = 0.8,    -- compression along accel at max; 0 = none
        stretch_speed_ref = 10.0,  -- (blocks/sec) speed at which stretch is full
        squash_accel_ref = 20.0,  -- (blocks/sec²) accel at which squash is full
        volume_preserve = true,    -- scale perpendicular by 1/sqrt(primary)
        roundness = 0.7,           -- [0, 1] 0 = sharp, 1 = soft silhouette
        impact_boost = 1.5,        -- extra squash on contact; 0 = none
        impact_squash_z = 0.7,     -- Z scale on landing; 1 = none
        impact_expand_xy = 1.2,    -- XY expansion on landing; 1 = none
        impact_duration_sec = 0.25,
        spring_bias = 0.2,        -- extra squash when platform CATCHING/ANTICIPATING; 0 = off
        use_spring_bias = true,    -- query platform spring state for bias
        smoothing = 0.06,          -- [0, ~0.2] reduces jitter; 0 = off
    },
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
    -- Direction: false = upward (default), true = downward with gravity
    downward = true,
    gravity_enabled = true,  -- required for downward; use same gravity as scene

    count = 40,
    lifetime_multiplier = 10,
    initial_speed = 100.0,
    drag_scale = vec3.new(0.5, 0.5, 0.90),
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
    hover_duration_sec = 0.5,
    hover_osc_speed = 2.0,
    hover_osc_amplitude = 10.0,
    hover_blend_sec = 2.0,
    hover_blend_easing = IREasingFunction.CUBIC_EASE_OUT,

    -- Per-particle randomness (0.0 = none, 0.3 = +/-30% of base value)
    hover_start_variance = 0.2,
    hover_duration_variance = 0.5,
    hover_amplitude_variance = 0.3,
    hover_speed_variance = 0.3,

    -- Spawn glow (auto-fires on particle creation)
    glow_enabled = true,
    glow_mix_to_white = 1.0,
    glow_hold_sec = 0.5,
    glow_fade_sec = 1.0,
    glow_easing = IREasingFunction.LINEAR_INTERPOLATION,
}

-- ── Visual options ──────────────────────────────────────────────────────────
S.visual = {
    platform_color = {
        mode = PlatformColorMode.MATCH_BLOCK,
        [PlatformColorMode.DESATURATE] = {
            desat_factor = 0.45,
            darken_factor = 0.30,
        },
    },

    platform_mute_color = Color.new(20, 20, 25, 255),
    platform_mute_amount = 0.60,

    background_enabled = false,
    background = {
        mode = "trixel_canvas_pulse",
        trixel_canvas_pulse = {
            color_a = Color.new(15, 15, 15, 255),
            color_b = Color.new(2, 2, 2, 255),
            pulse_speed = 0.5,
            pattern_scale = 4,
            start_zoom_multiplier = 4.0,
            wave_dir = vec2.new(1.0, 1.0),
            wave_phase_scale = 10.0,
            wave_speed_multiplier = 1.0,
            wave_start_offset = 0.0,
            wave_direction_motion_enabled = false,
            wave_direction_motion_start = vec2.new(1.0, 1.0),
            wave_direction_motion_end = vec2.new(-1.0, 1.0),
            wave_direction_motion_period_sec = 100.0,
            wave_direction_motion_ease_forward = IREasingFunction.SINE_EASE_IN_OUT,
            wave_direction_motion_ease_backward = IREasingFunction.SINE_EASE_IN_OUT,
            wave2_dir = vec2.new(-1.0, 1.0),
            wave2_phase_scale = 7.0,
            wave2_speed_multiplier = 1.25,
            wave2_start_offset = 1.2,
            wave2_direction_motion_enabled = true,
            wave2_direction_motion_start = vec2.new(-1.2, 1.0),
            wave2_direction_motion_end = vec2.new(-0.8, 1.0),
            wave2_direction_motion_period_sec = 41.0,
            wave2_direction_motion_ease_forward = IREasingFunction.SINE_EASE_IN_OUT,
            wave2_direction_motion_ease_backward = IREasingFunction.SINE_EASE_IN_OUT,
            wave_interference_mix = 0.2,
        },
    },

    camera_start_zoom = 4.0,

    note_hit_glow_enabled = true,
    note_hit_glow_mix_to_white = 1.0,
    note_hit_glow_hold_sec = 0.30,
    note_hit_glow_fade_sec = 1.0,
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
    mode         = ScaleMode.DORIAN,
    root_note    = NoteName.Eb,
    root_octave  = 3,
    start_offset = 0,
}
S.scale.intervals = IRAudio.getScaleIntervals(S.scale.mode)
S.scale.root      = IRAudio.rootNote(S.scale.root_note, S.scale.root_octave)

return S
