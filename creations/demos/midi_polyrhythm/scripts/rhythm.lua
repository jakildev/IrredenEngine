-- ============================================================================
-- rhythm.lua  –  Polyrhythm presets and math helpers
-- ============================================================================

local R = {}

local seconds_per_minute = 60.0

-- ── Math helpers ────────────────────────────────────────────────────────────

function R.gcd(a, b)
    while b ~= 0 do
        a, b = b, a % b
    end
    return a
end

function R.lcm(a, b)
    return (a / R.gcd(a, b)) * b
end

function R.lcm_list(t)
    local result = t[1]
    for i = 2, #t do
        result = R.lcm(result, t[i])
    end
    return result
end

-- ── Presets ─────────────────────────────────────────────────────────────────
-- FORMAT A  – beat-based (single BPM, integer cycle lengths):
--   bpm, cycles = {c1, c2, ...}
--   period_sec(i) = cycles[i] * 60 / bpm
--   alignment     = LCM(cycles) * 60 / bpm
--
-- FORMAT B  – notes-per-alignment (per-voice rates, direct alignment):
--   align_sec, notes_start, notes_step, num_voices
--   notes_per_align is generated: start, start+step, start+2*step, ...
--   period_sec(i) = align_sec / notes_per_align[i]
--   alignment     = align_sec

R.presets = {
    heartbeat = {
        name = "Heartbeat",
        bpm  = 120,
        cycles = {3, 4},
    },
    prime_pair = {
        name = "Prime Pair",
        bpm  = 120,
        cycles = {5, 7},
    },
    triple = {
        name = "Triple",
        bpm  = 120,
        cycles = {3, 4, 5},
    },
    clockwork = {
        name = "Clockwork",
        bpm  = 120,
        cycles = {3, 4, 5, 6},
    },
    pentad = {
        name = "Pentad",
        bpm  = 120,
        cycles = {4, 5, 6, 8, 10},
    },
    mesmerize = {
        name = "Mesmerize",
        bpm  = 120,
        cycles = {3, 4, 5, 6, 8, 10},
    },
    reich_phase = {
        name = "Reich Phase",
        bpm  = 120,
        cycles = {12, 13},
    },
    cascade = {
        name = "Cascade",
        bpm  = 120,
        cycles = {3, 4, 5, 6, 8, 9, 10, 12},
    },
    nine_smooth = {
        name = "Nine Smooth",
        bpm  = 120,
        cycles = {3, 4, 5, 6, 8, 9, 10, 12, 15},
    },
    dense_twelve = {
        name = "Dense Twelve",
        bpm  = 120,
        cycles = {3, 4, 5, 6, 8, 9, 10, 12, 15, 18, 20, 24},
    },
    full_360 = {
        name = "Full 360",
        bpm  = 120,
        cycles = {3, 4, 5, 6, 8, 9, 10, 12, 15, 18, 20, 24, 30, 36, 40, 45},
    },
    slow_evolve = {
        name = "Slow Evolve",
        bpm  = 100,
        cycles = {5, 7, 11},
    },
    -- Format B presets: wave_<alignment>_<pace>
    -- align_sec = total cycle time
    -- notes_start/notes_step/num_voices define triggers per voice per cycle

    wave_1m_fast = {
        name = "Wave 1m Fast",
        align_sec = 60.0,
        notes_start = 15, notes_step = 1, num_voices = 16,
    },
    wave_1m_slow = {
        name = "Wave 1m Slow",
        align_sec = 60.0,
        notes_start = 8, notes_step = 1, num_voices = 16,
    },
    wave_2m_fast = {
        name = "Wave 2m Fast",
        align_sec = 120.0,
        notes_start = 37, notes_step = 1, num_voices = 16,
    },
    wave_2m_slow = {
        name = "Wave 2m Slow",
        align_sec = 120.0,
        notes_start = 15, notes_step = 1, num_voices = 16,
    },
    wave_3m_slow = {
        name = "Wave 3m Slow",
        align_sec = 180.0,
        notes_start = 10, notes_step = 3, num_voices = 16,
    },
}

-- Compute derived fields for every preset.
for _, preset in pairs(R.presets) do
    if preset.cycles then
        preset.align_beats = R.lcm_list(preset.cycles)
        preset.align_sec   = preset.align_beats * seconds_per_minute / preset.bpm
        preset.num_voices  = #preset.cycles
    else
        preset.notes_per_align = {}
        for i = 1, preset.num_voices do
            preset.notes_per_align[i] = preset.notes_start + (i - 1) * preset.notes_step
        end
    end
end

-- ── Global enum for autocomplete ────────────────────────────────────────────
RhythmPreset = {}
for k in pairs(R.presets) do
    RhythmPreset[k] = k
end

-- ── Selection & debug print ─────────────────────────────────────────────────

function R.select(preset_key, bpm_override)
    local preset = R.presets[preset_key]
    assert(preset, "Unknown rhythm preset: " .. tostring(preset_key))
    if bpm_override and preset.cycles then
        preset.bpm       = bpm_override
        preset.align_sec = preset.align_beats * seconds_per_minute / preset.bpm
    end
    return preset
end

function R.print_all(active_key)
    print("──── Rhythm presets ────")
    local sorted = {}
    for k in pairs(R.presets) do sorted[#sorted + 1] = k end
    table.sort(sorted, function(a, b)
        local pa, pb = R.presets[a], R.presets[b]
        return pa.num_voices < pb.num_voices
            or (pa.num_voices == pb.num_voices and pa.align_sec < pb.align_sec)
    end)
    for _, k in ipairs(sorted) do
        local p = R.presets[k]
        local marker = (k == active_key) and " <<" or ""
        local min_p, max_p
        if p.cycles then
            local min_c, max_c = math.huge, 0
            for _, c in ipairs(p.cycles) do
                min_c = math.min(min_c, c)
                max_c = math.max(max_c, c)
            end
            min_p = min_c * seconds_per_minute / p.bpm
            max_p = max_c * seconds_per_minute / p.bpm
        else
            min_p = p.align_sec / p.notes_per_align[#p.notes_per_align]
            max_p = p.align_sec / p.notes_per_align[1]
        end
        local bpm_str = p.bpm and string.format("%3d bpm", p.bpm) or "  var  "
        print(string.format(
            "  %-16s %2d voices  %s  align %7.1fs  periods %5.2f – %5.2fs%s",
            k, p.num_voices, bpm_str, p.align_sec, min_p, max_p, marker
        ))
    end
    print("────────────────────────")
end

return R
