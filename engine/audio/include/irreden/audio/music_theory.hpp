#ifndef IR_MUSIC_THEORY_H
#define IR_MUSIC_THEORY_H

namespace IRAudio {

// ── Pitch-class names (semitone offset from C) ──────────────────────────────

enum IRNoteName {
    NOTE_NAME_C       = 0,
    NOTE_NAME_C_SHARP = 1,
    NOTE_NAME_D_FLAT  = 1,
    NOTE_NAME_D       = 2,
    NOTE_NAME_D_SHARP = 3,
    NOTE_NAME_E_FLAT  = 3,
    NOTE_NAME_E       = 4,
    NOTE_NAME_F       = 5,
    NOTE_NAME_F_SHARP = 6,
    NOTE_NAME_G_FLAT  = 6,
    NOTE_NAME_G       = 7,
    NOTE_NAME_G_SHARP = 8,
    NOTE_NAME_A_FLAT  = 8,
    NOTE_NAME_A       = 9,
    NOTE_NAME_A_SHARP = 10,
    NOTE_NAME_B_FLAT  = 10,
    NOTE_NAME_B       = 11,
};

constexpr int kNoteNamesCount = 12;

// MIDI note number from pitch-class + octave.
// Convention: C4 = 60 (middle C).  Formula: name + (octave + 1) * 12.
constexpr int rootMidiNote(IRNoteName name, int octave) {
    return static_cast<int>(name) + (octave + 1) * 12;
}

constexpr int rootMidiNote(int nameValue, int octave) {
    return nameValue + (octave + 1) * 12;
}

// ── Scale / mode catalogue ──────────────────────────────────────────────────

enum IRScaleMode {
    // --- Diatonic modes (Church modes) ---
    SCALE_IONIAN = 0,          // Major
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_AEOLIAN,             // Natural minor
    SCALE_LOCRIAN,

    // --- Melodic / harmonic variants ---
    SCALE_HARMONIC_MINOR,
    SCALE_MELODIC_MINOR,
    SCALE_HUNGARIAN_MINOR,
    SCALE_DOUBLE_HARMONIC,     // Byzantine
    SCALE_NEAPOLITAN_MINOR,
    SCALE_NEAPOLITAN_MAJOR,
    SCALE_ENIGMATIC,
    SCALE_PERSIAN,

    // --- Pentatonic ---
    SCALE_PENTATONIC_MAJOR,
    SCALE_PENTATONIC_MINOR,
    SCALE_HIRAJOSHI,           // Japanese
    SCALE_IN_SEN,
    SCALE_IWATO,
    SCALE_PELOG,

    // --- Hexatonic ---
    SCALE_WHOLE_TONE,
    SCALE_BLUES,
    SCALE_AUGMENTED,
    SCALE_PROMETHEUS,
    SCALE_TRITONE,

    // --- Octatonic ---
    SCALE_DIMINISHED_WHOLE_HALF,
    SCALE_DIMINISHED_HALF_WHOLE,
    SCALE_BEBOP_DOMINANT,
    SCALE_BEBOP_MAJOR,

    // --- Chromatic ---
    SCALE_CHROMATIC,

    NUM_SCALE_MODES,           // sentinel (count of unique entries)

    // --- Aliases (usable but map to existing entries) ---
    SCALE_MAJOR = SCALE_IONIAN,
    SCALE_MINOR = SCALE_AEOLIAN,
};

// ── Scale interval data ─────────────────────────────────────────────────────

struct ScaleDefinition {
    int intervals[12];
    int size;
};

// Indexed by IRScaleMode (unique entries only, not aliases).
// NUM_SCALE_MODES must equal the number of entries below.
inline constexpr ScaleDefinition kScaleDefinitions[] = {

    // --- Diatonic modes ---
    /* IONIAN       */ {{0, 2, 4, 5, 7, 9, 11},              7},
    /* DORIAN       */ {{0, 2, 3, 5, 7, 9, 10},              7},
    /* PHRYGIAN     */ {{0, 1, 3, 5, 7, 8, 10},              7},
    /* LYDIAN       */ {{0, 2, 4, 6, 7, 9, 11},              7},
    /* MIXOLYDIAN   */ {{0, 2, 4, 5, 7, 9, 10},              7},
    /* AEOLIAN      */ {{0, 2, 3, 5, 7, 8, 10},              7},
    /* LOCRIAN      */ {{0, 1, 3, 5, 6, 8, 10},              7},

    // --- Melodic / harmonic variants ---
    /* HARMONIC_MINOR    */ {{0, 2, 3, 5, 7, 8, 11},         7},
    /* MELODIC_MINOR     */ {{0, 2, 3, 5, 7, 9, 11},         7},
    /* HUNGARIAN_MINOR   */ {{0, 2, 3, 6, 7, 8, 11},         7},
    /* DOUBLE_HARMONIC   */ {{0, 1, 4, 5, 7, 8, 11},         7},
    /* NEAPOLITAN_MINOR  */ {{0, 1, 3, 5, 7, 8, 11},         7},
    /* NEAPOLITAN_MAJOR  */ {{0, 1, 3, 5, 7, 9, 11},         7},
    /* ENIGMATIC         */ {{0, 1, 4, 6, 8, 10, 11},        7},
    /* PERSIAN           */ {{0, 1, 4, 5, 6, 8, 11},         7},

    // --- Pentatonic ---
    /* PENTATONIC_MAJOR  */ {{0, 2, 4, 7, 9},                5},
    /* PENTATONIC_MINOR  */ {{0, 3, 5, 7, 10},               5},
    /* HIRAJOSHI         */ {{0, 2, 3, 7, 8},                5},
    /* IN_SEN            */ {{0, 1, 5, 7, 10},               5},
    /* IWATO             */ {{0, 1, 5, 6, 10},               5},
    /* PELOG             */ {{0, 1, 3, 7, 8},                5},

    // --- Hexatonic ---
    /* WHOLE_TONE        */ {{0, 2, 4, 6, 8, 10},            6},
    /* BLUES             */ {{0, 3, 5, 6, 7, 10},            6},
    /* AUGMENTED         */ {{0, 3, 4, 7, 8, 11},            6},
    /* PROMETHEUS        */ {{0, 2, 4, 6, 9, 10},            6},
    /* TRITONE           */ {{0, 1, 4, 6, 7, 10},            6},

    // --- Octatonic ---
    /* DIMINISHED_WH     */ {{0, 2, 3, 5, 6, 8, 9, 11},     8},
    /* DIMINISHED_HW     */ {{0, 1, 3, 4, 6, 7, 9, 10},     8},
    /* BEBOP_DOMINANT    */ {{0, 2, 4, 5, 7, 9, 10, 11},     8},
    /* BEBOP_MAJOR       */ {{0, 2, 4, 5, 7, 8, 9, 11},      8},

    // --- Chromatic ---
    /* CHROMATIC  */ {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 12},
};

static_assert(
    sizeof(kScaleDefinitions) / sizeof(kScaleDefinitions[0]) == NUM_SCALE_MODES,
    "kScaleDefinitions must have exactly NUM_SCALE_MODES entries"
);

inline constexpr const ScaleDefinition &getScaleDefinition(IRScaleMode mode) {
    return kScaleDefinitions[static_cast<int>(mode)];
}

inline constexpr int getScaleSize(IRScaleMode mode) {
    return kScaleDefinitions[static_cast<int>(mode)].size;
}

inline constexpr const int *getScaleIntervals(IRScaleMode mode) {
    return kScaleDefinitions[static_cast<int>(mode)].intervals;
}

} // namespace IRAudio

#endif /* IR_MUSIC_THEORY_H */
