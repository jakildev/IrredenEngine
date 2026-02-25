 -- ============================================================================
-- MIDI Polyrhythm Visualizer
-- ============================================================================
--
-- 9 voices on C minor pentatonic, each with a unique beat period.
-- All voices fire simultaneously on startup, then drift apart and
-- periodically realign as their timing ratios converge.
--
-- Animation: blocks climb linearly, then fall with eased acceleration. On impact
-- at the floor, a particle burst explodes outward and the MIDI note fires.
-- A small rebound bounce follows before the next climb.
--
-- ============================================================================

IRAudio.openMidiOut("OP-1")

local tau = math.pi * 2.0

-- ── Configuration ───────────────────────────────────────────────────────────

local bpm = 83
local beat_sec = 60.0 / bpm
local active_palette_name = "mulfok32"

-- Primary tweak points for motion/layout behavior.
local tuning = {
    lane_spacing = 12.0,
    platform_z = 100.0,
    platform_size_x = 8,
    platform_size_y = 8,
    platform_size_z = 2,
    start_phase = tau - 0.001,
    note_contact_depth = 1.25,
    note_travel_distance = 50.0,
    note_fall_duration_sec = 0.7,
    linear_block_time_step_sec = 0.005,
    burst_spawn_offset_z = 0,
    burst_iso_depth_behind = 0
}

local visual_options = {
    platform_mute_color = Color.new(20, 20, 25, 255),
    platform_mute_amount = 0.60,
    background_enabled = false,
    -- Drawn as a 2D trixel canvas background via C_TriangleCanvasBackground.
    background_mode = "trixel_canvas_pulse",
    background_color_a = Color.new(38, 38, 34, 255),
    background_color_b = Color.new(15, 15, 11, 255),
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

    particle_lifetime_multiplier = 10,
    particle_upward_acceleration = 20.0,
    particle_drag_scale = vec3.new(1.0, 1.0, 0.0)
}

-- Load palette assets.
local sweetie = IRMath.loadPalette("data/palettes/sweetie-16-1x.png")
local island_joy = IRMath.loadPalette("data/palettes/island-joy-16-1x.png")
local chasm = IRMath.loadPalette("data/palettes/chasm-1x.png")
local pear36 = IRMath.loadPalette("data/palettes/pear36-1x.png")
local mulfok32 = IRMath.loadPalette("data/palettes/mulfok32-1x.png")

local sweetie_voice_colors = {
    sweetie[3],   -- red/crimson
    sweetie[4],   -- orange
    sweetie[5],   -- golden yellow
    sweetie[6],   -- lime green
    sweetie[7],   -- green
    sweetie[8],   -- teal
    sweetie[10],  -- medium blue
    sweetie[11],  -- sky blue
    sweetie[12],  -- cyan
}

local island_joy_voice_colors = {
    island_joy[2],   -- mint
    island_joy[3],   -- teal
    island_joy[7],   -- green
    island_joy[8],   -- lime
    island_joy[9],   -- yellow
    island_joy[10],  -- orange
    island_joy[11],  -- rose
    island_joy[13],  -- hot pink
    island_joy[14],  -- light pink
}

-- chasm: 22-color atmospheric palette
local chasm_voice_colors = {
    chasm[4],
    chasm[6],
    chasm[8],
    chasm[10],
    chasm[12],
    chasm[14],
    chasm[16],
    chasm[18],
    chasm[20],
}

-- pear36: 36-color palette
local pear36_voice_colors = {
    pear36[4],
    pear36[6],
    pear36[8],
    pear36[10],
    pear36[12],
    pear36[14],
    pear36[21],
    pear36[22],
    pear36[23],
}

-- mulfok32: 32-color palette
local mulfok32_voice_colors = {
    mulfok32[7],
    mulfok32[6],
    mulfok32[5],
    mulfok32[4],
    mulfok32[3],
    mulfok32[2],
    mulfok32[1],
    mulfok32[28],
    mulfok32[29],
}

local palette_voice_colors = {
    sweetie = sweetie_voice_colors,
    island_joy = island_joy_voice_colors,
    chasm = chasm_voice_colors,
    pear36 = pear36_voice_colors,
    mulfok32 = mulfok32_voice_colors
}

-- Active palette selection for note block colors.
local voice_colors = palette_voice_colors[active_palette_name] or mulfok32_voice_colors
visual_options.background_color_a = Color.new(15, 15, 15, 255)
visual_options.background_color_b = Color.new(2, 2, 2, 255)

