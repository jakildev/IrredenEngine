-- ============================================================================
-- entities.lua  –  Platform, note-block, and background entity creation
-- ============================================================================

local E = {}

-- Requires voices module for layout_position
local voices_mod  -- set by init()

function E.init(voices_module)
    voices_mod = voices_module
end

-- ── Platforms ───────────────────────────────────────────────────────────────

function E.create_platforms(settings, voices, num_voices)
    local scene  = settings.scene
    local platform = settings.platform
    local vis    = settings.visual
    local off    = scene.center_offset
    local psz    = platform.size
    local ps     = platform.spring

    local cb_position = function(params)
        local i = params.index.x
        local p = voices_mod.layout_position(i, num_voices, platform)
        local plat_pos = vec3.new(off.x + p.x, off.y + p.y, off.z + p.z)
        print(string.format(
            "[Platform %2d] layout=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f)",
            i, p.x, p.y, p.z, plat_pos.x, plat_pos.y, plat_pos.z))
        return C_Position3D.new(plat_pos)
    end

    local cb_voxel = function(params)
        local i = params.index.x
        local v = voices[i + 1]
        return C_VoxelSetNew.new(psz, v.platform_color)
    end

    local cb_collider = function(params)
        local hs = vec3.new(psz.x * 0.5, psz.y * 0.5, psz.z * 0.5)
        return C_ColliderIso3DAABB.new(hs, hs)
    end

    local cb_layer = function(params)
        return C_CollisionLayer.new(IRCollisionLayer.NOTE_PLATFORM, IRCollisionLayer.NOTE_BLOCK, true)
    end

    local cb_contact = function(params) return C_ContactEvent.new() end
    local cb_vel     = function(params) return C_Velocity3D.new(0.0, 0.0, 0.0) end

    local cb_glow = function(params)
        local i = params.index.x
        local v = voices[i + 1]
        local base_color = IRMath.lerpColor(v.color, vis.platform_mute_color, vis.platform_mute_amount)
        local glow_color = IRMath.lerpColor(base_color, Color.new(255, 255, 255, 255),
            vis.platform_hit_glow_mix_to_white)
        if not vis.platform_hit_glow_enabled then glow_color = base_color end
        return C_TriggerGlow.new(
            glow_color,
            vis.platform_hit_glow_hold_sec,
            vis.platform_hit_glow_fade_sec,
            vis.platform_hit_glow_easing,
            vis.platform_hit_glow_enabled
        )
    end

    local cb_spring = function(params)
        return C_SpringPlatform.new(
            ps.auto_stiffness,
            ps.damping,
            ps.length,
            ps.lock_ratio,
            ps.overshoot_ratio,
            ps.absorption_ratio,
            ps.max_launch_oscillations,
            ps.max_catch_oscillations,
            ps.settle_speed,
            ps.load_lead_sec,
            ps.direction,
            ps.color_shift_hsv,
            ps.release_color_shift_hsv or ColorHSV(0.0, 0.0, 0.0, 0.0),
            ps.color_min_value or 0.0,
            ps.color_min_saturation or 0.0
        )
    end

    local cb_color_state = function(params)
        return C_AnimColorState.new(AnimColorBlendMode.REPLACE)
    end

    IREntity.createEntityBatchNotePlatformsSpring(
        ivec3.new(num_voices, 1, 1),
        cb_position, cb_voxel, cb_collider, cb_layer,
        cb_contact, cb_vel, cb_glow, cb_spring, cb_color_state
    )
end

-- ── Note blocks ─────────────────────────────────────────────────────────────

