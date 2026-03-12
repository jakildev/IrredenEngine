-- ============================================================================
-- rhythm.lua  –  Polyrhythm presets and math helpers
-- ============================================================================
--
-- Preset formats:
--   A) cycles + bpm     – integer cycle lengths (beats per voice), shared BPM
--   B) align_sec + notes_start/notes_step/num_voices – notes-per-alignment gradient
--   C) periods_sec + align_sec – direct period in seconds (for irrational BPM, etc.)
--
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

--- True if n is prime.
function R.is_prime(n)
    if n < 2 then return false end
    if n == 2 then return true end
    if n % 2 == 0 then return false end
    for d = 3, math.sqrt(n), 2 do
        if n % d == 0 then return false end
    end
    return true
end

--- First n primes (>= 2).
function R.first_n_primes(n)
    local out, p, count = {}, 2, 0
    while count < n do
        if R.is_prime(p) then
            count = count + 1
            out[count] = p
        end
        p = p + 1
    end
    return out
end

--- Divisors of n (including 1 and n), sorted ascending.
function R.divisors_of(n)
    local out = {}
    for d = 1, math.floor(math.sqrt(n)) do
        if n % d == 0 then
            out[#out + 1] = d
            if d * d ~= n then out[#out + 1] = n / d end
        end
    end
    table.sort(out)
    return out
end

--- Prime gaps: differences between consecutive primes up to max_prime.
function R.prime_gaps(max_prime)
    local gaps, prev = {}, nil
    for p = 2, max_prime do
        if R.is_prime(p) then
            if prev then gaps[#gaps + 1] = p - prev end
            prev = p
        end
    end
    return gaps
end

--- True Fibonacci sequence (1,1,2,3,5,8,13,...) sliced by 1-based indices.
--- fibonacci_cycles(1, 6) -> {1,1,2,3,5,8}; (4, 8) -> {3,5,8,13,21}.
function R.fibonacci_cycles(start_idx, end_idx)
    local fib = {1, 1}
    for i = 3, end_idx do
        fib[i] = fib[i - 1] + fib[i - 2]
    end
    local out = {}
    for i = start_idx, end_idx do
        out[#out + 1] = fib[i]
    end
    return out
end

--- Euclidean rhythm (Bjorklund): distribute k pulses over n steps.
--- Returns a binary pattern {1,0,1,0,...} of length n.
function R.euclidean_pattern(k, n)
    if k <= 0 or n <= 0 then return {} end
    if k >= n then
        local out = {}
        for i = 1, n do out[i] = 1 end
        return out
    end
    local counts, remainders = {}, {}
    local divisor = n - k
    remainders[1] = k
    local len = 1
    while true do
        counts[len] = math.floor(divisor / remainders[len])
        remainders[len + 1] = divisor % remainders[len]
        divisor = remainders[len]
        len = len + 1
        if remainders[len] <= 1 then break end
    end
    counts[len] = divisor

    local build
    build = function(level)
        if level == -1 then
            return {0}
        elseif level == -2 then
            return {1}
        end
        local res = {}
        for i = 1, counts[level] do
            for _, v in ipairs(build(level - 1)) do res[#res + 1] = v end
        end
        for _, v in ipairs(build(level - 2)) do res[#res + 1] = v end
        return res
    end
    return build(len - 1)
end

--- Euclidean-inspired cycle lengths: use common (steps, pulses) pairs.
--- Each (s,p) gives step count s as cycle length; combine multiple for polyrhythm.
function R.euclidean_cycles(...)
    local pairs = {...}  -- e.g. {13,5}, {16,7}, {12,5}
    local cycles = {}
    for i = 1, #pairs, 2 do
        local steps = pairs[i]
        cycles[#cycles + 1] = steps
    end
    return cycles
end

--- Rotational symmetry: all divisors of master that are in [min_div, max_div].
function R.rotational_symmetry(master, min_div, max_div)
    min_div = min_div or 2
    max_div = max_div or master
    local divs = R.divisors_of(master)
    local out = {}
    for _, d in ipairs(divs) do
        if d >= min_div and d <= max_div then out[#out + 1] = d end
    end
    return out
end

--- Spiral / phase-velocity gradient: period_i = base_sec * (1 + (i-1) * epsilon).
--- Returns periods_sec array; align_sec computed as LCM-approximation (max period * n).
function R.spiral_periods(base_sec, epsilon, n)
    local periods = {}
    for i = 1, n do
        periods[i] = base_sec * (1.0 + (i - 1) * epsilon)
    end
    return periods
end

--- Irrational tempo: per-voice BPM detuning. base_bpm + (i-1)*detune_bpm.
--- period_i = 60 / bpm_i. Creates never-perfectly-looping phasing.
function R.irrational_periods(base_bpm, detune_bpm, n)
    local periods = {}
    for i = 1, n do
        local bpm = base_bpm + (i - 1) * detune_bpm
        periods[i] = seconds_per_minute / bpm
    end
    return periods
end

--- Harmonic ratio cycles (just-intonation feel). Ratios as {num, denom}.
--- Converts to integer cycles via LCM of denominators.
--- e.g. {{1,1},{5,4},{3,2},{7,4}} -> periods in shared grid.
function R.harmonic_ratio_cycles(ratios, base)
    base = base or 4
    local dens = {}
    for _, r in ipairs(ratios) do
        dens[#dens + 1] = r[2]
    end
    local lcm_d = dens[1]
    for i = 2, #dens do lcm_d = R.lcm(lcm_d, dens[i]) end
    local cycles = {}
    for _, r in ipairs(ratios) do
        cycles[#cycles + 1] = math.floor(base * lcm_d * r[1] / r[2])
    end
    return cycles
end

-- ── Generators ────────────────────────────────────────────────────────────

--- N voices with cycle lengths 2, 3, 4, …, N+1  (time sig (1+i)/X).
function R.make_sequential(n, bpm)
    local cycles = {}
    for i = 1, n do
        cycles[i] = i + 1
    end
    return {
        name = string.format("Sequential %d", n),
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Cycles drawn from multiples of 3, 4, and 5 interleaved over `rounds`:
---   round 1: 3, 4, 5    round 2: 6, 8, 10    round 3: 9, 12, 15  …
function R.make_varying_345(rounds, bpm)
    local bases = {3, 4, 5}
    local cycles = {}
    for r = 1, rounds do
        for _, b in ipairs(bases) do
            cycles[#cycles + 1] = b * r
        end
    end
    return {
        name = string.format("Varying 345 x%d", rounds),
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Phase-shift: same cycle length, different phase offsets (rotating interference).
--- Note: phase_offset support requires engine changes to delay first trigger per voice.
--- For now this generator produces same-cycle presets; phase is structural only.
function R.make_phase_shift(cycle, num_voices, bpm)
    local cycles = {}
    for i = 1, num_voices do cycles[i] = cycle end
    return {
        name = string.format("Phase Shift %d×%d", cycle, num_voices),
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Co-prime explosion: strict primes, LCM grows very fast.
function R.make_coprime(primes_or_count, bpm)
    local cycles = type(primes_or_count) == "number"
        and R.first_n_primes(primes_or_count)
        or primes_or_count
    return {
        name = "Co-prime " .. table.concat(cycles, ":"),
        bpm  = bpm or 100,
        cycles = cycles,
    }
end

--- Fibonacci-based cycles (organic spacing, golden-ratio feel).
--- make_fibonacci(start_idx, end_idx, bpm) – slice of true Fibonacci.
--- make_fibonacci(n, bpm) – shorthand for indices 4..3+n (3,5,8,13,...).
function R.make_fibonacci(start_idx, end_idx, bpm)
    local cycles
    if end_idx == nil or (type(end_idx) == "number" and end_idx > 30) then
        -- Shorthand: (n) or (n, bpm) -> indices 4 .. 3+n
        bpm = end_idx
        end_idx = 3 + start_idx
        start_idx = 4
        cycles = R.fibonacci_cycles(start_idx, end_idx)
    else
        cycles = R.fibonacci_cycles(start_idx, end_idx)
    end
    return {
        name = string.format("Fibonacci %d-%d", start_idx, end_idx),
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Power-of-two fracture: binary grid + one rogue destabilizer.
function R.make_power_fracture(pow_max, rogue, bpm)
    local cycles = {}
    for p = 2, pow_max do cycles[#cycles + 1] = 2^p end
    if rogue then cycles[#cycles + 1] = rogue end
    return {
        name = rogue and string.format("Power Fracture +%d", rogue) or "Power Fracture",
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Tuplet stack: 4/4 subdivided into 5, 7, 9... (different subdivision density).
function R.make_tuplet_stack(subdivisions, bpm)
    return {
        name = "Tuplet " .. table.concat(subdivisions, ":"),
        bpm  = bpm or 120,
        cycles = subdivisions,
    }
end

--- Rotational symmetry: divisors of master number (clean mathematical closure).
function R.make_rotational(master, min_div, max_div, bpm)
    local cycles = R.rotational_symmetry(master, min_div, max_div)
    return {
        name = string.format("Rotational %d", master),
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Prime gap waves: use gaps between primes as cycle lengths.
function R.make_prime_gaps(max_prime, bpm)
    local gaps = R.prime_gaps(max_prime)
    local cycles = {}
    for _, g in ipairs(gaps) do
        if g >= 2 then cycles[#cycles + 1] = g end
    end
    return {
        name = string.format("Prime Gaps ≤%d", max_prime),
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Mirror system: add voice with align_beats - C (meets at opposite phase).
function R.make_mirror(base_preset)
    local cycles = {}
    for _, c in ipairs(base_preset.cycles) do cycles[#cycles + 1] = c end
    local align = R.lcm_list(cycles)
    cycles[#cycles + 1] = align - cycles[1]  -- mirror of first voice
    return {
        name = (base_preset.name or "Mirror") .. " +mirror",
        bpm  = base_preset.bpm or 120,
        cycles = cycles,
    }
end

--- Euclidean multi-layer: combine several (steps, pulses) pairs as cycle lengths.
function R.make_euclidean(...)
    local pairs = {...}
    local cycles, names = {}, {}
    for i = 1, #pairs, 2 do
        local s, p = pairs[i], pairs[i + 1]
        cycles[#cycles + 1] = s
        names[#names + 1] = string.format("%d/%d", p or 0, s)
    end
    return {
        name = "Euclidean " .. table.concat(names, " "),
        bpm  = 120,
        cycles = cycles,
    }
end

--- Harmonic ratio set (just intonation consonance).
function R.make_harmonic_ratios(ratios, base, bpm)
    local cycles = R.harmonic_ratio_cycles(ratios, base)
    return {
        name = "Harmonic Ratios",
        bpm  = bpm or 120,
        cycles = cycles,
    }
end

--- Spiral system: phase velocity gradient (traveling wave feel).
--- Uses Format C: direct periods_sec for fractional periods.
function R.make_spiral(base_sec, epsilon, n, align_sec)
    local periods = R.spiral_periods(base_sec, epsilon, n)
    align_sec = align_sec or (periods[n] * n * 2)  -- conservative
    return {
        name = string.format("Spiral ε=%.2f", epsilon),
        align_sec = align_sec,
        periods_sec = periods,
    }
end

--- Spiral with true LCM alignment. Uses epsilon = 1/m so periods are rational.
--- period_i = base_sec * (m + i - 1) / m. All voices align at align_sec.
--- m >= n-1; larger m = tighter spiral (periods closer together).
function R.make_spiral_aligned(base_sec, n, m)
    m = m or math.max(n - 1, 1)
    local periods = {}
    for i = 1, n do
        periods[i] = base_sec * (m + i - 1) / m
    end
    local denoms = {}
    for i = 0, n - 1 do denoms[#denoms + 1] = m + i end
    local lcm_val = R.lcm_list(denoms)
    local align_sec = base_sec * lcm_val / m
    return {
        name = string.format("Spiral Aligned n=%d 1/%d", n, m),
        align_sec = align_sec,
        periods_sec = periods,
    }
end

--- Irrational tempo (micro-detune BPM): never-perfectly-looping, analog phasing.
--- Uses Format C: per-voice period from detuned BPM.
function R.make_irrational(base_bpm, detune_bpm, n, align_sec)
    local periods = R.irrational_periods(base_bpm, detune_bpm, n)
    align_sec = align_sec or 300.0  -- 5 min default; true align can be very long
    return {
        name = string.format("Irrational %.1f+%.3f", base_bpm, detune_bpm),
        align_sec = align_sec,
        periods_sec = periods,
    }
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
    -- Generated: sequential polyrhythm (N voices, cycles 2..N+1)
    seq_8  = R.make_sequential(8),
    seq_12 = R.make_sequential(12),
    seq_16 = R.make_sequential(16),

    -- Generated: varying 3/4/5 polyrhythm (interleaved multiples of 3, 4, 5)
    vary_345_x2 = R.make_varying_345(2),   -- 6 voices:  3,4,5,6,8,10
    vary_345_x3 = R.make_varying_345(3),   -- 9 voices:  + 9,12,15
    vary_345_x4 = R.make_varying_345(4),   -- 12 voices: + 12,16,20
    vary_345_x5 = R.make_varying_345(5),   -- 15 voices: + 15,20,25

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

    -- ── Phase-shift (same cycle, rotating interference) ─────────────────────
    phase_16x4 = R.make_phase_shift(16, 4),

    -- ── Co-prime explosion (rare alignment, long-evolving) ───────────────────
    coprime_4   = R.make_coprime(4),    -- {2,3,5,7}
    coprime_5   = R.make_coprime(5),    -- {2,3,5,7,11}
    coprime_7_13 = R.make_coprime({7, 11, 13, 17}),

    -- ── Fibonacci (organic, golden-ratio feel) ─────────────────────────────
    fib_4 = R.make_fibonacci(4),   -- 3,5,8,13
    fib_5 = R.make_fibonacci(5),   -- 3,5,8,13,21
    fib_6 = R.make_fibonacci(6),

    -- ── Power-of-two fracture (grid + rogue) ───────────────────────────────
    pow_fracture     = R.make_power_fracture(5),         -- 4,8,16,32
    pow_fracture_7   = R.make_power_fracture(5, 7),      -- 4,8,16,32,7

    -- ── Tuplet stack (subdivision mismatch) ─────────────────────────────────
    tuplet_5_7_9 = R.make_tuplet_stack({5, 7, 9}),
    tuplet_3_5_7 = R.make_tuplet_stack({3, 5, 7}),

    -- ── Rotational symmetry (divisors of master) ───────────────────────────────
    rot_60   = R.make_rotational(60),
    rot_60_compact = R.make_rotational(60, 3, 15),
    rot_24   = R.make_rotational(24, 2, 12),

    -- ── Prime gap waves ─────────────────────────────────────────────────────
    prime_gaps = R.make_prime_gaps(23),

    -- ── Mirror system (opposite-phase tension) ────────────────────────────────
    mirror_prime_pair = R.make_mirror(R.make_coprime({5, 7})),

    -- ── Euclidean (Bjorklund-inspired cycle lengths) ──────────────────────────
    euclidean_basic = R.make_euclidean(8, 3, 12, 5, 16, 7),
    euclidean_5_13  = R.make_euclidean(13, 5, 16, 7),

    -- ── Harmonic ratios (just intonation) ────────────────────────────────────
    harmonic_ratios = R.make_harmonic_ratios(
        {{1,1}, {5,4}, {3,2}, {7,4}, {2,1}}, 4),

    -- ── Spiral (phase velocity gradient, Format C) ───────────────────────────
    spiral_smooth = R.make_spiral(0.5, 0.08, 16, 90.0),
    spiral_tight  = R.make_spiral(0.4, 0.05, 6, 60.0),
    spiral_aligned = R.make_spiral_aligned(2, 8, 8),
    spiral_aligned_24 = R.make_spiral_aligned(2, 24, 24),

    -- ── Irrational tempo (micro-detune BPM, Format C) ───────────────────────
    irrational_breathing = R.make_irrational(120.0, 0.02, 4, 300.0),
}

-- Compute derived fields for every preset.
for _, preset in pairs(R.presets) do
    if preset.cycles then
        preset.align_beats = R.lcm_list(preset.cycles)
        preset.align_sec   = preset.align_beats * seconds_per_minute / preset.bpm
        preset.num_voices  = #preset.cycles
    elseif preset.periods_sec then
        preset.num_voices  = #preset.periods_sec
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

-- ── Selection & resolution ───────────────────────────────────────────────────

--- Derive align_beats, align_sec, num_voices, notes_per_align from raw preset.
--- Call this on function-returned or ad-hoc presets before use.
function R.derive(preset)
    if preset.cycles then
        preset.align_beats = R.lcm_list(preset.cycles)
        preset.align_sec   = preset.align_beats * seconds_per_minute / (preset.bpm or 120)
        preset.num_voices  = #preset.cycles
    elseif preset.periods_sec then
        preset.num_voices  = #preset.periods_sec
    else
        preset.notes_per_align = preset.notes_per_align or {}
        for i = 1, preset.num_voices do
            preset.notes_per_align[i] = preset.notes_start + (i - 1) * preset.notes_step
        end
    end
end

--- Resolve rhythm spec: preset key (string), function(rhythm), or raw preset table.
--- Returns a fully derived preset ready for use.
function R.get(spec, bpm_override)
    local preset
    if type(spec) == "function" then
        preset = spec(R)
    elseif type(spec) == "table" and spec.cycles or spec.periods_sec or spec.notes_start then
        preset = spec
    else
        preset = R.presets[spec]
        assert(preset, "Unknown rhythm preset: " .. tostring(spec))
    end
    R.derive(preset)
    if bpm_override and preset.cycles and preset.bpm then
        preset.bpm       = bpm_override
        preset.align_sec = preset.align_beats * seconds_per_minute / preset.bpm
    end
    return preset
end

function R.select(preset_key, bpm_override)
    return R.get(preset_key, bpm_override)
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
        elseif p.periods_sec then
            min_p, max_p = math.huge, 0
            for _, s in ipairs(p.periods_sec) do
                min_p = math.min(min_p, s)
                max_p = math.max(max_p, s)
            end
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