-- 9 voices: C minor pentatonic across octaves
-- Beat periods are intentionally mixed for shifting polyrhythmic overlap.
local voices = {
    { note = MidiNote.C2,  beats = 18, color = voice_colors[1],
      size = 6, vel = 110, hold = 0.5,  ch = 0,
      burst = 20, bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.Eb2, beats = 14, color = voice_colors[2],
      size = 6, vel = 100, hold = 0.4,  ch = 0,
      burst = 20, bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.F3,  beats = 10, color = voice_colors[3],
      size = 6, vel = 94,  hold = 0.30, ch = 0,
      burst = 20, bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.G3,  beats = 8,  color = voice_colors[4],
      size = 6, vel = 90,  hold = 0.20, ch = 0,
      burst = 20,  bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.Bb3, beats = 6,  color = voice_colors[5],
      size = 6, vel = 86,  hold = 0.20, ch = 0,
      burst = 20,  bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.C4,  beats = 5,  color = voice_colors[6],
      size = 6, vel = 80,  hold = 0.15, ch = 0,
      burst = 20,  bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.Eb4, beats = 4,  color = voice_colors[7],
      size = 6, vel = 75,  hold = 0.10, ch = 0,
      burst = 20,  bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.F4,  beats = 3,  color = voice_colors[8],
      size = 6, vel = 72,  hold = 0.09, ch = 0,
      burst = 20,  bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },

    { note = MidiNote.G4,  beats = 2,  color = voice_colors[9],
      size = 6, vel = 70,  hold = 0.08, ch = 0,
      burst = 20,  bLife = 60, bSpd = 5, bAccel = visual_options.particle_upward_acceleration,
      bDragX = visual_options.particle_drag_scale.x, bDragY = visual_options.particle_drag_scale.y,
      bDragZ = visual_options.particle_drag_scale.z },
}

local num_voices = #voices

-- Arrange lanes adjacent along the screen bottom:
-- keep isometric depth stable by moving (+x, -x, 0).
local function lane_grid_x(i)
    return math.floor(i / 2)
end

local function lane_grid_y(i)
    return -math.floor((i + 1) / 2)
end

-- Keep the lane layout centered around (0,0) for any voice count.
local lane_last_index = math.max(0, num_voices - 1)
local lane_center_x = (lane_grid_x(0) + lane_grid_x(lane_last_index)) * 0.5
local lane_center_y = (lane_grid_y(0) + lane_grid_y(lane_last_index)) * 0.5

local function lane_position(i)
    local x = (lane_grid_x(i) - lane_center_x) * tuning.lane_spacing
    local y = (lane_grid_y(i) - lane_center_y) * tuning.lane_spacing
    return x, y
end

