-- ============================================================================
-- voices.lua  –  Scale generation, voice creation, layout, period assignment
-- ============================================================================

local V = {}

-- ── Global enum for autocomplete ────────────────────────────────────────────
LayoutMode = {
    GRID           = "grid",
    ZIGZAG         = "zigzag",
    ZIGZAG_PATH    = "zigzag_path",
    SQUARE_SPIRAL  = "square_spiral",
    HELIX          = "helix",
}

local seconds_per_minute = 60.0

-- ── Scale helpers ───────────────────────────────────────────────────────────

function V.build_scale_notes(cfg, count)
    local notes = {}
    local n = #cfg.intervals
    for i = 1, count do
        local idx = cfg.start_offset + (i - 1)
        local octave = math.floor(idx / n)
        local degree = (idx % n) + 1
        notes[i] = cfg.root + octave * 12 + cfg.intervals[degree]
    end
    return notes
end

function V.build_pitch_classes(count, scale_len)
    local pc = {}
    for i = 1, count do
        pc[i] = ((i - 1) % scale_len) + 1
    end
    return pc
end

-- ── Layout ──────────────────────────────────────────────────────────────────

function V.layout_position(i, num_voices, platform_cfg)
    local layout = platform_cfg.layout
    local psz = platform_cfg.size
    local sp_x = psz.x + platform_cfg.lane_spacing
    local sp_y = psz.y + platform_cfg.lane_spacing
    if layout.mode == LayoutMode.ZIGZAG then
        return IRMath.layoutZigZagCentered(
            i, num_voices, layout.zigzag_items_per_row,
            sp_x, sp_y,
            layout.plane, layout.depth
        )
    elseif layout.mode == LayoutMode.ZIGZAG_PATH then
        return IRMath.layoutZigZagPath(
            i, num_voices, layout.zigzag_path_segment,
            sp_x, sp_y,
            layout.plane, layout.depth
        )
    elseif layout.mode == LayoutMode.SQUARE_SPIRAL then
        return IRMath.layoutSquareSpiral(i, layout.spiral_spacing, layout.plane, layout.depth)
    elseif layout.mode == LayoutMode.HELIX then
        return IRMath.layoutHelix(
            i, num_voices, layout.helix_radius, layout.helix_turns,
            layout.helix_height_span, layout.helix_axis
        )
    end
    local columns = layout.grid_columns
    if columns <= 0 then
        columns = math.ceil(math.sqrt(num_voices))
    end
    return IRMath.layoutGridCentered(
        i, num_voices, columns,
        sp_x, sp_y,
        layout.plane, layout.depth
    )
end

-- ── Build voice list ────────────────────────────────────────────────────────

function V.build(settings, preset, palette)
    local part = settings.particle
    local voice_cfg = settings.voice or {}
    local block_size = voice_cfg.block_size or 6
    local vel_start = voice_cfg.midi_velocity_start or 112
    local vel_step = voice_cfg.midi_velocity_step or 3
    local vel_min = voice_cfg.midi_velocity_min or 64
    local hold_sec = voice_cfg.midi_hold_sec or 0.12
    local midi_channel = voice_cfg.midi_channel or 0
    local num_voices = preset.num_voices
    local scale_notes = V.build_scale_notes(settings.scale, num_voices)
    local pitch_class = V.build_pitch_classes(num_voices, #settings.scale.intervals)

    print(string.format("[Scale] root=%d  offset=%d  degrees=%d  voices=%d",
        settings.scale.root, settings.scale.start_offset,
        #settings.scale.intervals, num_voices))
    local note_list = {}
    for i = 1, num_voices do note_list[i] = tostring(scale_notes[i]) end
    print("[Scale] MIDI notes: " .. table.concat(note_list, ", "))

    local per_voice = (palette.note_color_mode == NoteColorMode.PER_VOICE)

    local voices = {}
    for i = 1, num_voices do
        local pc = pitch_class[i]
        local color_idx  = per_voice and i or pc
        local note_color = palette.note_colors[color_idx]
        local plat_color = palette.platform_color_for(note_color, color_idx)
        voices[i] = {
            note = scale_notes[i],
            color = note_color,
            platform_color = plat_color,
            size = block_size,
            vel = math.max(vel_min, vel_start - ((i - 1) * vel_step)),
            hold = hold_sec,
            ch = midi_channel,
            burst = part.count,
            bLife = math.floor(60 * part.lifetime_multiplier),
            bSpd = part.initial_speed,
            bAccel = 0.0,
            bDragX = part.drag_scale.x,
            bDragY = part.drag_scale.y,
            bDragZ = part.drag_scale.z,
        }
    end

    -- Assign periods and per-cycle launch counts from the selected preset.
    for i = 1, num_voices do
        local v = voices[i]
        if preset.cycles then
            v.cycle_beats = preset.cycles[i]
            v.period_sec  = v.cycle_beats * seconds_per_minute / preset.bpm
            v.launches_per_cycle = preset.align_beats / v.cycle_beats
        else
            v.notes_per_align = preset.notes_per_align[i]
            v.period_sec = preset.align_sec / v.notes_per_align
            v.launches_per_cycle = v.notes_per_align
        end
    end

    -- Debug print
    print(string.format(
        "Preset '%s': %d voices, align every %.1fs",
        settings.rhythm_preset, num_voices, preset.align_sec
    ))
    for i = 1, num_voices do
        local v = voices[i]
        if v.cycle_beats then
            print(string.format(
                "  voice %2d: cycle %3d beats  period %6.3fs", i, v.cycle_beats, v.period_sec))
        else
            print(string.format(
                "  voice %2d: %3d notes/%gs  period %6.3fs",
                i, v.notes_per_align, preset.align_sec, v.period_sec))
        end
    end

    return voices, num_voices
end

return V
