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
    local tuning = settings.tuning
    local vis    = settings.visual
    local off    = tuning.center_offset
    local psz    = tuning.platform_size
    local pa     = tuning.platform_anim

    local use_anim = pa and pa.enabled

    local cb_position = function(params)
        local i = params.index.x
        local p = voices_mod.layout_position(i, num_voices, tuning)
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

    local cb_reactive = function(params)
        return C_ReactiveReturn3D.new(
            vec3.new(0.0, 0.0, 12.0),
            80.0, 14.0, 4, 0.05, 0.1, true
        )
    end

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

    if use_anim then
        local easing_map = {
            LINEAR_INTERPOLATION    = IREasingFunction.LINEAR_INTERPOLATION,
            QUADRATIC_EASE_IN       = IREasingFunction.QUADRATIC_EASE_IN,
            QUADRATIC_EASE_OUT      = IREasingFunction.QUADRATIC_EASE_OUT,
            QUADRATIC_EASE_IN_OUT   = IREasingFunction.QUADRATIC_EASE_IN_OUT,
            CUBIC_EASE_IN           = IREasingFunction.CUBIC_EASE_IN,
            CUBIC_EASE_OUT          = IREasingFunction.CUBIC_EASE_OUT,
            CUBIC_EASE_IN_OUT       = IREasingFunction.CUBIC_EASE_IN_OUT,
            SINE_EASE_IN            = IREasingFunction.SINE_EASE_IN,
            SINE_EASE_OUT           = IREasingFunction.SINE_EASE_OUT,
            SINE_EASE_IN_OUT        = IREasingFunction.SINE_EASE_IN_OUT,
            BACK_EASE_IN            = IREasingFunction.BACK_EASE_IN,
            BACK_EASE_OUT           = IREasingFunction.BACK_EASE_OUT,
            BACK_EASE_IN_OUT        = IREasingFunction.BACK_EASE_IN_OUT,
            BOUNCE_EASE_OUT         = IREasingFunction.BOUNCE_EASE_OUT,
            ELASTIC_EASE_OUT        = IREasingFunction.ELASTIC_EASE_OUT,
        }

        local function build_clip(clip_def)
            local clip = C_AnimationClip.new()
            for i, phase in ipairs(clip_def) do
                local ease = easing_map[phase.ease] or IREasingFunction.LINEAR_INTERPOLATION
                clip:addPhase(ActionAnimationPhase.new(phase.dur, phase.from, phase.to, ease))
            end
            if clip_def.action_phase then
                clip.actionPhaseIndex = clip_def.action_phase
            end
            return clip
        end

        local use_color = pa.color_enabled
        local use_hsv   = pa.color_mode == "HSV_OFFSET"
        local use_hsv_state_blend = pa.color_mode == "HSV_OFFSET_STATE_BLEND"
        local use_hsv_timeline = pa.color_mode == "HSV_OFFSET_TIMELINE"

        local launch_clip, land_clip
        if use_color and use_hsv then
            -- HSV offset mode: shared clips with relative color modifiers.
            -- Launch idle = no offset (original color).
            -- Land idle = darken value.
            local launch_track = C_AnimClipColorTrack.new()
            launch_track.mode = AnimColorTrackMode.HSV_OFFSET
            launch_track.idleMod = ColorHSV(0, 0, 0, 0)

            local land_track = C_AnimClipColorTrack.new()
            land_track.mode = AnimColorTrackMode.HSV_OFFSET
            land_track.idleMod = ColorHSV(0, 0, pa.idle_value_offset or -0.3, 0)

            launch_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.launch), launch_track)
            land_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.land), land_track)
        elseif use_color and use_hsv_state_blend then
            -- HSV state blend mode:
            -- Blend linearly across the whole clip duration from startMod -> endMod.
            -- This supports transitions spanning multiple motion phases.
            local idle_offset = pa.idle_value_offset or -0.3

            local launch_track = C_AnimClipColorTrack.new()
            launch_track.mode = AnimColorTrackMode.HSV_OFFSET_STATE_BLEND
            launch_track.startMod = ColorHSV(0, 0, idle_offset, 0)
            launch_track.endMod = ColorHSV(0, 0, 0, 0)

            local land_track = C_AnimClipColorTrack.new()
            land_track.mode = AnimColorTrackMode.HSV_OFFSET_STATE_BLEND
            land_track.startMod = ColorHSV(0, 0, 0, 0)
            land_track.endMod = ColorHSV(0, 0, idle_offset, 0)

            launch_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.launch), launch_track)
            land_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.land), land_track)
        elseif use_color and use_hsv_timeline then
            -- HSV timeline mode:
            -- Supports one or many arbitrary color transitions across phase ranges.
            local idle_offset = pa.idle_value_offset or -0.3

            local function hsv_from_table_or_default(tbl, default_hsv)
                if not tbl then return default_hsv end
                return ColorHSV(
                    tbl.h or 0.0,
                    tbl.s or 0.0,
                    tbl.v or 0.0,
                    tbl.a or 0.0
                )
            end

            local function phase_count_of(clip_def)
                local count = 0
                for _, _ in ipairs(clip_def) do
                    count = count + 1
                end
                return count
            end

            local function build_timeline_track(clip_def, start_mod, end_mod, timeline_defs)
                local track = C_AnimClipColorTrack.new()
                track.mode = AnimColorTrackMode.HSV_OFFSET_TIMELINE
                track.startMod = start_mod
                track.endMod = end_mod

                local phase_count = phase_count_of(clip_def)
                if timeline_defs and #timeline_defs > 0 then
                    for _, seg in ipairs(timeline_defs) do
                        local ease = easing_map[seg.ease] or IREasingFunction.LINEAR_INTERPOLATION
                        local from_phase = seg.from_phase or seg.from or 0
                        local to_phase = seg.to_phase or seg.to or from_phase
                        local seg_start_mod =
                            hsv_from_table_or_default(seg.from_mod, start_mod)
                        local seg_end_mod =
                            hsv_from_table_or_default(seg.to_mod, end_mod)
                        track:addTimelineMod(AnimColorModTimelineSegment.new(
                            from_phase,
                            to_phase,
                            seg_start_mod,
                            seg_end_mod,
                            ease
                        ))
                    end
                elseif phase_count > 0 then
                    track:addTimelineMod(AnimColorModTimelineSegment.new(
                        0,
                        phase_count - 1,
                        start_mod,
                        end_mod,
                        IREasingFunction.LINEAR_INTERPOLATION
                    ))
                end

                return track
            end

            local launch_track = build_timeline_track(
                pa.launch,
                ColorHSV(0, 0, idle_offset, 0),
                ColorHSV(0, 0, 0, 0),
                pa.launch_timeline
            )
            local land_track = build_timeline_track(
                pa.land,
                ColorHSV(0, 0, 0, 0),
                ColorHSV(0, 0, idle_offset, 0),
                pa.land_timeline
            )

            launch_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.launch), launch_track)
            land_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.land), land_track)
        elseif use_color and pa.launch_colors and pa.land_colors then
            -- Absolute color mode (future use)
            local function build_color_track(color_defs, idle_color)
                local track = C_AnimClipColorTrack.new()
                track.mode = AnimColorTrackMode.ABSOLUTE
                for _, cd in ipairs(color_defs) do
                    local fc = cd.from
                    local tc = cd.to
                    local ease = easing_map[cd.ease] or IREasingFunction.LINEAR_INTERPOLATION
                    track:addPhaseColor(AnimPhaseColor.new(
                        Color.new(fc.r, fc.g, fc.b, fc.a),
                        Color.new(tc.r, tc.g, tc.b, tc.a),
                        ease
                    ))
                end
                track.idleColor = idle_color
                return track
            end
            launch_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.launch),
                build_color_track(pa.launch_colors, Color.new(255, 255, 255, 255)))
            land_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.land),
                build_color_track(pa.land_colors, Color.new(255, 255, 255, 255)))
        else
            launch_clip = IREntity.createAnimationClip(build_clip(pa.launch))
            land_clip   = IREntity.createAnimationClip(build_clip(pa.land))
        end

        local total_lead = 0.0
        for _, phase in ipairs(pa.launch) do
            total_lead = total_lead + phase.dur
            if pa.launch.action_phase and _ > pa.launch.action_phase then break end
        end

        local cb_action_anim = function(params)
            local a = C_ActionAnimation.new()
            a.direction = pa.direction
            a.currentDisplacement = pa.start_displacement or 0.0

            a:addBinding(AnimationBinding.new(
                AnimTriggerMode.TIMER_SYNC, launch_clip, total_lead, true))
            a:addBinding(AnimationBinding.new(
                AnimTriggerMode.CONTACT_ENTER, land_clip, 0.0, true))
            return a
        end

        if use_color then
            local cb_color_state = function(params)
                return C_AnimColorState.new(AnimColorBlendMode.REPLACE)
            end

            local ms = pa.motion_shift
            local ms_enabled = ms and ms.enabled
            local cb_motion_shift = function(params)
                if not ms_enabled then
                    return C_AnimMotionColorShift.new(
                        Color.new(255, 255, 255, 255), 0.0, 0.0)
                end
                local i = params.index.x
                local v = voices[i + 1]
                local motion_color = IRMath.lerpColor(
                    v.platform_color, Color.new(255, 255, 255, 255), ms.mix_to_white)
                return C_AnimMotionColorShift.new(
                    motion_color, ms.fade_in_speed, ms.fade_out_speed)
            end

            IREntity.createEntityBatchNotePlatformsAnimatedColor(
                ivec3.new(num_voices, 1, 1),
                cb_position, cb_voxel, cb_collider, cb_layer,
                cb_contact, cb_vel, cb_reactive, cb_glow, cb_action_anim,
                cb_color_state, cb_motion_shift
            )
        else
            IREntity.createEntityBatchNotePlatformsAnimated(
                ivec3.new(num_voices, 1, 1),
                cb_position, cb_voxel, cb_collider, cb_layer,
                cb_contact, cb_vel, cb_reactive, cb_glow, cb_action_anim
            )
        end
    else
        IREntity.createEntityBatchNotePlatforms(
            ivec3.new(num_voices, 1, 1),
            cb_position, cb_voxel, cb_collider, cb_layer,
            cb_contact, cb_vel, cb_reactive, cb_glow
        )
    end