function E.create_note_blocks(settings, voices, num_voices, gravity_mag)
    local scene  = settings.scene
    local platform = settings.platform
    local note_block = settings.note_block
    local ps = platform.spring
    local vis    = settings.visual
    local part   = settings.particle
    local off    = scene.center_offset
    local psz    = platform.size

    IREntity.createEntityBatchNoteBlocksPhysics(
        ivec3.new(num_voices, 1, 1),

        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            local p = voices_mod.layout_position(i, num_voices, platform)
            local center_x = (psz.x - v.size) / 2.0
            local center_y = (psz.y - v.size) / 2.0
            local rest_offset_z = v.size - note_block.contact_depth
            -- Platform position is center; block sits on top face.
            -- Use full thickness for empirical correction to observed peak.
            local platform_top_z = (off.z + p.z) + psz.z - rest_offset_z
            -- Peak height from impulse (v²/2g). Launch happens from compressed
            -- spring (locked), not rest — add lock_point so we measure from
            -- the actual launch height.
            local lock_point = ps.length * ps.lock_ratio
            local g = IRPhysics.getGravityMagnitude()
            local impulse_speed = IRPhysics.impulseForHeight(gravity_mag, note_block.travel_distance)
            local peak_height = IRPhysics.heightForImpulse(g, impulse_speed)
            local peak_z = platform_top_z + lock_point - peak_height
            return C_Position3D.new(vec3.new(off.x + p.x + center_x, off.y + p.y + center_y, peak_z))
        end,

        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            return C_VoxelSetNew.new(ivec3.new(v.size, v.size, v.size), v.color)
        end,

        function(params) return C_Velocity3D.new(0.0, 0.0, 0.0) end,
        function(params) return C_HasGravity.new() end,

        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            local impulse_speed = IRPhysics.impulseForHeight(gravity_mag, note_block.travel_distance)
            local rest_offset_z = v.size - note_block.contact_depth
            local fall_time = IRPhysics.flightTimeForHeight(gravity_mag, note_block.travel_distance) / 2.0
            -- Always start with fall_time elapsed so blocks are ready to fall
            local start_elapsed = fall_time
            local start_frozen = false  -- Never start frozen, let pauseAll handle that
            
            if settings.stop_after_cycle then
                local max_launches = math.floor(v.launches_per_cycle)
                return C_RhythmicLaunch.new(v.period_sec, vec3.new(0.0, 0.0, -impulse_speed), rest_offset_z, start_elapsed, start_frozen, max_launches)
            end
            return C_RhythmicLaunch.new(v.period_sec, vec3.new(0.0, 0.0, -impulse_speed), rest_offset_z, start_elapsed, start_frozen)
        end,

        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            local hs = v.size * 0.5
            return C_ColliderIso3DAABB.new(vec3.new(hs, hs, hs), vec3.new(hs, hs, hs))
        end,

        function(params)
            return C_CollisionLayer.new(IRCollisionLayer.NOTE_BLOCK, IRCollisionLayer.NOTE_PLATFORM, true)
        end,

        function(params) return C_ContactEvent.new() end,

        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            return C_MidiNote.new(v.note, v.vel, v.ch, v.hold)
        end,

        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            local b = C_ParticleBurst.new(
                v.burst, v.bLife, v.bSpd, v.bAccel,
                v.bDragX, v.bDragY, v.bDragZ,
                part.burst_spawn_offset_z, part.burst_iso_depth_behind
            )
            b.xySpeedRatio       = part.xy_speed_ratio
            b.zSpeedRatio        = part.z_speed_ratio
            b.zVarianceRatio     = part.z_variance_ratio
            b.pDragPerSecond     = part.drag_per_second
            b.pDriftDelaySeconds = part.drift_delay_sec
            -- Downward + gravity: let gravity pull; drift_up would fight it
            b.pDriftUpAccelPerSec = (part.downward and part.gravity_enabled) and 0 or part.drift_up_accel
            b.gravityEnabled      = part.gravity_enabled
            b.downward            = part.downward
            b.pDragMinSpeed     = part.min_speed
            b.pHoverDurationSec = part.hover_duration_sec
            b.pHoverOscSpeed    = part.hover_osc_speed
            b.pHoverOscAmplitude = part.hover_osc_amplitude
            b.pHoverBlendSec    = part.hover_blend_sec
            b.pHoverBlendEasing = part.hover_blend_easing
            b.hoverStartVariance    = part.hover_start_variance
            b.hoverDurationVariance = part.hover_duration_variance
            b.hoverAmplitudeVariance = part.hover_amplitude_variance
            b.hoverSpeedVariance    = part.hover_speed_variance
            b.glowEnabled       = part.glow_enabled
            if part.glow_enabled then
                local glow_color = IRMath.lerpColor(v.color, Color.new(255, 255, 255, 255),
                    part.glow_mix_to_white)
                b.glowColor     = glow_color
            end
            b.glowHoldSeconds   = part.glow_hold_sec
            b.glowFadeSeconds   = part.glow_fade_sec
            b.glowEasing        = part.glow_easing
            return b
        end,

        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            local glow_color = IRMath.lerpColor(v.color, Color.new(255, 255, 255, 255),
                vis.note_hit_glow_mix_to_white)
            if not vis.note_hit_glow_enabled then glow_color = v.color end
            return C_TriggerGlow.new(
                glow_color,
                vis.note_hit_glow_hold_sec,
                vis.note_hit_glow_fade_sec,
                vis.note_hit_glow_easing,
                vis.note_hit_glow_enabled
            )
        end,

        function(params)
            local sq = note_block.squash or {}
            if not sq.enabled then
                return C_VoxelSquashStretch.new(0.0, 0.0, 1.0, 0.0, 1.0, 0.0)
            end
            local c = C_VoxelSquashStretch.new(
                sq.stretch_strength or 0.2,
                sq.squash_strength or 0.15,
                sq.stretch_speed_ref or sq.max_speed_ref or 50.0,
                sq.roundness or 0.6,
                sq.impact_squash_z or 0.75,
                sq.impact_duration_sec or 0.12
            )
            c.stretchSpeedRef = sq.stretch_speed_ref or sq.max_speed_ref or 50.0
            c.squashAccelRef = sq.squash_accel_ref or 120.0
            c.volumePreserve = sq.volume_preserve ~= false
            c.impactBoost = sq.impact_boost or 1.5
            c.impactExpandXY = sq.impact_expand_xy or 1.2
            c.springBias = sq.spring_bias or 0.2
            c.useSpringBias = sq.use_spring_bias ~= false
            c.smoothing = sq.smoothing or 0.06
            return c
        end
    )
