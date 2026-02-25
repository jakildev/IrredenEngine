-- ============================================================================
-- voices.lua  –  Scale generation, voice creation, layout, period assignment
-- ============================================================================

local V = {}

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

function V.layout_position(i, num_voices, tuning)
    local layout = tuning.layout
    local psz = tuning.platform_size
    local sp_x = psz.x + tuning.lane_spacing
    local sp_y = psz.y + tuning.lane_spacing
    if layout.mode == "zigzag" then
        return IRMath.layoutZigZagCentered(
            i, num_voices, layout.zigzag_items_per_row,
            sp_x, sp_y,
            layout.plane, layout.depth
        )
    elseif layout.mode == "zigzag_path" then
        return IRMath.layoutZigZagPath(
            i, num_voices, layout.zigzag_path_segment,
            sp_x, sp_y,
            layout.plane, layout.depth
        )
    elseif layout.mode == "square_spiral" then
        return IRMath.layoutSquareSpiral(i, layout.spiral_spacing, layout.plane, layout.depth)
    elseif layout.mode == "helix" then
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
    local num_voices = preset.num_voices
    local scale_notes = V.build_scale_notes(settings.scale, num_voices)
    local pitch_class = V.build_pitch_classes(num_voices, #settings.scale.intervals)

    print(string.format("[Scale] root=%d  offset=%d  degrees=%d  voices=%d",
        settings.scale.root, settings.scale.start_offset,
        #settings.scale.intervals, num_voices))
    local note_list = {}
    for i = 1, num_voices do note_list[i] = tostring(scale_notes[i]) end
    print("[Scale] MIDI notes: " .. table.concat(note_list, ", "))

    local voices = {}
    for i = 1, num_voices do
        local pc = pitch_class[i]
        local note_color = palette.note_colors[pc]
        local plat_color = palette.platform_color_for(note_color, pc)
        voices[i] = {
            note = scale_notes[i],
            color = note_color,
            platform_color = plat_color,
            size = 6,
            vel = math.max(64, 112 - ((i - 1) * 3)),
            hold = 0.12,
            ch = 0,
            burst = part.count,
            bLife = math.floor(60 * part.lifetime_multiplier),
            bSpd = part.initial_speed,
            bAccel = 0.0,
            bDragX = part.drag_scale.x,
            bDragY = part.drag_scale.y,
            bDragZ = part.drag_scale.z,
        }
    end

    -- Assign periods from the selected preset.
    for i = 1, num_voices do
        local v = voices[i]
        if preset.cycles then
            v.cycle_beats = preset.cycles[i]
            v.period_sec  = v.cycle_beats * seconds_per_minute / preset.bpm
        else
            v.notes_per_align = preset.notes_per_align[i]
            v.period_sec = preset.align_sec / v.notes_per_align
        end
    end

    -- Debug print
    print(string.format(
        "Preset '%s': %d voices, align every %.1fs",
        settings.tuning.rhythm.preset, num_voices, preset.align_sec
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
