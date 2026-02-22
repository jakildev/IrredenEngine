-- Polyrhythmic MIDI + Animation Demo
--
-- Each row of voxels has a different period length, creating a visual
-- polyrhythm. When paired with C_MidiNote, each row fires its note
-- at the start of every cycle. The result: independent rhythmic voices
-- that drift in and out of phase.
--
-- Swap main.lua for this file to run (or change runScript in main_lua.cpp).

local num_voices = 5
local base_period = 1.0
local bounce_height = 30.0
local spacing = 6

local function fract(x)
    return x - math.floor(x)
end

local function hsv_to_rgb(h, s, v)
    local hh = fract(h) * 6.0
    local c = v * s
    local x = c * (1.0 - math.abs((hh % 2.0) - 1.0))
    local m = v - c
    local r, g, b = 0.0, 0.0, 0.0
    if hh < 1.0 then r, g = c, x
    elseif hh < 2.0 then r, g = x, c
    elseif hh < 3.0 then g, b = c, x
    elseif hh < 4.0 then g, b = x, c
    elseif hh < 5.0 then r, b = x, c
    else r, b = c, x
    end
    return r + m, g + m, b + m
end

-- Voices: period multiplier ratios for polyrhythm (e.g., 3 against 4 against 5)
local voices = {
    { ratio = 1, note = MidiNote.C4,  velocity = 110 },
    { ratio = 2, note = MidiNote.E4,  velocity = 100 },
    { ratio = 3, note = MidiNote.G4,  velocity = 90  },
    { ratio = 4, note = MidiNote.B4,  velocity = 80  },
    { ratio = 5, note = MidiNote.D5,  velocity = 70  },
}

-- Create polyrhythmic entities: each is a visible bouncing voxel that
-- also triggers a MIDI note every time its oscillator cycle completes.
IREntity.createEntityBatchPolyrhythm(
    ivec3.new(num_voices, 1, 1),
    function(params)
        local x = (params.index.x - (num_voices - 1) * 0.5) * spacing
        return C_Position3D.new(vec3.new(x, 0.0, 0.0))
    end,
    function(params)
        local voice = voices[params.index.x + 1]
        local hue = params.index.x / num_voices
        local r, g, b = hsv_to_rgb(hue, 0.9, 0.95)
        return C_VoxelSetNew.new(
            ivec3.new(2, 2, 2),
            Color.new(
                math.floor(r * 255),
                math.floor(g * 255),
                math.floor(b * 255),
                255
            )
        )
    end,
    function(params)
        local voice = voices[params.index.x + 1]
        local period = base_period * voice.ratio
        local amp = vec3.new(0.0, 0.0, bounce_height)

        local idle = C_PeriodicIdle.new(amp, period, 0.0)
        idle:addStageDurationSeconds(0.0, period * 0.5, 0.0, 1.0,
            IREasingFunction.SINE_EASE_OUT)
        idle:addStageDurationSeconds(period * 0.5, period * 0.5, 1.0, 0.0,
            IREasingFunction.SINE_EASE_IN)
        return idle
    end,
    function(params)
        local voice = voices[params.index.x + 1]
        return C_MidiNote.new(voice.note, voice.velocity, 0, 0.1)
    end
)

-- Create a MIDI sequence that plays a simple arpeggio pattern alongside
-- the polyrhythmic triggers above.
local seq = C_MidiSequence.new(120.0, 4, 4, 2)
local eighth = 0.125
for measure = 0, 1 do
    local arp_notes = { MidiNote.C3, MidiNote.E3, MidiNote.G3, MidiNote.B3,
                        MidiNote.G3, MidiNote.E3, MidiNote.C3, MidiNote.E3 }
    for i, note in ipairs(arp_notes) do
        seq:insertNote(measure + (i - 1) * eighth, 0.08, note, 70)
    end
end
IREntity.createMidiSequence(seq)
