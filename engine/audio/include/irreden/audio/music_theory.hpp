#ifndef IR_MUSIC_THEORY_H
#define IR_MUSIC_THEORY_H

#include <array>
#include <cstddef>

namespace IRAudio {

// ── Pitch-class names (semitone offset from C) ──────────────────────────────

/// Semitone offset from C within a single octave (0 = C, 11 = B).
/// Enharmonic equivalents (e.g. C# and Db) share the same integer value.
/// Use with @ref rootMidiNote to compute absolute MIDI note numbers.
enum IRNoteName {
    NOTE_NAME_C       = 0,
    NOTE_NAME_C_SHARP = 1,
    NOTE_NAME_D_FLAT  = 1,  ///< Enharmonic alias for C#
    NOTE_NAME_D       = 2,
    NOTE_NAME_D_SHARP = 3,
    NOTE_NAME_E_FLAT  = 3,  ///< Enharmonic alias for D#
    NOTE_NAME_E       = 4,
    NOTE_NAME_F       = 5,
    NOTE_NAME_F_SHARP = 6,
    NOTE_NAME_G_FLAT  = 6,  ///< Enharmonic alias for F#
    NOTE_NAME_G       = 7,
    NOTE_NAME_G_SHARP = 8,
    NOTE_NAME_A_FLAT  = 8,  ///< Enharmonic alias for G#
    NOTE_NAME_A       = 9,
    NOTE_NAME_A_SHARP = 10,
    NOTE_NAME_B_FLAT  = 10, ///< Enharmonic alias for A#
    NOTE_NAME_B       = 11,
};

/// Number of distinct pitch classes in an octave (chromatic scale size).
constexpr int kNoteNamesCount = 12;

/// Returns the MIDI note number for a given pitch class and octave.
/// Convention: C4 (middle C) = 60.
/// Formula: `name + (octave + 1) * 12` where octave -1 starts at MIDI 0.
constexpr int rootMidiNote(IRNoteName name, int octave) {
    return static_cast<int>(name) + (octave + 1) * 12;
}

/// Integer overload of @ref rootMidiNote for computed pitch-class values.
constexpr int rootMidiNote(int nameValue, int octave) {
    return nameValue + (octave + 1) * 12;
}

// ── Scale / mode catalogue ──────────────────────────────────────────────────

/// Catalogue of scale and mode identifiers.
/// Each entry (except aliases) maps to a row in @ref kScaleDefinitions.
/// Aliases (@ref SCALE_MAJOR, @ref SCALE_MINOR) share definitions with
/// their Church-mode counterparts; their integer values are equal.
/// Use @ref scaleModeIndex to convert to a @ref kScaleDefinitions index.
enum IRScaleMode {
    // --- Diatonic modes (Church modes) ---
    SCALE_IONIAN = 0,          ///< Major (W W H W W W H)
    SCALE_DORIAN,              ///< Minor with raised 6th
    SCALE_PHRYGIAN,            ///< Minor with lowered 2nd
    SCALE_LYDIAN,              ///< Major with raised 4th
    SCALE_MIXOLYDIAN,          ///< Major with lowered 7th (dominant)
    SCALE_AEOLIAN,             ///< Natural minor (W H W W H W W)
    SCALE_LOCRIAN,             ///< Diminished (H W W H W W W)

    // --- Melodic / harmonic variants ---
    SCALE_HARMONIC_MINOR,      ///< Natural minor with raised 7th
    SCALE_MELODIC_MINOR,       ///< Natural minor with raised 6th and 7th
    SCALE_HUNGARIAN_MINOR,     ///< Harmonic minor with raised 4th
    SCALE_DOUBLE_HARMONIC,     ///< Byzantine / Arabic — raised 4th and 7th
    SCALE_NEAPOLITAN_MINOR,    ///< Harmonic minor with lowered 2nd
    SCALE_NEAPOLITAN_MAJOR,    ///< Melodic minor with lowered 2nd
    SCALE_ENIGMATIC,           ///< Verdi's enigmatic scale
    SCALE_PERSIAN,             ///< Ancient Persian / Arabic scale