for i = 1, num_voices do
    local v = voices[i]
    v.bLife = math.floor(v.bLife * visual_options.particle_lifetime_multiplier)
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
        return C_Position3D.new(vec3.new(x, y, tuning.platform_z))
    end,

    -- Platform voxel
    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local platform_color = IRMath.lerpColor(
            v.color, visual_options.platform_mute_color, visual_options.platform_mute_amount)
        return C_VoxelSetNew.new(
            ivec3.new(tuning.platform_size_x, tuning.platform_size_y, tuning.platform_size_z),
            platform_color
        )
    end,

    function(params)
        return C_ColliderIso3DAABB.new(
            vec3.new(tuning.platform_size_x * 0.5, tuning.platform_size_y * 0.5, tuning.platform_size_z * 0.5),
            vec3.new(tuning.platform_size_x * 0.5, tuning.platform_size_y * 0.5, tuning.platform_size_z * 0.5)
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
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local base_color = IRMath.lerpColor(
            v.color, visual_options.platform_mute_color, visual_options.platform_mute_amount)
        local glow_color = IRMath.lerpColor(base_color, Color.new(255, 255, 255, 255),
            visual_options.platform_hit_glow_mix_to_white)

        if not visual_options.platform_hit_glow_enabled then
            glow_color = base_color
        end

        return C_TriggerGlow.new(
            glow_color,
            visual_options.platform_hit_glow_hold_sec,
            visual_options.platform_hit_glow_fade_sec,
            visual_options.platform_hit_glow_easing,
            visual_options.platform_hit_glow_enabled
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
        local travel = tuning.note_travel_distance
        -- Add slight penetration at cycle bottom so collision-enter is robust.
        local base_z = tuning.platform_z - v.size - travel + tuning.note_contact_depth
        return C_Position3D.new(vec3.new(x, y, base_z))
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        return C_VoxelSetNew.new(ivec3.new(v.size, v.size, v.size), v.color)
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local period = beat_sec * v.beats
        local travel = tuning.note_travel_distance
        local fall_duration = math.min(tuning.note_fall_duration_sec, period - 0.05)
        local climb_duration = period - fall_duration
        local fastest_voice_index = num_voices - 1
        local block_distance_from_fastest = i - fastest_voice_index
        local time_offset_sec = block_distance_from_fastest * tuning.linear_block_time_step_sec
        local phase_offset = (time_offset_sec / period) * tau
        local idle = C_PeriodicIdle.new(vec3.new(0.0, 0.0, travel), period, tuning.start_phase + phase_offset)
        idle:addStageDurationSeconds(0.0, fall_duration, 0.0, 1.0, IREasingFunction.QUADRATIC_EASE_IN)
        idle:addStageDurationSeconds(fall_duration, climb_duration, 1.0, 0.0, IREasingFunction.LINEAR_INTERPOLATION)
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
        return C_ParticleBurst.new(
            v.burst,
            v.bLife,
            v.bSpd,
            v.bAccel,
            v.bDragX,
            v.bDragY,
            v.bDragZ,
            tuning.burst_spawn_offset_z,
            tuning.burst_iso_depth_behind
        )
    end,

    function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local base_color = v.color
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
local background_canvas = IREntity.getCanvasEntity("background")
if visual_options.background_enabled and visual_options.background_mode == "trixel_canvas_pulse" then
    local canvas_size = IREntity.getCanvasSizeTriangles(background_canvas)
    local background = C_TriangleCanvasBackground.new(
        BackgroundTypes.PULSE_PATTERN,
        visual_options.background_color_a,
        visual_options.background_color_b,
        canvas_size,
        visual_options.background_pulse_speed,
        visual_options.background_pattern_scale
    )
    background:setPulseWaveDirection(
        visual_options.background_wave_dir.x,
        visual_options.background_wave_dir.y,
        visual_options.background_wave_phase_scale
    )
    background:setPulseWavePrimaryTiming(
        visual_options.background_wave_speed_multiplier,
        visual_options.background_wave_start_offset
    )
    if visual_options.background_wave_direction_motion_enabled then
        background:setPulseWaveDirectionLinearMotion(
            visual_options.background_wave_direction_motion_start.x,
            visual_options.background_wave_direction_motion_start.y,
            visual_options.background_wave_direction_motion_end.x,
            visual_options.background_wave_direction_motion_end.y,
            visual_options.background_wave_direction_motion_period_sec,
            visual_options.background_wave_direction_motion_ease_forward,
            visual_options.background_wave_direction_motion_ease_backward
        )
    else
        background:clearPulseWaveDirectionLinearMotion()
    end
    background:setPulseWaveInterference(
        visual_options.background_wave2_dir.x,
        visual_options.background_wave2_dir.y,
        visual_options.background_wave2_phase_scale,
        visual_options.background_wave_interference_mix
    )
    background:setPulseWaveSecondaryTiming(
        visual_options.background_wave2_speed_multiplier,
        visual_options.background_wave2_start_offset
    )
    if visual_options.background_wave2_direction_motion_enabled then
        background:setPulseWaveSecondaryDirectionLinearMotion(
            visual_options.background_wave2_direction_motion_start.x,
            visual_options.background_wave2_direction_motion_start.y,
            visual_options.background_wave2_direction_motion_end.x,
            visual_options.background_wave2_direction_motion_end.y,
            visual_options.background_wave2_direction_motion_period_sec,
            visual_options.background_wave2_direction_motion_ease_forward,
            visual_options.background_wave2_direction_motion_ease_backward
        )
    else
        background:clearPulseWaveSecondaryDirectionLinearMotion()
    end
    IREntity.setTriangleCanvasBackground(background_canvas, background)
    IREntity.setTrixelCanvasRenderBehavior(
        background_canvas,
        C_TrixelCanvasRenderBehavior.new(
            false, -- useCameraPositionIso
            false, -- useCameraZoom
            false, -- applyRenderSubdivisions
            false, -- mouseHoverEnabled
            false, -- usePixelPerfectCameraOffset
            1.0,   -- parityOffsetIsoX
            0.0,   -- parityOffsetIsoY
            1.0,   -- staticPixelOffsetX
            -1.0   -- staticPixelOffsetY
        )
    )
    IREntity.setZoomLevel(background_canvas, C_ZoomLevel.new(visual_options.background_start_zoom_multiplier))
else
    IREntity.clearTriangleCanvasBackground(background_canvas)
end

IRRender.setCameraZoom(visual_options.camera_start_zoom)