end

-- ── Note blocks ─────────────────────────────────────────────────────────────

function E.create_note_blocks(settings, voices, num_voices, gravity_mag)
    local tuning = settings.tuning
    local vis    = settings.visual
    local part   = settings.particle
    local off    = tuning.center_offset
    local psz    = tuning.platform_size

    IREntity.createEntityBatchNoteBlocksPhysics(
        ivec3.new(num_voices, 1, 1),

        -- Position: start at peak of arc, centered over platform.
        function(params)
            local i = params.index.x
            local v = voices[i + 1]
            local p = voices_mod.layout_position(i, num_voices, tuning)
            local center_x = (psz.x - v.size) / 2.0
            local center_y = (psz.y - v.size) / 2.0
            local rest_offset_z = v.size - tuning.note_contact_depth
            local grounded_z = (off.z + p.z) - rest_offset_z
            local peak_z = grounded_z - tuning.note_travel_distance
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
            local impulse_speed = IRPhysics.impulseForHeight(gravity_mag, tuning.note_travel_distance)
            local rest_offset_z = v.size - tuning.note_contact_depth
            local fall_time = IRPhysics.flightTimeForHeight(gravity_mag, tuning.note_travel_distance) / 2.0
            return C_RhythmicLaunch.new(v.period_sec, vec3.new(0.0, 0.0, -impulse_speed), rest_offset_z, fall_time, false)
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
                tuning.burst_spawn_offset_z, tuning.burst_iso_depth_behind
            )
            b.xySpeedRatio      = part.xy_speed_ratio
            b.zSpeedRatio       = part.z_speed_ratio
            b.zVarianceRatio    = part.z_variance_ratio
            b.pDragPerSecond    = part.drag_per_second
            b.pDriftDelaySeconds = part.drift_delay_sec
            b.pDriftUpAccelPerSec = part.drift_up_accel
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
        end
    )