    // --- Pentatonic ---
    SCALE_PENTATONIC_MAJOR,    ///< Major without 4th and 7th {0,2,4,7,9}
    SCALE_PENTATONIC_MINOR,    ///< Minor without 2nd and 6th {0,3,5,7,10}
    SCALE_HIRAJOSHI,           ///< Japanese koto scale {0,2,3,7,8}
    SCALE_IN_SEN,              ///< Japanese "In" pentatonic {0,1,5,7,10}
    SCALE_IWATO,               ///< Japanese Buddhist {0,1,5,6,10}
    SCALE_PELOG,               ///< Balinese pentatonic {0,1,3,7,8}

    // --- Hexatonic ---
    SCALE_WHOLE_TONE,          ///< All whole steps {0,2,4,6,8,10}
    SCALE_BLUES,               ///< Minor pentatonic + b5 {0,3,5,6,7,10}
    SCALE_AUGMENTED,           ///< Symmetric augmented {0,3,4,7,8,11}
    SCALE_PROMETHEUS,          ///< Scriabin's mystic chord scale
    SCALE_TRITONE,             ///< Symmetric tritone scale

    // --- Octatonic ---
    SCALE_DIMINISHED_WHOLE_HALF, ///< Whole-half diminished (8 notes)
    SCALE_DIMINISHED_HALF_WHOLE, ///< Half-whole diminished (8 notes)
    SCALE_BEBOP_DOMINANT,      ///< Mixolydian + major 7th
    SCALE_BEBOP_MAJOR,         ///< Major scale + augmented 5th

    // --- Chromatic ---
    SCALE_CHROMATIC,           ///< All 12 semitones

    NUM_SCALE_MODES,           ///< Sentinel — total count of unique mode entries

    // --- Aliases (usable but map to existing entries) ---
    SCALE_MAJOR = SCALE_IONIAN,   ///< Alias for @ref SCALE_IONIAN
    SCALE_MINOR = SCALE_AEOLIAN,  ///< Alias for @ref SCALE_AEOLIAN
};

// ── Scale interval data ─────────────────────────────────────────────────────

/// Interval pattern for a single scale or mode.
/// @p intervals holds up to 12 semitone offsets from the root (always starts
/// with 0); @p size is the number of valid entries used.
struct ScaleDefinition {
    int intervals[12]; ///< Semitone offsets from the root note (0 … 11).
    int size;          ///< Number of notes in the scale (5 … 12).
};

/// Converts an @ref IRScaleMode to its @ref kScaleDefinitions index.
/// Aliases (@ref SCALE_MAJOR, @ref SCALE_MINOR) share indices with their
/// Church-mode counterparts, so this is always a safe cast.
constexpr std::size_t scaleModeIndex(IRScaleMode mode) {
    return static_cast<std::size_t>(mode);
}

/// Total number of unique scale definitions (excludes aliases).
constexpr std::size_t kNumScaleModes = static_cast<std::size_t>(NUM_SCALE_MODES);

/// Compile-time table of all scale interval patterns, indexed by @ref IRScaleMode.
/// Aliases are not present — use the underlying mode value as the index.
inline constexpr std::array<ScaleDefinition, kNumScaleModes> kScaleDefinitions = {{

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
}};

/// Returns the @ref ScaleDefinition for a given mode.
inline constexpr const ScaleDefinition &getScaleDefinition(IRScaleMode mode) {
    return kScaleDefinitions[scaleModeIndex(mode)];
}

/// Returns the number of notes in the given scale (5 … 12).
inline constexpr int getScaleSize(IRScaleMode mode) {
    return kScaleDefinitions[scaleModeIndex(mode)].size;
}

/// Returns a pointer to the semitone-offset array for the given scale.
/// The array has @ref getScaleSize entries; element 0 is always 0 (root).
inline constexpr const int *getScaleIntervals(IRScaleMode mode) {
    return kScaleDefinitions[scaleModeIndex(mode)].intervals;
}

} // namespace IRAudio

#endif /* IR_MUSIC_THEORY_H */