end

-- ── Background ──────────────────────────────────────────────────────────────

function E.setup_background(settings)
    local vis = settings.visual
    local background_canvas = IREntity.getCanvasEntity("background")

    local bg_cfg = vis.background or {}
    local bg_mode = bg_cfg.mode or "trixel_canvas_pulse"
    local trixel = bg_cfg.trixel_canvas_pulse or {}
    local tc = trixel

    if not (vis.background_enabled and bg_mode == "trixel_canvas_pulse") then
        IREntity.clearTriangleCanvasBackground(background_canvas)
        return
    end

    local canvas_size = IREntity.getCanvasSizeTriangles(background_canvas)
    local bg = C_TriangleCanvasBackground.new(
        BackgroundTypes.PULSE_PATTERN,
        tc.color_a or Color.new(15, 15, 15, 255),
        tc.color_b or Color.new(2, 2, 2, 255),
        canvas_size,
        tc.pulse_speed or 0.5,
        tc.pattern_scale or 4
    )
    bg:setPulseWaveDirection(tc.wave_dir and tc.wave_dir.x or 1.0, tc.wave_dir and tc.wave_dir.y or 1.0, tc.wave_phase_scale or 10.0)
    bg:setPulseWavePrimaryTiming(tc.wave_speed_multiplier or 1.0, tc.wave_start_offset or 0.0)

    if tc.wave_direction_motion_enabled then
        bg:setPulseWaveDirectionLinearMotion(
            (tc.wave_direction_motion_start or vec2.new(1,1)).x,
            (tc.wave_direction_motion_start or vec2.new(1,1)).y,
            (tc.wave_direction_motion_end or vec2.new(-1,1)).x,
            (tc.wave_direction_motion_end or vec2.new(-1,1)).y,
            tc.wave_direction_motion_period_sec or 100.0,
            tc.wave_direction_motion_ease_forward or IREasingFunction.SINE_EASE_IN_OUT,
            tc.wave_direction_motion_ease_backward or IREasingFunction.SINE_EASE_IN_OUT
        )
    else
        bg:clearPulseWaveDirectionLinearMotion()
    end

    bg:setPulseWaveInterference(
        (tc.wave2_dir or vec2.new(-1,1)).x, (tc.wave2_dir or vec2.new(-1,1)).y,
        tc.wave2_phase_scale or 7.0, tc.wave_interference_mix or 0.2
    )
    bg:setPulseWaveSecondaryTiming(tc.wave2_speed_multiplier or 1.25, tc.wave2_start_offset or 1.2)

    if tc.wave2_direction_motion_enabled then
        bg:setPulseWaveSecondaryDirectionLinearMotion(
            (tc.wave2_direction_motion_start or vec2.new(-1.2,1)).x,
            (tc.wave2_direction_motion_start or vec2.new(-1.2,1)).y,
            (tc.wave2_direction_motion_end or vec2.new(-0.8,1)).x,
            (tc.wave2_direction_motion_end or vec2.new(-0.8,1)).y,
            tc.wave2_direction_motion_period_sec or 41.0,
            tc.wave2_direction_motion_ease_forward or IREasingFunction.SINE_EASE_IN_OUT,
            tc.wave2_direction_motion_ease_backward or IREasingFunction.SINE_EASE_IN_OUT
        )
    else
        bg:clearPulseWaveSecondaryDirectionLinearMotion()
    end

    IREntity.setTriangleCanvasBackground(background_canvas, bg)
    IREntity.setTrixelCanvasRenderBehavior(
        background_canvas,
        C_TrixelCanvasRenderBehavior.new(false, false, false, false, false, 1.0, 0.0, 1.0, -1.0)
    )
    IREntity.setZoomLevel(background_canvas, C_ZoomLevel.new(tc.start_zoom_multiplier or 4.0))
end

return E