end

-- ── Background ──────────────────────────────────────────────────────────────

function E.setup_background(settings)
    local vis = settings.visual
    local background_canvas = IREntity.getCanvasEntity("background")

    if not (vis.background_enabled and vis.background_mode == "trixel_canvas_pulse") then
        IREntity.clearTriangleCanvasBackground(background_canvas)
        return
    end

    local canvas_size = IREntity.getCanvasSizeTriangles(background_canvas)
    local bg = C_TriangleCanvasBackground.new(
        BackgroundTypes.PULSE_PATTERN,
        vis.background_color_a, vis.background_color_b,
        canvas_size, vis.background_pulse_speed, vis.background_pattern_scale
    )
    bg:setPulseWaveDirection(vis.background_wave_dir.x, vis.background_wave_dir.y, vis.background_wave_phase_scale)
    bg:setPulseWavePrimaryTiming(vis.background_wave_speed_multiplier, vis.background_wave_start_offset)

    if vis.background_wave_direction_motion_enabled then
        bg:setPulseWaveDirectionLinearMotion(
            vis.background_wave_direction_motion_start.x, vis.background_wave_direction_motion_start.y,
            vis.background_wave_direction_motion_end.x, vis.background_wave_direction_motion_end.y,
            vis.background_wave_direction_motion_period_sec,
            vis.background_wave_direction_motion_ease_forward,
            vis.background_wave_direction_motion_ease_backward
        )
    else
        bg:clearPulseWaveDirectionLinearMotion()
    end

    bg:setPulseWaveInterference(
        vis.background_wave2_dir.x, vis.background_wave2_dir.y,
        vis.background_wave2_phase_scale, vis.background_wave_interference_mix
    )
    bg:setPulseWaveSecondaryTiming(vis.background_wave2_speed_multiplier, vis.background_wave2_start_offset)

    if vis.background_wave2_direction_motion_enabled then
        bg:setPulseWaveSecondaryDirectionLinearMotion(
            vis.background_wave2_direction_motion_start.x, vis.background_wave2_direction_motion_start.y,
            vis.background_wave2_direction_motion_end.x, vis.background_wave2_direction_motion_end.y,
            vis.background_wave2_direction_motion_period_sec,
            vis.background_wave2_direction_motion_ease_forward,
            vis.background_wave2_direction_motion_ease_backward
        )
    else
        bg:clearPulseWaveSecondaryDirectionLinearMotion()
    end

    IREntity.setTriangleCanvasBackground(background_canvas, bg)
    IREntity.setTrixelCanvasRenderBehavior(
        background_canvas,
        C_TrixelCanvasRenderBehavior.new(false, false, false, false, false, 1.0, 0.0, 1.0, -1.0)
    )
    IREntity.setZoomLevel(background_canvas, C_ZoomLevel.new(vis.background_start_zoom_multiplier))
end

return E
