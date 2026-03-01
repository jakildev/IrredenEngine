-- ============================================================================
-- entities_anim_backup.lua  –  BACKUP of animation-based platform creation
-- ============================================================================
-- This is a snapshot of entities.lua before the spring platform refactor.
-- Kept for reference / rollback.
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
            local launch_track = C_AnimClipColorTrack.new()
            launch_track.mode = AnimColorTrackMode.HSV_OFFSET
            launch_track.idleMod = ColorHSV(0, 0, 0, 0)
            local land_track = C_AnimClipColorTrack.new()
            land_track.mode = AnimColorTrackMode.HSV_OFFSET
            land_track.idleMod = ColorHSV(0, 0, pa.idle_value_offset or -0.3, 0)
            launch_clip = IREntity.createAnimationClipWithColor(build_clip(pa.launch), launch_track)
            land_clip = IREntity.createAnimationClipWithColor(build_clip(pa.land), land_track)
        elseif use_color and use_hsv_state_blend then
            local idle_offset = pa.idle_value_offset or -0.3
            local launch_track = C_AnimClipColorTrack.new()
            launch_track.mode = AnimColorTrackMode.HSV_OFFSET_STATE_BLEND
            launch_track.startMod = ColorHSV(0, 0, idle_offset, 0)
            launch_track.endMod = ColorHSV(0, 0, 0, 0)
            local land_track = C_AnimClipColorTrack.new()
            land_track.mode = AnimColorTrackMode.HSV_OFFSET_STATE_BLEND
            land_track.startMod = ColorHSV(0, 0, 0, 0)
            land_track.endMod = ColorHSV(0, 0, idle_offset, 0)
            launch_clip = IREntity.createAnimationClipWithColor(build_clip(pa.launch), launch_track)
            land_clip = IREntity.createAnimationClipWithColor(build_clip(pa.land), land_track)
        elseif use_color and use_hsv_timeline then
            local idle_offset = pa.idle_value_offset or -0.3
            local function hsv_from_table_or_default(tbl, default_hsv)
                if not tbl then return default_hsv end
                return ColorHSV(tbl.h or 0.0, tbl.s or 0.0, tbl.v or 0.0, tbl.a or 0.0)
            end
            local function phase_count_of(clip_def)
                local count = 0
                for _, _ in ipairs(clip_def) do count = count + 1 end
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
                        local seg_start_mod = hsv_from_table_or_default(seg.from_mod, start_mod)
                        local seg_end_mod = hsv_from_table_or_default(seg.to_mod, end_mod)
                        track:addTimelineMod(AnimColorModTimelineSegment.new(
                            from_phase, to_phase, seg_start_mod, seg_end_mod, ease))
                    end
                elseif phase_count > 0 then
                    track:addTimelineMod(AnimColorModTimelineSegment.new(
                        0, phase_count - 1, start_mod, end_mod, IREasingFunction.LINEAR_INTERPOLATION))
                end
                return track
            end
            local launch_track = build_timeline_track(pa.launch, ColorHSV(0,0,idle_offset,0), ColorHSV(0,0,0,0), pa.launch_timeline)
            local land_track = build_timeline_track(pa.land, ColorHSV(0,0,0,0), ColorHSV(0,0,idle_offset,0), pa.land_timeline)
            launch_clip = IREntity.createAnimationClipWithColor(build_clip(pa.launch), launch_track)
            land_clip = IREntity.createAnimationClipWithColor(build_clip(pa.land), land_track)
        elseif use_color and pa.launch_colors and pa.land_colors then
            local function build_color_track(color_defs, idle_color)
                local track = C_AnimClipColorTrack.new()
                track.mode = AnimColorTrackMode.ABSOLUTE
                for _, cd in ipairs(color_defs) do
                    local fc = cd.from
                    local tc = cd.to
                    local ease = easing_map[cd.ease] or IREasingFunction.LINEAR_INTERPOLATION
                    track:addPhaseColor(AnimPhaseColor.new(
                        Color.new(fc.r, fc.g, fc.b, fc.a), Color.new(tc.r, tc.g, tc.b, tc.a), ease))
                end
                track.idleColor = idle_color
                return track
            end
            launch_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.launch), build_color_track(pa.launch_colors, Color.new(255,255,255,255)))
            land_clip = IREntity.createAnimationClipWithColor(
                build_clip(pa.land), build_color_track(pa.land_colors, Color.new(255,255,255,255)))
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
            a:addBinding(AnimationBinding.new(AnimTriggerMode.TIMER_SYNC, launch_clip, total_lead, true))
            a:addBinding(AnimationBinding.new(AnimTriggerMode.CONTACT_ENTER, land_clip, 0.0, true))
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
                    return C_AnimMotionColorShift.new(Color.new(255,255,255,255), 0.0, 0.0)
                end
                local i = params.index.x
                local v = voices[i + 1]
                local motion_color = IRMath.lerpColor(
                    v.platform_color, Color.new(255,255,255,255), ms.mix_to_white)
                return C_AnimMotionColorShift.new(motion_color, ms.fade_in_speed, ms.fade_out_speed)
            end
            IREntity.createEntityBatchNotePlatformsAnimatedColor(
                ivec3.new(num_voices, 1, 1),
                cb_position, cb_voxel, cb_collider, cb_layer,
                cb_contact, cb_vel, cb_reactive, cb_glow, cb_action_anim,
                cb_color_state, cb_motion_shift)
        else
            IREntity.createEntityBatchNotePlatformsAnimated(
                ivec3.new(num_voices, 1, 1),
                cb_position, cb_voxel, cb_collider, cb_layer,
                cb_contact, cb_vel, cb_reactive, cb_glow, cb_action_anim)
        end
    else
        IREntity.createEntityBatchNotePlatforms(
            ivec3.new(num_voices, 1, 1),
            cb_position, cb_voxel, cb_collider, cb_layer,
            cb_contact, cb_vel, cb_reactive, cb_glow)
    end
end

-- Note blocks and background are unchanged from the live version.
-- See the main entities.lua for those functions.

return E
